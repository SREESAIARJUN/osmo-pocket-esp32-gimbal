# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

> **Assumptions.** I don't have your project's "Cycle 25" spec sheet: motor constants, inertia, IMU model, or required bandwidth. The structure and equations below are standard and complete. Every number marked *(assumed)* is a placeholder to replace with your measured or required values.
>
> Assumed hardware: brushless gimbal motor driven by FOC/SVPWM, and a 6-axis IMU (ICM-42688 / MPU-6500 class) on SPI. The ESP32-S3 has a single-precision FPU, so all math uses `float`. Avoid `double`, which is software-emulated and slow.

---

## 1. Architecture

```
 θ_ref ──►(+)──► [Angle PI(D)] ──► ω_ref ──►(+)──► [Rate PID] ──► u (torque/Iq or V_q) ──► Motor/FOC ──► Gimbal
           ▲-          │ clamp ±ω_max         ▲-          │ clamp ±u_max
           │           │                      │           │
           θ̂ (attitude filter)                ω_gyro (LPF)  sat flag ──► freezes outer integrator
```

| Loop | Rate | Ts | Target bandwidth | Purpose |
|---|---|---|---|---|
| Inner (roll rate) | 2 kHz *(assumed)* | 0.5 ms | ~40–60 Hz | Reject torque disturbances, set damping |
| Outer (roll angle) | 500 Hz *(assumed)* | 2 ms | ~6–10 Hz | Hold/track angle |

**Rule:** keep the outer loop bandwidth 5–10× below the inner loop bandwidth. The angle loop can then treat the rate loop as an ideal unity gain.

---

## 2. Signal Conditioning

**Gyro low-pass filter** (1st-order IIR, cutoff `f_g` ≈ 150–250 Hz):

$$
\alpha_g = \frac{T_s}{T_s + \frac{1}{2\pi f_g}}, \qquad
\omega_f[k] = \omega_f[k-1] + \alpha_g\,(\omega_{raw}[k] - \omega_f[k-1])
$$

Prefer the IMU's internal digital LPF (UI filter) where possible. Add a notch filter if a frame or motor resonance appears in the gyro FFT.

**Angle estimate** θ̂: use a complementary filter or Mahony at ≥ 500 Hz.

$$
\hat\theta[k] = \beta\left(\hat\theta[k-1] + \omega_f T_s\right) + (1-\beta)\,\theta_{acc}, \quad \beta \approx 0.995\text{–}0.999
$$

---

## 3. Outer Loop: Angle Controller

Use the gyro as the derivative term. Since dθ/dt = ω, differentiating θ̂ is unnecessary and noisier.

$$
e_\theta[k] = \theta_{ref}[k] - \hat\theta[k]
$$

$$
\omega_{ref,unsat} = K_{p\theta}\,e_\theta + I_\theta[k] - K_{d\theta}\,\omega_f[k] + \omega_{ff}
$$

$$
\omega_{ref} = \text{sat}\left(\omega_{ref,unsat},\ \pm\omega_{max}\right), \quad \omega_{max}\approx 300^\circ/\text{s}\ \textit{(assumed)}
$$

- `ω_ff = dθ_ref/dt` is a feedforward term for smooth tracking of follow-mode commands.
- In practice `K_dθ` is usually 0. The inner rate loop already provides the damping.
- `K_iθ` is small and only removes residual offset from cable drag or imbalance.

---

## 4. Inner Loop: Rate Controller

$$
e_\omega[k] = \omega_{ref}[k] - \omega_f[k]
$$

### 4.1 Proportional

$$
P[k] = K_p\,e_\omega[k]
$$

### 4.2 Derivative: on measurement, filtered

Take the derivative of the measurement, not the error. This avoids a "derivative kick" on setpoint steps. A first-order filter with time constant T_f limits high-frequency gain:

$$
D(s) = -\frac{K_d\,s}{1 + T_f s}\,\omega(s)
$$

Backward-Euler discretization (unconditionally stable for any T_f):

$$
\boxed{D[k] = \frac{T_f}{T_f + T_s}\,D[k-1] \;-\; \frac{K_d}{T_f + T_s}\,\big(\omega_f[k] - \omega_f[k-1]\big)}
$$

Choosing T_f, either:

- from a cutoff frequency: T_f = 1/(2π f_d), with f_d ≈ 80–100 Hz *(assumed)*, or
- from the classic N-form: T_f = K_d / (K_p·N), with N = 5–20.

Keep f_d below Nyquist/4 (≤ 250 Hz at 2 kHz) and above roughly 2× the loop bandwidth.

Optional Tustin form, which has a better phase match near the cutoff:

$$
D[k] = \frac{2T_f - T_s}{2T_f + T_s}D[k-1] - \frac{2K_d}{2T_f + T_s}\big(\omega_f[k]-\omega_f[k-1]\big)
$$

### 4.3 Integral and Anti-Windup Clamping

First compute the unsaturated output and saturate it:

$$
u_{unsat}[k] = P[k] + I[k-1] + D[k]
$$

$$
u[k] = \text{sat}(u_{unsat}[k], u_{min}, u_{max})
$$

**Method A: conditional integration (clamping).** This is the recommended primary method.

$$
\text{saturated} = (u_{unsat} \neq u)
$$

$$
\text{windup} = \text{saturated} \;\land\; \big(\text{sign}(e_\omega) = \text{sign}(u_{unsat})\big)
$$

$$
I[k] =
\begin{cases}
I[k-1] & \text{if windup} \\
I[k-1] + K_i\,T_s\,e_\omega[k] & \text{otherwise}
\end{cases}
$$

Then apply a hard integrator bound as a safety net:

$$
I[k] = \text{sat}\big(I[k],\ -I_{lim},\ +I_{lim}\big), \quad I_{lim} \approx 0.3\text{–}0.5\,u_{max}
$$

Optionally, use a dynamic bound so that I alone can never push the output past saturation:

$$
I_{max} = \max(u_{max} - P - D,\ 0), \qquad I_{min} = \min(u_{min} - P - D,\ 0)
$$

**Method B: back-calculation.** This alternative unwinds smoothly.

$$
I[k] = I[k-1] + T_s\left(K_i\,e_\omega[k] + \frac{u[k] - u_{unsat}[k]}{T_t}\right), \quad T_t = \sqrt{T_i T_d}\ \text{or}\ T_i
$$

where T_i = K_p/K_i and T_d = K_d/K_p.

### 4.4 Cascade Anti-Windup

If the inner loop saturates, the outer integrator must also stop. Otherwise the angle loop keeps demanding more rate that cannot be delivered.

$$
\text{freeze } I_\theta \text{ if } \big(\text{inner saturated} \lor \omega_{ref}\ \text{clamped}\big) \land \text{sign}(e_\theta) = \text{sign}(\omega_{ref,unsat})
$$

---

## 5. Implementation (ESP-IDF, C)

```c
typedef struct {
    float kp, ki, kd;
    float Ts, Tf;
    float out_min, out_max;
    float i_lim;
    float integ, d_state, meas_prev;
    bool  saturated;
} pid_t;

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* inhibit_integ: external freeze request (cascade anti-windup) */
float pid_update(pid_t *c, float sp, float meas, float