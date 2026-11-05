# Cascaded PID Design: ESP32-S3 Gimbal, Roll Axis

I don't have the details of your "Cycle 22" spec, so this design states its assumptions. Where your hardware or requirements differ, swap in your own numbers. The structure and equations still apply.

## 0. Assumptions

| Item | Assumed value |
|---|---|
| Actuator | BLDC gimbal motor (e.g. 2804, 7 pole pairs), driven by FOC voltage mode (U_q command) |
| Supply / limit | 12 V bus, U_q clamp = ±6 V |
| Rate sensor | IMU gyro X-axis (ICM-42688 / MPU-6500 class), ODR ≥ 1 kHz |
| Angle sensor | Fused roll estimate (Mahony/complementary filter), optionally an AS5600 for motor electrical angle |
| Inner (rate) loop | 1 kHz, Ts_i = 1 ms |
| Outer (angle) loop | 500 Hz, Ts_o = 2 ms (runs every 2nd inner tick) |
| Numeric type | `float` (S3 has a single-precision FPU; avoid `double`) |

## 1. Architecture

```
θ_ref ─►(+)─► [Angle PI(D)] ─► ω_ref ─►(+)─► [Rate PID] ─► U_q ─► FOC ─► Motor ─► Roll
         ▲−      500 Hz        + ω_ff     ▲−     1 kHz      ±6 V                   │
         │                                │                                        │
         └──── θ_roll (AHRS) ◄────────────┴──── ω_gyro (LPF + notch) ◄─────────────┘
```

**Design rules:**
- The inner loop's bandwidth should be at least 4–5× the outer loop's. Example targets:
  - Rate loop: ~40–60 Hz closed-loop bandwidth.
  - Angle loop: ~8–12 Hz.
- The **outer loop is mostly P (+ small I)**. The inner rate loop already provides damping, so outer-loop D is usually unnecessary.
- The **inner loop is the full PID**. Its D-term acts on gyro rate, i.e. angular acceleration, so it is noise-sensitive and must be filtered.

## 2. Discrete PID Formulation

Use a parallel form with three modifications:
- derivative on measurement (no derivative kick on setpoint steps),
- a filtered derivative,
- an integrator with anti-windup.

**Error (angle loop needs wrapping):**

$$
e[k] = \mathrm{wrap}_{\pm\pi}\big(r[k] - y[k]\big)
$$

**Proportional term (optional setpoint weight b ≈ 0.7–1.0):**

$$
P[k] = K_p\big(b\,r[k] - y[k]\big)
$$

### 2.1 Derivative Filtering

Ideal derivative on measurement:

$$
D(s) = -K_d\,\frac{s}{\tau s + 1}\,Y(s), \qquad \tau = \frac{1}{2\pi f_c}
$$

**Option A: Backward Euler (simple and robust)**

$$
\alpha = \frac{\tau}{\tau + T_s}
$$

$$
D[k] = \alpha\,D[k-1] \;-\; K_d\,(1-\alpha)\,\frac{y[k]-y[k-1]}{T_s}
$$

**Option B: Tustin (better phase near f_c, recommended for the inner loop)**

$$
D[k] = \frac{2\tau - T_s}{2\tau + T_s}\,D[k-1] \;-\; \frac{2K_d}{2\tau + T_s}\,\big(y[k]-y[k-1]\big)
$$

**Cutoff selection:**
- Choose f_c ≈ 5–10× the target loop bandwidth.
- Keep f_c below ~f_s/5.
- Rate loop: f_c ≈ 80–150 Hz (at 1 kHz).
- Angle loop, if D is used: f_c ≈ 30–50 Hz.
- Equivalent rule: N = K_d/(K_p·τ) ≈ 8–20.

**Upstream gyro conditioning (it matters as much as the D filter):**
- 2nd-order Butterworth LPF on the gyro at ~150–200 Hz.
- Optionally a notch at the frame/arm mechanical resonance. Find it by sweeping or from an FFT of the gyro while the axis is energized.

Biquad coefficients for a Butterworth LPF (pre-warped):

$$
K=\tan\!\left(\frac{\pi f_c}{f_s}\right),\quad
\text{norm}=\frac{1}{1+\sqrt2 K + K^2}
$$

$$
b_0 = K^2\,\text{norm},\quad b_1 = 2b_0,\quad b_2=b_0
$$

$$
a_1 = 2(K^2-1)\,\text{norm},\quad a_2 = (1-\sqrt2 K+K^2)\,\text{norm}
$$

### 2.2 Anti-Windup Clamping

Compute the unsaturated output, then saturate it:

$$
v[k] = P[k] + I[k] + D[k] + u_{ff}[k]
$$

$$
u[k] = \mathrm{sat}(v[k]) = \min\big(\max(v[k],\,u_{min}),\,u_{max}\big)
$$

**Method 1: Conditional integration (clamping).** Freeze the integrator when the output is saturated *and* the error would drive it deeper into saturation:

$$
\text{clamp} = \big(v[k] \neq u[k]\big) \;\wedge\; \big(\mathrm{sign}(e[k]) = \mathrm{sign}(v[k])\big)
$$

$$
I[k+1] =
\begin{cases}
I[k] & \text{if clamp}\\[2pt]
I[k] + K_i\,T_s\,\tfrac{e[k]+e[k-1]}{2} & \text{otherwise (trapezoidal)}
\end{cases}
$$

Then apply a hard bound on the integrator state itself:

$$
I[k+1] = \mathrm{sat}_{[I_{min},\,I_{max}]}\big(I[k+1]\big)
$$

Typically |I_max| ≈ 50–70 % of u_max. On a gimbal, the integrator mainly cancels static imbalance and cable torque, so it shouldn't need full authority.

**Method 2: Back-calculation (smoother recovery, optional).**

$$
I[k+1] = I[k] + K_i T_s\,e[k] + \frac{T_s}{T_t}\big(u[k]-v[k]\big),
\qquad T_t \approx \sqrt{T_i T_d}
$$

where T_i = K_p/K_i and T_d = K_d/K_p. Use T_t ≈ T_i/2 if there is no D-term.

**Cascade-specific windup.** When the inner loop saturates (U_q at ±6 V), the outer loop also has to stop integrating. Otherwise it keeps demanding rate that can't be delivered. Pass a saturation flag outward:

$$
\text{outer\_clamp} = \text{inner\_saturated} \;\wedge\; \mathrm{sign}(e_\theta) = \mathrm{sign}(\omega_{ref})
$$

Also clamp the outer output to |ω_ref| ≤ ω_max (e.g. 6 rad/s ≈ 340 °/s). This bounds slew speed.

## 3. Starting Gains (Roll, Assumed Hardware)

| Loop | Kp | Ki | Kd | D filter f_c | Output limit | I limit |
|---|---|---|---|---|---|---|
| Rate (ω → U_q) | 0.08 V/(rad/s) | 1.5 V/rad | 0.0008 V·s²/rad | 120 Hz (Tustin) | ±6 V | ±3.5 V |
| Angle (θ → ω_ref) | 8.0 (rad/s)/rad | 2.0 (rad/s)/(rad·s) | 0 (optional 0.05) | 40 Hz | ±6 rad/s | ±2 rad/s |

These values are deliberately conservative. Tune them on hardware using Section 6.

## 4. Implementation (ESP-IDF, C)

```c
// pid.h
typedef struct {
    float kp, ki, kd, b;          // gains, setpoint weight
    float ts;                     // sample time [s]
    float tau;                    // derivative filter time constant
    float out_min, out_max;       // output saturation
    float i_min, i_max;           // integ