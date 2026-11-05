# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have your project's "Cycle 8" specifications, so this design uses typical brushless gimbal assumptions, listed below. Substitute your own values where they differ. The structure and equations apply regardless.

## 0. Assumptions

| Item | Assumed value |
|---|---|
| Actuator | BLDC gimbal motor (e.g., GBM2804), FOC driver, torque/voltage-q command |
| Sensor | 6-axis IMU (ICM-42688 / MPU-6500) over SPI, gyro ODR ≥ 4 kHz |
| Attitude estimate | Complementary or Mahony filter → roll angle φ̂ |
| Inner (rate) loop | 2 kHz, Ts_r = 0.5 ms |
| Outer (angle) loop | 500 Hz, Ts_a = 2 ms (runs every 4th rate tick) |
| Compute | Single-precision float (the S3 has an FPU), task pinned to Core 1 |
| Output limit | u_max = ±1.0 (normalized U_q) |
| Rate limit | ω_max = ±300 °/s |

## 1. Loop Architecture

```
          ┌──────────── Angle loop (500 Hz) ───────────┐   ┌──────── Rate loop (2 kHz) ────────┐
φ_ref ──►(Σ)──► P (+ small I) ──► clamp ±ω_max ──►(Σ)──► PID ──► clamp ±u_max ──► FOC Uq ──► Motor
          ▲ −                                   ω_ref    ▲ −                                      │
          │                                              │                                       │
       φ̂ (attitude filter) ◄──────────── IMU ◄──────────┴── ω_gyro (LPF) ◄─────────────────────┘
```

**Bandwidth separation.** The inner rate loop must be 5–10× faster than the outer angle loop.
- Target rate-loop crossover: about 40–60 Hz.
- Target angle-loop crossover: about 6–10 Hz.

**Why derivative lives in the inner loop only.** The rate loop already provides damping (it acts as the "D" of the angle). The outer loop is therefore P or PI, with no D term.

## 2. Inner Rate Loop: Discrete PID

### 2.1 Error

$$e_\omega[k] = \omega_{ref}[k] - \omega_{gyro}[k]$$

### 2.2 Proportional

$$P[k] = K_{p,\omega}\, e_\omega[k]$$

Optionally use setpoint weighting b ∈ [0.5, 1] to soften steps:

$$P[k] = K_{p,\omega}\,(b\,\omega_{ref}[k] - \omega_{gyro}[k])$$

### 2.3 Derivative: on measurement, first-order filtered

Differentiating the error causes "derivative kick" on setpoint steps, so differentiate the measurement instead. A pure derivative of gyro data is angular acceleration, which is very noisy, so it must be band-limited.

Continuous form, with τ_d = T_d / N (N ≈ 5–20):

$$D(s) = -\frac{K_d\, s}{\tau_d s + 1}\,\omega_{gyro}(s)$$

**Discretization (backward Euler).** This form is unconditionally stable for any τ_d:

$$\alpha = \frac{\tau_d}{\tau_d + T_s}$$

$$D[k] = \alpha\, D[k-1] \;-\; \frac{K_d\,(1-\alpha)}{T_s}\,\big(\omega_{gyro}[k] - \omega_{gyro}[k-1]\big)$$

**Tustin alternative** (better phase near Nyquist):

$$D[k] = \frac{2\tau_d - T_s}{2\tau_d + T_s}\,D[k-1] \;-\; \frac{2K_d}{2\tau_d + T_s}\,\big(\omega[k] - \omega[k-1]\big)$$

**Choosing the cutoff.** Use $f_c = 1/(2\pi\tau_d) \approx 80\text{–}150$ Hz.
- It must sit well below Nyquist (1 kHz) and below the first mechanical resonance of the gimbal arm.
- It must sit above the rate-loop crossover (~50 Hz), or you lose the phase lead D is meant to provide.
- Example: f_c = 100 Hz gives τ_d = 1.59 ms, and α = 1.59/(1.59+0.5) = 0.761.

**Additional filtering.** Apply a 2nd-order Butterworth LPF to the gyro feeding P and D, at about 150–250 Hz. Add a notch filter if a structural resonance shows up in an FFT of the logged gyro data.

### 2.4 Integral with clamping anti-windup

Unsaturated output, using the I value from the previous step:

$$v[k] = P[k] + I[k-1] + D[k] + u_{ff}[k]$$

Saturated output:

$$u[k] = \text{sat}(v[k]) = \min(\max(v[k], -u_{max}), u_{max})$$

**Conditional integration (clamping).** Freeze the integrator when the output is saturated *and* the error would push further into saturation:

$$
\text{freeze} =
\big(v[k] \neq u[k]\big) \;\wedge\; \big(\text{sign}(e_\omega[k]) = \text{sign}(v[k])\big)
$$

$$
I[k] =
\begin{cases}
I[k-1] & \text{if freeze} \\
I[k-1] + K_{i,\omega}\, T_s\, e_\omega[k] & \text{otherwise}
\end{cases}
$$

Plus a hard integrator bound as a backstop:

$$I[k] = \text{clamp}(I[k],\; -I_{max},\; I_{max}), \qquad I_{max} \approx 0.3\text{–}0.5\,u_{max}$$

This bound is sized to cover cable drag, cogging, and imbalance torque, but not to command full torque.

**Optional back-calculation variant.** This gives smoother recovery from saturation:

$$I[k] = I[k-1] + K_i T_s\, e[k] + \frac{T_s}{T_t}\big(u[k] - v[k]\big), \qquad T_t \approx \sqrt{T_i T_d}$$

(or T_t ≈ T_i for PI).

**Pick one scheme, not both.** Clamping is simpler and deterministic; back-calculation tends to recover better after large disturbances. Using both doubles the anti-windup effect and makes tuning confusing.

### 2.5 Feedforward (optional, recommended)

$$u_{ff}[k] = K_{ff}\,\omega_{ref}[k] + K_{aff}\,\dot\omega_{ref}[k]$$

- K_ff compensates back-EMF and viscous friction in voltage mode.
- K_aff compensates inertia.

Both reduce how hard the PID has to work during pans.

## 3. Outer Angle Loop

### 3.1 Wrapped error

Roll is usually within ±90°, but wrap anyway to stay robust:

$$e_\phi = \text{wrap}_{\pm\pi}(\phi_{ref} - \hat\phi)$$

### 3.2 Control law (PI)

$$v_\phi[k] = K_{p,\phi}\, e_\phi[k] + I_\phi[k-1]$$

$$\omega_{ref}[k] = \text{clamp}(v_\phi[k],\; -\omega_{max},\; \omega_{max})$$

### 3.3 Cascade-aware anti-windup

The outer integrator must freeze in two cases:
1. Its own output (ω_ref) is clamped, or
2. The inner loop is saturated, which means the commanded rate is not being achieved.

$$
\text{freeze}_\phi =
\Big[(v_\phi \neq \omega_{ref}) \wedge \text{sign}(e_\phi)=\text{sign}(v_\phi)\Big]
\;\vee\;
\Big[\text{inner\_sat} \wedge \text{sign}(e_\phi)=\text{sign}(u)\Big]
$$

$$
I_\phi[k] =
\begin{cases}
I_\phi[k-1] & \text{freeze}_\phi\\
\text{clamp}\big(I_\phi[k-1] + K_{i,\phi}T_{s,a}\,e_\phi[k],\; \pm I_{\phi,max}\big) & \text{otherwise}
\end{cases}
$$

Outer K_i is often unnecessary because the inner I already rejects constant torque disturbances. A small value can still help remove residual attitude-estimator bias.

## 4. Implementation (ESP-IDF, C)

```c
#include <math.h>
#include <stdbool.h>

typedef struct {
    float kp, ki, kd, kff;
    float b;            // setpoint weight
    float alpha;        // derivative LPF coeff
    float ts;
    float out_max, i_max;
    float i_term, d_term, meas_prev;
    bool  saturated;
    float last_out;
} pid_t;

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

void pid_init(pid_t *p, float kp, float ki, float kd, float ts,
              float fc_d, float out_max, float i_max) {
    *p = (pid_t){0};
    p->kp = kp; p->ki = ki; p->kd = kd; p->ts = ts; p->b = 1.0f;
    float tau = 1.0f / (2.0