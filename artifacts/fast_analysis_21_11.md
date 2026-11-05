# Cascaded PID Design: ESP32-S3 Gimbal, Roll Axis

> **Note on "Cycle 21":** I don't have your project's Cycle 21 specs, so I've used typical BLDC-gimbal assumptions, listed below. Replace them with your values. The structure and equations carry over unchanged.

## 0. Assumptions

| Item | Assumed value |
|---|---|
| Actuator | Gimbal BLDC (high-pole, ~10 Ω), FOC/SVPWM, command = Uq (normalized −1…+1) |
| Sensors | IMU gyro over SPI (ICM-42688-class) + angle from complementary/Mahony filter; optional AS5600/AS5048 for commutation |
| Inner (rate) loop | 2 kHz (Ts_r = 500 µs) |
| Outer (angle) loop | 500 Hz (Ts_a = 2 ms), i.e. every 4th rate tick |
| Target bandwidth | Rate loop ≈ 40 Hz, angle loop ≈ 6–8 Hz (≥5× separation) |
| Math | Single-precision float (S3 has a hardware FPU; avoid `double`) |

---

## 1. Architecture

```
θ_ref ─►(+)─► Angle PI ─► ω_ref ─(+ FF θ̇_ref)─► sat(±ω_max) ─►(+)─► Rate PID ─► Uq ─► sat ─► FOC ─► Motor
        ▲−                                                     ▲−
        └──────── θ_roll (attitude filter) ◄── IMU ──► ω_gyro ─┘ (LPF + notch)
```

- **Inner loop (rate):** fast. It rejects disturbance torques such as cable drag, friction and base vibration. It uses the gyro directly, with no estimator lag.
- **Outer loop (angle):** slow. It produces a rate setpoint. It is mostly P, with a small I term to remove static offset from imbalance.

---

## 2. Signal Conditioning (before the PIDs)

**Gyro low-pass** (first-order IIR, f_c ≈ 150–250 Hz at 2 kHz):

$$\alpha_g = \frac{T_s}{T_s + \frac{1}{2\pi f_c}}, \qquad \omega_f[k] = \omega_f[k-1] + \alpha_g(\omega[k] - \omega_f[k-1])$$

**Optional notch** (biquad) at the measured structural or motor-cogging resonance. Identify it with an FFT of gyro data under a step input.

**Angle error wrap** for roll, to avoid ±180° discontinuity:

$$e_\theta = \operatorname{wrap}_{[-\pi,\pi)}(\theta_{ref} - \theta)$$

---

## 3. Inner Rate Loop: PID

### 3.1 Error and proportional term

$$e_\omega[k] = \omega_{ref}[k] - \omega_f[k], \qquad P[k] = K_{p,\omega}\, e_\omega[k]$$

### 3.2 Derivative: on measurement, low-pass filtered

Take the derivative of the **measurement** (−ω_f), not the error. This avoids derivative kick on setpoint steps from the outer loop.

The continuous form is:

$$D(s) = -\frac{K_d\, s}{\tau_d s + 1}\,\omega_f(s), \qquad \tau_d = \frac{1}{2\pi f_d}$$

**Tustin (bilinear) discretization** is recommended, because it preserves phase better near f_d:

$$D[k] = \frac{2\tau_d - T_s}{2\tau_d + T_s}\, D[k-1] \;-\; \frac{2K_d}{2\tau_d + T_s}\,\big(\omega_f[k] - \omega_f[k-1]\big)$$

The **backward Euler** form is simpler and always stable:

$$D[k] = \frac{\tau_d}{\tau_d + T_s}\, D[k-1] \;-\; \frac{K_d}{\tau_d + T_s}\,\big(\omega_f[k] - \omega_f[k-1]\big)$$

Choosing the filter cutoff:
- Pick f_d ≈ 5–10× the rate-loop crossover. For a 40 Hz crossover, use f_d ≈ 80–120 Hz. Keep it well below Nyquist (1 kHz).
- The derivative of gyro rate is angular acceleration, which is noisy. Keep K_d small. Many gimbals run the rate loop as PI and get damping from the motor's back-EMF.
- Equivalently, the filter limits high-frequency derivative gain to K_d/τ_d. Check that this times the gyro noise RMS stays below about 2–5 % of Uq_max.

### 3.3 Integral with clamping anti-windup

Compute the unsaturated and saturated outputs:

$$u_{us}[k] = P[k] + I[k-1] + D[k] + u_{ff}[k], \qquad u[k] = \operatorname{sat}(u_{us}[k],\, u_{min},\, u_{max})$$

**Conditional integration (clamping).** Freeze the integrator when the output is saturated *and* the error would push further into saturation:

$$
\text{clamp} =
\big(u_{us} \neq u\big) \;\wedge\; \big(\operatorname{sign}(e_\omega) = \operatorname{sign}(u_{us})\big)
$$

$$
I[k] =
\begin{cases}
I[k-1] & \text{if clamp} \\[4pt]
I[k-1] + K_{i,\omega}\, \dfrac{T_s}{2}\big(e_\omega[k] + e_\omega[k-1]\big) & \text{otherwise}
\end{cases}
$$

Then apply a **hard bound on the integrator state**, as a safety net against sensor faults:

$$I[k] = \operatorname{sat}\big(I[k],\; -I_{max},\; I_{max}\big), \qquad I_{max} \approx 0.6\,u_{max}$$

**Alternative: back-calculation.** This gives smoother recovery than clamping:

$$I[k] = I[k-1] + K_i T_s\, e[k] + \frac{T_s}{T_t}\big(u[k] - u_{us}[k]\big), \qquad T_t \approx \sqrt{T_i T_d}\ \text{(or } T_t \approx T_i/2 \text{ for PI)}$$

Finally, recompute u[k] using the updated I[k]. You can either use I[k] in the current output, or use I[k−1] as above for a one-step-delayed integral. Both work; be consistent.

### 3.4 Output

- u[k] is the normalized Uq, which you send to FOC.
- Add a slew-rate limit on u (e.g. ≤ 0.02 per tick) during startup only.

---

## 4. Outer Angle Loop: PI → Rate Setpoint

The angle loop runs every N = 4 rate ticks.

$$e_\theta[k] = \operatorname{wrap}(\theta_{ref} - \theta)$$

$$\omega_{ref,us} = K_{p,\theta}\, e_\theta + I_\theta + K_{ff}\,\dot\theta_{ref}$$

$$\omega_{ref} = \operatorname{sat}(\omega_{ref,us},\, -\omega_{max},\, \omega_{max})$$

The integrator uses the same clamping rule as the rate loop, with the condition built from (ω_ref,us, ω_ref, e_θ):

$$I_\theta[k] = I_\theta[k-1] + K_{i,\theta} T_{s,a}\, e_\theta \quad \text{unless clamped}$$

**Cascade-specific anti-windup.** Also freeze I_θ whenever the **inner** loop is saturated. Otherwise the outer integrator winds up against an actuator it cannot see. The rate loop exports a `saturated` flag for this.

**Gains:**
- K_p,θ (in s⁻¹) is roughly the outer-loop bandwidth in rad/s, e.g. 2π·6 ≈ 38 s⁻¹.
- Usually no D term is needed on the angle loop, because the rate loop provides the damping.

---

## 5. Reference Implementation (ESP-IDF, C)

```c
typedef struct {
    float kp, ki, kd;
    float tau_d;          // derivative filter time constant [s]
    float ts;             // sample time [s]
    float out_min, out_max;
    float i_max;
    // state
    float integ, d_state, prev_meas, prev_err;
    bool  saturated;
} pid_t;

static inline float cl