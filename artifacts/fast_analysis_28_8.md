# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

> **Note:** I don't have your earlier project notes, so "Cycle 28" parameters such as motor, IMU, and prior gains aren't known to me. The design below uses stated assumptions that you can swap for your values. Every equation is independent of those parameters.

---

## 1. Assumptions and System Architecture

| Item | Assumed value |
|---|---|
| MCU | ESP32-S3 @ 240 MHz, single-precision FPU (use `float`, not `double`) |
| IMU | ICM-42688-P over SPI, gyro ODR 4 kHz, on-chip anti-alias filter ~250 Hz |
| Actuator | Gimbal BLDC (e.g., 2804, 7 pole pairs) driven by FOC voltage/torque mode |
| Inner loop (rate) | **2 kHz** (Ts = 500 µs) |
| Outer loop (angle) | **500 Hz** (Ts = 2 ms), executed every 4th inner tick |
| Attitude estimate | Mahony/complementary filter at 2 kHz |

```
 θ_ref ──►(+)──► [Angle P(I)] ──ω_ref──►(+)──► [Rate PID] ──u──► [FOC/Motor] ──► Roll dynamics
          ▲ -        (500 Hz)    clamp   ▲ -     (2 kHz)    sat                     │
          │                              │                                          │
          └──────── θ (attitude filter) ◄┴───────── ω (gyro, LPF) ◄─────────────────┘
```

**Bandwidth separation rule:** inner closed-loop bandwidth should be ≥ 5–10× outer.
- Target rate-loop bandwidth: ~40–60 Hz
- Target angle-loop bandwidth: ~5–10 Hz

---

## 2. Inner Loop: Rate (Velocity) PID

### 2.1 Error and Proportional Term
$$e_\omega[k] = \omega_{ref}[k] - \omega[k]$$
$$P[k] = K_p \, e_\omega[k]$$

### 2.2 Derivative on Measurement, with Low-Pass Filter

Differentiate the **measurement**, not the error. This avoids derivative kick when the outer loop steps ω_ref.

Continuous form: $D(s) = -K_d \dfrac{s}{\tau s + 1}\,\omega(s)$, with $\tau = \dfrac{1}{2\pi f_c}$.

**Tustin (bilinear) discretization** (recommended):
$$D[k] = \frac{2\tau - T_s}{2\tau + T_s}\,D[k-1] \;-\; \frac{2K_d}{2\tau + T_s}\,\big(\omega[k] - \omega[k-1]\big)$$

**Backward-Euler alternative** (simpler, slightly more phase lag):
$$\alpha = \frac{\tau}{\tau + T_s}, \qquad D[k] = \alpha\,D[k-1] - K_d\,(1-\alpha)\,\frac{\omega[k]-\omega[k-1]}{T_s}$$

**Choosing f_c:**
- Use 3–5× the target loop bandwidth, and keep it below the first structural/motor resonance. Typically **f_c ≈ 100–150 Hz** at 2 kHz.
- Keep f_c ≤ Fs/10 (200 Hz here) so the discrete filter behaves like its analog prototype.
- Also run the gyro itself through a 2nd-order Butterworth LPF (~200 Hz). If a frame resonance shows up in the FFT, add a notch filter.

### 2.3 Integral Term with Anti-Windup Clamping

First compute the tentative (unsaturated) output using the *previous* integrator:
$$u_{unsat}[k] = P[k] + I[k-1] + D[k] + K_{ff}\,\omega_{ref}[k]$$
$$u[k] = \text{sat}(u_{unsat}[k],\; u_{min},\; u_{max})$$

**Conditional integration (clamping).** Freeze the integrator when the output is saturated *and* the error would drive it further into saturation:
$$
\text{clamp}[k] =
\begin{cases}
1 & \text{if } u_{unsat}[k] \neq u[k] \;\land\; \text{sign}(e_\omega[k]) = \text{sign}(u_{unsat}[k]) \\
0 & \text{otherwise}
\end{cases}
$$

$$
I[k] =
\begin{cases}
I[k-1] & \text{clamp}[k] = 1 \\
I[k-1] + K_i\,\dfrac{T_s}{2}\big(e_\omega[k] + e_\omega[k-1]\big) & \text{clamp}[k] = 0
\end{cases}
$$
(The integral update uses the trapezoidal rule.)

**Hard integrator bound** (a second layer of protection):
$$I[k] = \text{sat}(I[k],\; -I_{max},\; I_{max}), \qquad I_{max} \approx 0.3\text{–}0.5\,u_{max}$$

The bound only needs to cover the static torque the integrator must hold (cable drag, imbalance). Keep it small.

**Optional back-calculation alternative.** This gives smoother recovery than hard clamping:
$$I[k] = I[k-1] + K_i T_s\, e_\omega[k] + \frac{T_s}{T_t}\big(u[k] - u_{unsat}[k]\big), \quad T_t = \sqrt{T_i T_d} = \sqrt{\tfrac{K_p}{K_i}\cdot\tfrac{K_d}{K_p}}$$

### 2.4 Output
`u[k]` is the FOC voltage (or q-axis current) command, saturated to the driver limit (e.g., ±0.8·V_bus/2 for voltage-mode FOC).

---

## 3. Outer Loop: Angle Controller

Gimbal roll typically needs **P plus a small I** (for offset from cable stiffness). A D term is unnecessary because the inner rate loop already provides damping.

### 3.1 Wrapped Error
$$e_\theta[k] = \text{wrap}_{\pm\pi}\big(\theta_{ref}[k] - \theta[k]\big)$$

### 3.2 Rate Command with Clamping
$$\omega_{ref,unsat}[k] = K_{p\theta}\, e_\theta[k] + I_\theta[k-1] + \dot\theta_{ref}[k]$$
$$\omega_{ref}[k] = \text{sat}(\omega_{ref,unsat}[k],\; \pm\omega_{max})$$

The $\dot\theta_{ref}$ term is a feedforward for smooth follow-mode pans. A typical limit is $\omega_{max} \approx 3$ rad/s for roll.

### 3.3 Cascade-Aware Anti-Windup
The outer integrator must also freeze when the **inner** loop saturates. Otherwise it winds up while the motor is torque-limited.
$$
I_\theta[k] =
\begin{cases}
I_\theta[k-1] & \text{if } (\text{outer sat} \lor \text{inner clamp flag}) \land \text{sign}(e_\theta) = \text{sign}(\omega_{ref,unsat}) \\
\text{sat}\big(I_\theta[k-1] + K_{i\theta} T_{s,o}\, e_\theta[k],\; \pm I_{\theta,max}\big) & \text{otherwise}
\end{cases}
$$

---

## 4. Implementation (ESP-IDF, C)

```c
// roll_pid.h
#include <math.h>
#include <stdbool.h>

typedef struct {
    float kp, ki, kd, kff;
    float ts, tau;            // sample time, derivative filter time constant
    float out_min, out_max;
    float i_max;
    // state
    float integ, d_state, prev_meas, prev_err;
    float d_a, d_b;           // precomputed Tustin coefficients
    bool  clamped;
} rate_pid_t;

static inline float sat(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

void rate_pid_init(rate_pid_t *c, float fc_hz) {
    c->tau  = 1.0f / (2.0f * (float)M_PI * fc_hz);
    c->d_a  = (2.0f * c->tau - c->ts) / (2.0f * c->tau + c->ts);
    c->d_b  = (2.0f * c->kd)          / (2.0f * c->tau + c->ts);
    c->integ = c->d_state = c->prev_meas = c->prev_err = 0.0f;
    c->clamped = false;
}

float IRAM_ATTR rate_pid_update(rate_pid_t *c, float ref, float meas) {
    float e = ref - meas;
    float p = c->kp * e;

    // Derivative on measurement, Tustin-filtered
    c->d_state = c->d_a * c->d_state - c->d_b * (meas - c->prev_meas);
    c->prev_meas = meas;

    float u_unsat = p + c->integ + c->d_state + c->kff * ref;
    