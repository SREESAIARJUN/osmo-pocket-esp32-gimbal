# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis (Cycle 12)

I don't have your Cycle 12 spec, so this design states its assumptions explicitly. Replace the bracketed values with your own numbers and the structure and equations still apply.

**Assumed hardware:**
- ESP32-S3 at 240 MHz, using its single-precision FPU.
- 6-axis IMU (e.g. ICM-42688-P) on SPI, gyro ODR 2 kHz (or higher).
- Gimbal BLDC motor driven by FOC, so the controller outputs a torque or current command `iq_ref` (or a voltage `uq` for voltage-mode FOC).
- Roll angle comes from a Mahony or complementary filter running at the inner-loop rate.

---

## 1. Architecture

```
θ_ref ──►(Σ)──► Angle PI(+FF) ──► ω_ref ──►(Σ)──► Rate PID ──► τ_cmd/iq_ref ──► FOC ──► Motor+Load
          ▲ −                       clamp ±ω_max   ▲ −              clamp ±I_max                  │
          │                                        │                                              │
          θ̂ ◄─── Attitude filter (Mahony) ◄────────┴──── ω_gyro (LPF + notch) ◄───── IMU ◄─────────┘
```

| Loop | Rate | Ts | Controller | Output limit | Target bandwidth |
|---|---|---|---|---|---|
| Inner (rate) | 2 kHz | 0.5 ms | PID, D-on-measurement, filtered | ±I_max (e.g. ±1.0 A) | 40–60 Hz |
| Outer (angle) | 500 Hz | 2 ms | PI + rate feedforward | ±ω_max (e.g. ±300 °/s) | 8–12 Hz (≤ ¼ of inner) |

**Bandwidth separation.** Keep the outer loop at or below about ¼–⅕ of the inner bandwidth. The angle loop then sees the rate loop as approximately unity gain with a small lag.

---

## 2. Plant Model (Roll Axis)

$$
J\,\dot\omega = K_t\, i_q - b\,\omega - \tau_{dist}, \qquad
G_\omega(s) = \frac{\omega(s)}{i_q(s)} = \frac{K_t}{Js + b}
$$

Here b is small, so the plant is nearly an integrator. To identify the parameters:
1. Apply a step of `iq` with the loop open and the motor restrained to small angles.
2. Measure the slope of ω:
$$
\frac{K_t}{J} \approx \frac{\Delta\dot\omega}{\Delta i_q}
$$

### Starting gains (loop shaping on the integrator plant)

**Inner rate loop**, with crossover ω_c,i = 2π·f_c,i:

$$
K_{p,\omega} = \frac{J\,\omega_{c,i}}{K_t}, \quad
K_{i,\omega} = K_{p,\omega}\frac{\omega_{c,i}}{5}, \quad
K_{d,\omega} = K_{p,\omega}\cdot T_d,\; T_d \approx \frac{1}{4\,\omega_{c,i}}
$$

**Outer angle loop**, where the inner loop is approximately 1:

$$
K_{p,\theta} = \omega_{c,o}, \qquad K_{i,\theta} = K_{p,\theta}\frac{\omega_{c,o}}{5}
$$

For example, f_c,o = 10 Hz gives K_p,θ ≈ 63 (rad/s)/rad.

---

## 3. Gyro Pre-Filtering (Before the Rate PID)

1. **Anti-alias/noise LPF.** Use a 2nd-order Butterworth biquad on ω_gyro with fc ≈ 150–250 Hz. That is at least 3× the inner bandwidth, so phase loss at crossover stays under about 10°.
2. **Notch filter.** Add one at any frame or motor resonance found in a sweep or FFT. Use Q ≈ 2–5.

Biquad, direct form II transposed:

$$
y = b_0 x + z_1,\quad z_1 = b_1 x - a_1 y + z_2,\quad z_2 = b_2 x - a_2 y
$$

---

## 4. Derivative Filtering

### 4.1 Continuous form

Take the derivative on the **measurement**, not the error. This avoids derivative kick when ω_ref steps, which the outer loop does every 2 ms.

$$
D(s) = -\frac{K_d\, s}{1 + s\,T_f}\, y(s), \qquad T_f = \frac{K_d}{N\,K_p},\; N \in [5, 20]
$$

- N sets the derivative's high-frequency gain cap at N·K_p.
- Use N ≈ 8–10 for the gimbal rate loop, because the D term on the gyro is angular acceleration and is noisy.
- Equivalently, choose the D-filter corner f_D = 1/(2πT_f) at about 80–120 Hz.

### 4.2 Discrete form, Tustin (bilinear), preferred

$$
D[k] = \alpha\, D[k-1] \;-\; \beta\,\big(y[k] - y[k-1]\big)
$$

$$
\alpha = \frac{2T_f - T_s}{2T_f + T_s}, \qquad \beta = \frac{2K_d}{2T_f + T_s}
$$

### 4.3 Discrete form, backward Euler (always stable, slightly more lag)

$$
D[k] = \frac{T_f}{T_f + T_s} D[k-1] \;-\; \frac{K_d}{T_f + T_s}\big(y[k]-y[k-1]\big)
$$

**Check:** with T_s = 0.5 ms and f_D = 100 Hz, T_f = 1.59 ms. That gives α = 0.521 for Tustin. Tustin needs T_f > T_s/2, and backward Euler is fine for any T_f.

### 4.4 Outer loop

Normally no D term is needed. The inner rate loop already provides damping, since ω is the derivative of θ.

---

## 5. Anti-Windup

### 5.1 Output saturation

$$
u = P + I + D, \qquad u_{sat} = \mathrm{clamp}(u,\, u_{min},\, u_{max})
$$

### 5.2 Conditional integration (clamping), used on both loops

Integrate only if the output is not saturated, or if integrating would drive it out of saturation:

$$
\text{freeze} = (u \ne u_{sat}) \;\wedge\; \big(\mathrm{sign}(e) = \mathrm{sign}(u)\big)
$$

$$
I[k] = \begin{cases} I[k-1] & \text{freeze} \\ I[k-1] + K_i\, T_s\, e[k] & \text{otherwise} \end{cases}
$$

This uses Ki·Ts·e (rectangular rule). Trapezoidal integration with ½Ts(e[k]+e[k−1]) can be used instead.

Add a hard integrator bound as a second layer:

$$
I[k] = \mathrm{clamp}(I[k],\, I_{min},\, I_{max}), \quad |I_{max}| \approx 0.5\text{–}0.7\,|u_{max}|
$$

The integrator only needs to cancel gravity imbalance, cable drag and friction, so it should never need the full output range.

### 5.3 Back-calculation, optional alternative for the rate loop

$$
I[k] = I[k-1] + T_s\Big(K_i\, e[k] + \frac{1}{T_t}\big(u_{sat}[k] - u[k]\big)\Big), \qquad T_t = \sqrt{T_i T_d}\ \text{(or } T_t \approx T_i/2)
$$

Back-calculation unwinds more smoothly than hard clamping. Use one method or the other on a given loop, not both.

### 5.4 Cascade windup, critical and often missed

When the inner loop saturates (current limit), the outer integrator is still seeing error it cannot fix. Propagate the inner saturation upward:

$$
\text{freeze}_{\theta