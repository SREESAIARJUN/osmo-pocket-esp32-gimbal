# Cascaded PID Design: Gimbal Roll Axis on ESP32-S3

I don't have the details of your "Cycle 3" (motor, IMU, prior-cycle results), so this design states its assumptions explicitly. Replace the marked values with your measured data.

---

## 1. System Assumptions

| Item | Assumed value | Notes |
|---|---|---|
| Actuator | Gimbal BLDC (e.g., 2804/GBM2804, ~100 Ω-class winding) driven by FOC in voltage/torque mode | Output of rate loop = q-axis voltage/torque command |
| Sensor | 6-axis IMU (ICM-42688-P / MPU-6050-class) over SPI | Gyro ODR ≥ 2 kHz preferred |
| Attitude estimate | Mahony/complementary filter producing roll angle φ | Runs at the inner-loop rate |
| MCU | ESP32-S3 @ 240 MHz, single-precision FPU | `float` math is fast. Avoid `double`. |
| Inner (rate) loop rate | f_s,in = 1 kHz (T_s = 1 ms) | |
| Outer (angle) loop rate | f_s,out = 250–500 Hz (decimated) | |
| Output limit | u ∈ [−U_max, +U_max], e.g., ±6 V or normalized ±1 | Sets torque saturation |

---

## 2. Loop Architecture

```
 φ_ref ──►(+)──► Angle PI ──► ω_ref ──►(+)──► Rate PID ──► τ_cmd ──► FOC ──► Motor/Camera
          ▲−      (outer)     [clamp]   ▲−     (inner)    [clamp]                 │
          │                              │                                        │
          φ̂ ◄── Attitude filter ◄────────┴──── gyro ω (LPF + notch) ◄──── IMU ◄──┘
                                    ◄── saturation flag (inner → outer) ──
```

**Design rules:**

- **Bandwidth separation.** The inner loop must be 5–10× faster than the outer loop. Typical targets: inner rate loop about 40–80 Hz crossover, outer angle loop about 5–10 Hz.
- **Outer loop controller.** Use P or PI only. A D term in the outer loop is redundant because the inner rate loop already provides the damping.
- **Inner loop controller.** Use full PID with D on measurement. Its D term acts on angular acceleration, so it needs heavy filtering (Section 4).

---

## 3. Continuous-Time Control Laws

**Outer (angle) loop:**

$$
e_\phi = \operatorname{wrap}_{[-\pi,\pi]}(\phi_{ref}-\hat\phi)
$$

$$
\omega_{ref} = \operatorname{sat}_{\pm\Omega_{max}}\!\left(K_{p,\phi}\,e_\phi + K_{i,\phi}\!\int e_\phi\,dt + \omega_{ff}\right)
$$

- ω_ff is optional feedforward, the rate of change of φ_ref, used for smooth follow/pan modes.
- Ω_max is the maximum commanded slew rate, e.g., 180°/s.

**Inner (rate) loop:** a 2-DOF PID with setpoint weighting b and D on measurement:

$$
\tau_{cmd} = \operatorname{sat}_{\pm U_{max}}\!\Big(K_p(b\,\omega_{ref}-\omega) + K_i\!\int(\omega_{ref}-\omega)\,dt - K_d\,\frac{s}{1+sT_f}\,\omega\Big)
$$

- **b = 0.7–1.0.** Setpoint weighting reduces overshoot when the angle loop steps.
- **D acts on −ω, not on the error.** This avoids a "derivative kick" when ω_ref jumps.

---

## 4. Derivative Filtering

### 4.1 Filter Time Constant

The ideal derivative is replaced by a band-limited one:

$$
D(s) = \frac{K_d\,s}{1+sT_f}, \qquad T_f = \frac{K_d}{N\,K_p}, \quad N \in [5, 20]
$$

Practical constraints on T_f:

- **Lower bound for discrete stability and noise:** T_f ≥ 2T_s. A floor of 5T_s is recommended, i.e., ≥ 5 ms at 1 kHz.
- **Implied corner frequency:** f_c = 1/(2πT_f). Place it **3–5× above the rate-loop crossover** and **below the first mechanical resonance**. A typical starting value is f_c ≈ 150–250 Hz.

### 4.2 Discretization

**Backward Euler** (unconditionally stable; recommended):

$$
d[k] = \alpha\,d[k-1] - \frac{K_d}{T_f+T_s}\,\big(\omega[k]-\omega[k-1]\big), \qquad \alpha = \frac{T_f}{T_f+T_s}
$$

**Tustin (bilinear)** gives better phase near Nyquist, but rings if T_f < T_s/2:

$$
d[k] = \frac{2T_f-T_s}{2T_f+T_s}\,d[k-1] - \frac{2K_d}{2T_f+T_s}\big(\omega[k]-\omega[k-1]\big)
$$

### 4.3 Pre-Filtering the Gyro Before the PID

Pre-filter the gyro signal, and feed the same filtered signal to the P and D terms:

1. **Anti-alias filter.** Configure the IMU's on-chip digital low-pass filter to about 250–500 Hz.
2. **First-order low-pass in software:**
   $$
   \omega_f[k] = \omega_f[k-1] + \beta(\omega[k]-\omega_f[k-1]), \qquad \beta = \frac{T_s}{T_s+\frac{1}{2\pi f_{lp}}}, \quad f_{lp}\approx 150\text{–}200\text{ Hz}
   $$
3. **Optional biquad notch** at the measured frame/camera-plate resonance f_0, with Q ≈ 2–5:
   $$
   H(z)=\frac{1-2\cos\omega_0 z^{-1}+z^{-2}}{(1+\alpha_n)-2\cos\omega_0 z^{-1}+(1-\alpha_n)z^{-2}}, \qquad \omega_0=2\pi f_0T_s, \quad \alpha_n=\frac{\sin\omega_0}{2Q}
   $$
   Normalize the coefficients by (1+α_n).

**Phase budget:** every filter adds lag at crossover. Check that the total stays under about 20° at the inner crossover frequency f_c,in. The first-order LPF contributes approximately atan(f_c,in / f_lp).

---

## 5. Anti-Windup

### 5.1 Inner Loop: Conditional Integration (Clamping) Plus Hard Bound

Compute the unsaturated output, saturate it, and integrate only when doing so won't drive deeper into saturation:

$$
v[k] = P[k] + I[k] + d[k], \qquad u[k] = \operatorname{sat}_{\pm U_{max}}(v[k])
$$

$$
\text{sat\_flag} = \big(v[k]\neq u[k]\big)\ \wedge\ \big(\operatorname{sign}(e[k]) = \operatorname{sign}(v[k])\big)
$$

$$
I[k+1] = \begin{cases} I[k] & \text{if sat\_flag} \\ \operatorname{sat}_{\pm I_{max}}\!\big(I[k] + K_i T_s\,e[k]\big) & \text{otherwise}\end{cases}
$$

- **Integrator bound:** I_max ≈ 0.5–0.8·U_max. This keeps headroom for P and D.
- **Integrating with K_i inside the sum** (rather than multiplying K_i outside) lets you retune gains online without output bumps.

### 5.2 Alternative: Back-Calculation

Back-calculation recovers more smoothly after long saturation, such as when the gimbal hits a hard stop or the motor is under-powered:

$$
I[k+1] = I[k] + K_i T_s\,e[k] + \frac{T_s}{T_t}\big(u[k]-v[k]\big)
$$

- **Tracking time constant:** T_t = √(T_i T_d), with T_i = K_p/K_i and T_d = K_d/K_p. If there is no D term, use T_t ≈ 0.5·T_i.
- **Clamp the result anyway** to ±I_max as a safety bound.

### 5.3 Cascade Windup: Propagating Saturation Outward