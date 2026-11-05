# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have the specific "Cycle 18" documents (motor datasheet, inertia measurements, prior tuning logs), so the numbers below come from a stated example plant. Substitute your measured J, K_t, and sensor rates where marked.

---

## 1. Architecture

```
            Outer loop (500 Hz)                        Inner loop (2 kHz)
θ_ref ─►(+)─► wrap ─► P / PI_angle ─►(+)─► clamp ─►(+)─► PID_rate ─► sat ─► i_q,ref ─► FOC ─► BLDC
         ▲−                          ▲ω_ff  ω_ref   ▲−                                         │
         │                                          │                                          │
     θ_meas (AHRS / encoder)                   ω_meas (gyro X, filtered)  ◄────────────────────┘
```

| Item | Choice | Rationale |
|---|---|---|
| Inner loop | PID on angular **rate**, output = q‑axis current (torque) | Gyro gives clean, direct rate. A torque output makes the plant ≈ 1/(Js). |
| Outer loop | P (optional small I) on **angle** | The inner loop already provides rate damping, so no D is needed outside. |
| Rates | Inner 2 kHz (Ts = 0.5 ms), outer 500 Hz | Rates are an integer ratio. Keep bandwidth separation of at least 5×. |
| Feedback sources | ω from gyro (ICM‑42688/BMI270 at ≥2 kHz ODR), θ from complementary/Mahony filter or magnetic encoder (AS5600/MT6701) | |
| Execution | GPTimer ISR → notify a task pinned to **Core 1**, priority `configMAX_PRIORITIES-1` | Keeps the loop away from WiFi/BT on Core 0. Use `float` only, because the S3 FPU is single precision. |

---

## 2. Plant Model and Starting Gains (Example Values)

Plant from current to rate:

$$
\frac{\omega(s)}{i_q(s)} = \frac{K_t}{J s + b} \approx \frac{K_t}{J s}
$$

Example values:
- J = 2.0×10⁻⁴ kg·m² (roll arm plus camera, measured about the roll pivot)
- K_t = 0.05 N·m/A

### Inner Rate Loop
Target crossover f_c,in ≈ 60 Hz, so ω_c = 377 rad/s.

| Gain | Formula | Value |
|---|---|---|
| Kp_ω | J·ω_c / K_t | **1.5 A/(rad/s)** |
| Ki_ω | Kp_ω·ω_c / 10 | **57 A/rad** |
| Kd_ω | small, J‑lead only | **0.001 A·s²/rad**, start at 0 |

### Outer Angle Loop
Target f_c,out ≈ 8 Hz, so ω_c ≈ 50 rad/s.

| Gain | Value |
|---|---|
| Kp_θ | ≈ **50 s⁻¹** (rad/s of rate command per rad of error) |
| Ki_θ | 0 initially. Add about 0.5–2 s⁻² only if a steady-state offset persists (cable drag, imbalance). |

**Limits:**
- |ω_ref| ≤ 6 rad/s
- |i_q| ≤ I_max (driver/motor rating, e.g. 1.2 A)
- Integrator limit: 60 % of I_max

---

## 3. Discrete PID Equations

Error definition:

$$
e_k = r_k - y_k
$$

### 3.1 Proportional

$$
P_k = K_p\, e_k
$$

### 3.2 Derivative: On Measurement, First-Order Filtered

Taking the derivative of the measurement −y instead of the error eliminates the "derivative kick" on setpoint steps.

The continuous filtered derivative is:

$$
D(s) = -\frac{K_d\, s}{\tau s + 1}\, Y(s), \qquad \tau = \frac{K_d}{N K_p}, \quad N \in [5, 20]
$$

Alternatively, set the cutoff directly: τ = 1/(2π f_D), with f_D ≈ 5–8 × f_c, but below Nyquist/2.

**Tustin (bilinear) discretization**, recommended:

$$
D_k = \alpha\, D_{k-1} \;-\; \beta\,(y_k - y_{k-1})
$$

$$
\alpha = \frac{2\tau - T_s}{2\tau + T_s}, \qquad \beta = \frac{2K_d}{2\tau + T_s}
$$

**Backward Euler**, an alternative that is always stable even when τ < Ts/2:

$$
D_k = \frac{\tau}{\tau + T_s} D_{k-1} - \frac{K_d}{\tau + T_s}(y_k - y_{k-1})
$$

Notes for the rate loop:
- The derivative of a gyro signal is angular acceleration, which is noisy. Apply a 2nd‑order Butterworth LPF to the gyro first (≈ 150–250 Hz).
- If frame resonance shows up in an FFT of the log, add a notch at that frequency.
- Keep f_D ≈ 300 Hz at Ts = 0.5 ms.

### 3.3 Integral with Anti-Windup

Use trapezoidal integration:

$$
\Delta I_k = K_i \frac{T_s}{2}(e_k + e_{k-1})
$$

Build the unsaturated output, including feedforward, then saturate:

$$
u^{*}_k = P_k + I_{k-1} + D_k + u_{ff,k}
$$

$$
u_k = \operatorname{sat}(u^{*}_k) = \min\!\big(\max(u^{*}_k,\, u_{min}),\, u_{max}\big)
$$

**Method A: conditional clamping (primary).** Freeze the integrator only when it would push the output further into saturation:

$$
I_k =
\begin{cases}
I_{k-1} & \text{if } (u^*_k > u_{max} \wedge e_k > 0) \;\lor\; (u^*_k < u_{min} \wedge e_k < 0) \\[4pt]
\operatorname{clamp}\!\big(I_{k-1} + \Delta I_k,\; I_{min},\; I_{max}\big) & \text{otherwise}
\end{cases}
$$

Bounding the integrator state with I_max = 0.6·u_max also guards against long disturbances such as a hand holding the arm.

**Method B: back-calculation (optional, smoother recovery):**

$$
I_k = I_{k-1} + \Delta I_k + \frac{T_s}{T_t}\big(u_k - u^*_k\big), \qquad T_t \approx \sqrt{T_i T_d}\ \text{ or }\ T_t \approx T_i/2
$$

### 3.4 Outer Loop Specifics

- **Wrap the angle error:**
  $$e_\theta = \operatorname{atan2}(\sin(\theta_{ref}-\theta), \cos(\theta_{ref}-\theta))$$
- **Rate command with setpoint feedforward:**
  $$\omega_{ref} = \operatorname{sat}_{\pm\omega_{max}}\!\big(K_{p\theta}\, e_\theta + I_\theta + \dot\theta_{ref}\big)$$
- **Cross-loop anti-windup:** if the inner loop is saturated, also freeze I_θ. Otherwise the outer integrator winds up against a limit it cannot see.

---

## 4. Reference Implementation (ESP-IDF, C)

```c
typedef struct {
    float kp, ki, kd;
    float tau;            // derivative filter time constant
    float Ts;
    float out_min, out_max;
    float i_min, i_max;
    // state
    float integ, d_state, y_prev, e_prev;
    bool  saturated;      // exported for cascade anti-windup
    // precomputed
    float alpha, beta;
} pid_t;

void pid_init(pid_t *p, float kp, float ki, float kd, float fD_hz,
              float Ts, float umin, float umax, float ifrac)
{
    *p = (pid_t){ .kp=kp, .ki=ki, .kd=kd