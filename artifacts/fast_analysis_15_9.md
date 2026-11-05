# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have the specifics of "Cycle 15", such as your motor, IMU, inertia or prior tuning. This design uses the typical BLDC-gimbal assumptions below. Replace the marked values with your measured ones.

## 1. Architecture and Assumptions

```
 θ_ref ──►(Σ)──► Angle PI ──► ω_ref ─►(Σ)──► Rate PID ──► τ/i_q cmd ──► FOC ──► Motor ──► Roll
          ▲  e_θ   (outer)    ▲ clamp  ▲ e_ω   (inner)       ▲ clamp                     │
          │                   │        │                                                 │
          │      ω_ff ────────┘        └──────── ω_meas (gyro X, filtered) ◄─────────────┤
          └──────────────────────── θ_meas (Mahony/complementary) ◄──────────────────────┘
```

| Parameter | Assumed value |
|---|---|
| IMU | ICM-42688 / BMI088, gyro ODR 8 kHz, SPI |
| Inner (rate) loop | **2 kHz** (Ts_i = 500 µs) |
| Outer (angle) loop | **1 kHz** (Ts_o = 1 ms), run every 2nd inner tick |
| Actuator | Gimbal BLDC (e.g. GBM2804), FOC with i_q or voltage-mode torque command |
| Roll inertia J | 1.5·10⁻⁴ kg·m² (**measure**) |
| Torque constant K_t | 0.04 N·m/A (**measure**) |
| Inner-loop bandwidth | 50 Hz |
| Outer-loop bandwidth | 10 Hz (≥5× separation from inner) |

**Execution on the ESP32-S3:**
- Pin the control task to **core 1**. Leave Wi-Fi/BT on core 0.
- Trigger it from an MCPWM or GPTimer ISR that notifies the task.
- Use `float` only. The S3 has a single-precision FPU; `double` is software-emulated.

## 2. Measurement Conditioning (Before the PIDs)

**Gyro filtering.** Apply a 2nd-order Butterworth low-pass at 200 Hz. Optionally add a notch at the frame or motor resonance, found from a gyro FFT log.
- Keep total filter phase lag at 50 Hz below about 15°, or the inner loop loses phase margin.

**Angle estimate.** Use a Mahony or complementary filter at 1 kHz.

**Angle error wrapping.** Wrap the error to (−π, π]:

```
e_θ = atan2f(sinf(θ_ref − θ_meas), cosf(θ_ref − θ_meas))
```

## 3. Inner Loop: Rate PID (2 kHz)

### 3.1 Control law

The derivative acts on the measurement, not the error, which avoids derivative kick on setpoint steps:

```
e_ω[k]   = ω_ref[k] − ω_meas[k]
P[k]     = Kp_ω · e_ω[k]
I[k]     = (see 3.3)
D[k]     = (see 3.2)
u_raw[k] = P[k] + I[k] + D[k] + u_ff[k]
u[k]     = sat(u_raw[k], −U_max, +U_max)        // U_max = i_q limit (A) or V limit
```

### 3.2 Filtered derivative

The continuous form is a derivative on measurement with a first-order roll-off:

```
D(s) = − Kd · s / (1 + T_f·s) · Y(s)
```

Choose the filter time constant by either rule:
- **Cutoff frequency:** T_f = 1/(2π·f_D), with f_D ≈ 80–150 Hz. It must stay below the gyro LPF and above the loop bandwidth.
- **Derivative-gain limit:** T_f = Kd / (N·Kp), with N = 5…20.

**Tustin discretization (preferred, no phase warping at these ratios):**

```
α_D  = (2T_f − Ts) / (2T_f + Ts)
β_D  = 2Kd / (2T_f + Ts)

D[k] = α_D · D[k−1] − β_D · (y[k] − y[k−1])
```

**Backward-Euler alternative (unconditionally stable, slightly more lag):**

```
D[k] = T_f/(T_f+Ts) · D[k−1] − Kd/(T_f+Ts) · (y[k] − y[k−1])
```

Two practical notes:
- In the rate loop, y = ω, so D is a filtered angular acceleration. This is noisy, so Kd_ω is usually small or zero.
- If you need damping, prefer adding it in the outer loop (where y = θ and D ≈ −Kd·ω, i.e. rate feedback).

### 3.3 Anti-windup

Use three layers, in order.

**(a) Back-calculation**, which actively bleeds the integrator while saturated:

```
I[k+1] = I[k] + Ki_ω·Ts·e_ω[k] + (Ts/T_t)·(u[k] − u_raw[k])
```

The tracking time constant is T_t = √(T_i·T_d) with T_i = Kp/Ki and T_d = Kd/Kp. If Kd = 0, use T_t ≈ 0.5·T_i.

**(b) Conditional integration (clamping).** Freeze the integrator when it would push further into saturation:

```
if ( u_raw ≠ u  AND  sign(e_ω) == sign(u_raw) )  →  skip the Ki·Ts·e term
```

**(c) Hard integrator clamp**, a backstop against estimator glitches:

```
I[k+1] = sat(I[k+1], −I_max, +I_max),   with I_max ≈ 0.6–0.8 · U_max
```

### 3.4 Initial gains (plant ≈ K_t / (J·s))

```
Kp_ω = J·ω_c / K_t          = 1.5e-4 · 314 / 0.04 ≈ 1.18   A/(rad/s)
Ki_ω = Kp_ω · ω_c / 5       ≈ 74                           A/rad
Kd_ω = 0 … 0.002            (only if resonance-free)
```

## 4. Outer Loop: Angle PI(D) (1 kHz)

### 4.1 Control law

```
e_θ[k]      = wrap(θ_ref − θ_meas)
ω_cmd_raw   = Kp_θ·e_θ + I_θ + D_θ + ω_ff          // ω_ff = stick rate / base-motion feedforward
ω_ref       = sat(ω_cmd_raw, −ω_max, +ω_max)        // e.g. ω_max = 6 rad/s
```

- Kp_θ ≈ 2π·10 ≈ **63 s⁻¹**. A pure P outer loop is a 10 Hz first-order tracker when the inner loop is ideal.
- Ki_θ ≈ Kp_θ·ω_c,outer/10 ≈ **40 s⁻²**. This removes static error from cable drag and imbalance.
- D_θ: use the same filtered-derivative equations with y = θ_meas, at Ts = 1 ms. It is often unnecessary because the inner loop already provides rate damping.

### 4.2 Cascade anti-windup (critical)

The outer integrator must also stop when the **inner** loop is saturated, or it winds up while the motor is torque-limited.

```
sat_outer = (ω_ref ≠ ω_cmd_raw)
sat_inner = (u ≠ u_raw)  AND  sign(e_θ) == sign(u_raw)   // inner pushing same way as outer demand

if (!sat_outer && !sat_inner)  I_θ += Ki_