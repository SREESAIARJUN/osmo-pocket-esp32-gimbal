# ESP32-S3 Gimbal Roll Axis: Cascaded PID Design (Cycle 5)

I don't have your project's Cycle 5 documents, so this design uses the stated assumptions. **Bold items** are the parameters to replace with your actual hardware values.

---

## 1. Assumptions & System Context

| Item | Assumed value |
|---|---|
| MCU | ESP32-S3 @ 240 MHz, single-precision FPU (use `float`, not `double`) |
| Actuator | Gimbal BLDC (e.g. GBM2804), FOC voltage- or current-mode, **Kt ≈ 0.07 N·m/A** |
| IMU | 6-axis (ICM-42688 / BMI270), gyro ODR **≥ 2 kHz**, SPI |
| Attitude estimate | Mahony/complementary filter → roll angle φ |
| Roll inertia (camera + bracket) | **J ≈ 2×10⁻⁴ kg·m²** |
| Output command | q-axis current I_q (or V_q in voltage mode), saturated at ±I_max |

---

## 2. Loop Architecture

```
 φ_ref ──►(+)──► [Angle PID] ──► ω_ref ──►(+)──► [Rate PID] ──► I_q ──► [FOC] ──► Motor ──► Roll dynamics
         ▲ -     (outer, 500 Hz)  │sat     ▲ -    (inner, 2 kHz)  │sat                          │
         │                        ▼        │                      ▼                             │
         │                   ±ω_max        │                 ±I_max ──sat flag──► outer AW      │
         │                                 └──────────── gyro_x (filtered) ◄────────────────────┤
         └────────────────────────────────── φ (Mahony) ◄───────────────────────────────────────┘
```

**Timing and bandwidth separation.** Each loop should run 5–10× faster in bandwidth than the loop that wraps it.

| Loop | Rate | Ts | Target closed-loop BW |
|---|---|---|---|
| FOC current | 10–20 kHz (MCPWM ISR) | 50–100 µs | > 500 Hz |
| **Inner rate (ω)** | **2 kHz** | 500 µs | **40–60 Hz** |
| **Outer angle (φ)** | **500 Hz** (every 4th inner tick) | 2 ms | **6–10 Hz** |

**Why each loop has the terms it has:**
- **Inner loop: PI + filtered D.** The integrator rejects constant disturbances, such as gravity torque from an imperfectly balanced camera and cable drag.
- **Outer loop: P (+ small I).** Because the inner loop already integrates, the outer loop can be mostly proportional. A small Ki removes residual steady-state error caused by IMU bias or estimator drift.

---

## 3. Discrete PID Equations

Both loops share one controller structure. Notation: sample index k, period Ts, reference r, measurement y, error e[k] = r[k] − y[k].

### 3.1 Proportional (with setpoint weighting)
$$P[k] = K_p\,(b\,r[k] - y[k]), \quad b \in [0,1]$$
Use b = 1 for the inner loop. Use b ≈ 0.7–1 for the outer loop to soften the response to step commands.

### 3.2 Derivative: on measurement, first-order low-pass filtered

Two choices protect the D term:
- **Differentiating the measurement −y instead of the error** avoids the "derivative kick" on setpoint steps.
- **A first-order low-pass filter** keeps gyro and estimator noise from being amplified.

Continuous form, where the filter time constant comes from the N-factor (typically N = 5–20):

$$D(s) = -\frac{K_d\, s}{1 + \tau_f s}\,Y(s), \qquad \tau_f = \frac{K_d}{K_p N} \quad\text{or}\quad \tau_f = \frac{1}{2\pi f_c}$$

**Tustin (bilinear) discretization**, recommended because it is stable for any τ_f and has correct high-frequency behavior:

$$\boxed{D[k] = \frac{2\tau_f - T_s}{2\tau_f + T_s}\,D[k-1] \;-\; \frac{2K_d}{2\tau_f + T_s}\,\big(y[k] - y[k-1]\big)}$$

Precompute these constants:
$$a_d = \frac{2\tau_f - T_s}{2\tau_f + T_s}, \qquad b_d = \frac{2K_d}{2\tau_f + T_s}$$

**Backward-Euler alternative** (simpler, slightly more phase lag):
$$\alpha = \frac{\tau_f}{\tau_f + T_s}, \qquad D[k] = \alpha D[k-1] - K_d(1-\alpha)\frac{y[k]-y[k-1]}{T_s}$$

**Cutoff guidance:**

| Loop | D cutoff f_c | Reason |
|---|---|---|
| Inner | 150–250 Hz | Stay well below Nyquist (1 kHz). Stay below any frame or motor resonance; find it with a chirp/sine-sweep test. Optionally add a notch at that resonance. |
| Outer | ~30–50 Hz, or Kd = 0 | Gyro rate is already the angle's derivative, so the inner loop supplies the damping. Outer Kd is usually unnecessary. |

> **Tip.** If the inner loop measures ω directly from the gyro, its D term uses dω/dt, which is angular acceleration and is noisy. Also pre-filter the gyro with a 2nd-order Butterworth at **~250–300 Hz** before the rate loop.

### 3.3 Integral (forward Euler)
$$I[k] = I[k-1] + K_i\,T_s\,e[k]$$

### 3.4 Output and saturation
$$u_{raw}[k] = P[k] + I[k] + D[k] + u_{ff}[k]$$
$$u[k] = \mathrm{sat}(u_{raw}[k]) = \min\big(\max(u_{raw}[k],\,u_{min}),\,u_{max}\big)$$

The feedforward term u_ff covers:
- **Inner loop:** inertia/friction feedforward, $u_{ff} = \frac{J}{K_t}\dot\omega_{ref} + K_{fric}\,\mathrm{sgn}(\omega_{ref})$
- **Outer loop:** target-rate feedforward $\dot\phi_{ref}$, which helps when following a smoothed stick or pan command.

---

## 4. Anti-Windup

Use **three layers** of protection.

### 4.1 Layer 1: Conditional integration (clamping)
Freeze the integrator when the output is saturated **and** the error would push it further into saturation:

$$
I[k] = \begin{cases}
I[k-1] & \text{if } (u_{raw} > u_{max} \wedge e > 0) \;\lor\; (u_{raw} < u_{min} \wedge e < 0) \\
I[k-1] + K_i T_s\, e[k] & \text{otherwise}
\end{cases}
$$

Evaluate this with a *tentative* u_raw computed from I[k−1], so integration is decided before committing the new value.

### 4.2 Layer 2: Back-calculation (smooth unwinding)
Add a bleed term proportional to the saturation excess:

$$I[k] = I[k-1] + K_i T_s\, e[k] + \frac{T_s}{T_t}\big(u[k-1] - u_{raw}[k-1]\big)$$

Choose the tracking time constant as $T_t = \sqrt{T_i T_d}$, where $T_i = K_p/K_i$ and $T_d = K_d/K_p$. If Kd = 0, use $T_t \approx 0.5\,T_i$.

You can use clamping alone, or clamping plus back-calculation. Together they give the fastest recovery from saturation.

### 4.3 Layer 3: Hard integrator bound
$$I[k] = \mathrm{sat}\big(I[k],\; -I_{lim},\; +I_{lim}\big), \qquad I_{lim} \approx 0.6\text{–}0.8 \times u_{max}$$

For the inner loop, size I_lim to cover the worst-case gravity imbalance torque plus margin, converted to current.

### 4.4 Cascade-specific: propagate saturation upward
When the **inner** loop saturates, the outer loop's command ω_ref cannot be realized. The outer integrator must therefore stop as well:

$$\text{outer\_freeze} = \text{inner\_saturated} \;\wedge\; \mathrm{sgn}(e_\phi) = \mathrm{sgn}(u_{inner})$$

The outer output is also limited, $|\omega_{ref}| \le \omega_{max}$ (e.g. **300 °/s**), and its own clamp logic applies to that limit.

### 4.5 Bumpless mode transfer
On enable, or when switching between follow and lock modes, initialize:
- I = u_current − P − D, and
- y[k−1] = y[k], so no derivative spike occurs