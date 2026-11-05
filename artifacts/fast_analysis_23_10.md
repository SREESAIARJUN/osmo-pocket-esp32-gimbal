# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

> **Note on "Cycle 23":** I don't have your project's Cycle 23 documents, so I can't see its requirements, hardware list, or earlier tuning data. The design below states its assumptions explicitly. Replace the example numbers with your own measured values.

---

## 1. Assumptions (verify against your hardware)

| Item | Assumed value |
|---|---|
| MCU | ESP32-S3 @ 240 MHz, single-precision FPU (use `float`, never `double`) |
| Actuator | Gimbal BLDC (e.g., 2804-class) driven by FOC; the PID output is the q-axis command (voltage or current) |
| Sensor | 6-axis IMU (e.g., ICM-42688-P) over SPI, gyro ODR ≥ 2 kHz |
| Attitude estimate | Complementary or Mahony filter giving roll angle θ |
| Inner (rate) loop | 1 kHz, Ts_i = 1 ms |
| Outer (angle) loop | 250 Hz, Ts_o = 4 ms (every 4th inner tick) |
| Bandwidth targets | Inner ≈ 30–50 Hz, outer ≈ 5–8 Hz (5–10× separation) |

---

## 2. Loop Architecture

```
θ_ref ─►(+)─► [Angle P(I)] ─► ω_ref ─►(+)─► [Rate PID] ─► sat ─► u_q ─► FOC ─► Motor/Gimbal
         ▲ −          clamp ±ω_max    ▲ −                                           │
         │                            │                                             │
         └── θ (attitude filter) ◄────┴── ω (gyro X, LPF) ◄─────────────────────────┘
```

- **The outer loop** turns angle error into a rate setpoint. Mostly P is enough, because the inner loop already acts like an integrator from rate to angle. A small I term can be added to remove steady-state error from cable drag or imbalance.
- **The inner loop** uses the raw (lightly filtered) gyro rate directly. This gives low latency and high bandwidth, and it rejects disturbances such as handle shake before they show up as angle error.

---

## 3. Outer (Angle) Loop Equations

Wrap the angle error to the range [−π, π):

$$e_\theta[k] = \operatorname{wrap}\big(\theta_{ref}[k] - \theta[k]\big)$$

Rate command, with optional setpoint-rate feed-forward:

$$\omega_{ref}^{*}[k] = K_{p\theta}\,e_\theta[k] + I_\theta[k] + \underbrace{\dot\theta_{ref}[k]}_{\text{FF}}$$

Saturate the rate command:

$$\omega_{ref}[k] = \operatorname{clamp}\big(\omega_{ref}^{*}[k],\ -\omega_{max},\ \omega_{max}\big)$$

The outer integrator, if used, applies the same clamping rule as in §5 against the ω_max limit. Typical starting value: K_pθ ≈ 2π·f_outer ≈ 30–50 s⁻¹, with ω_max ≈ 3–6 rad/s.

---

## 4. Inner (Rate) Loop PID Equations

Rate error:

$$e_\omega[k] = \omega_{ref}[k] - \omega[k]$$

Proportional term:

$$P[k] = K_p\, e_\omega[k]$$

Integral term (forward Euler, gated by §5):

$$I[k] = I[k-1] + K_i\, T_s\, e_\omega[k]$$

### 4.1 Derivative: on measurement, with a first-order low-pass filter

Two choices keep the derivative well behaved:

- **Take the derivative of the measurement, not the error.** This avoids "derivative kick" when the setpoint steps.
- **Low-pass filter the derivative.** Gyro noise differentiated at 1 kHz is huge, so filtering is mandatory.

Continuous form:

$$D(s) = -\frac{K_d\, s}{\tau_d s + 1}\,\omega(s)$$

Choosing the filter time constant τ_d:

$$\tau_d = \frac{K_d}{N K_p}\quad (N \approx 8\text{–}20),\qquad \text{or fixed: } \tau_d = \frac{1}{2\pi f_c}$$

Here f_c is roughly 80–150 Hz. Keep f_c ≤ f_s/5, and below the first frame resonance.

**Tustin (bilinear) discretization**, which is recommended because it preserves phase better:

$$D[k] = \frac{2\tau_d - T_s}{2\tau_d + T_s}\,D[k-1] \;-\; \frac{2K_d}{2\tau_d + T_s}\big(\omega[k] - \omega[k-1]\big)$$

Define the coefficients:

$$\alpha = \frac{2\tau_d - T_s}{2\tau_d + T_s},\qquad \beta = \frac{2K_d}{2\tau_d + T_s}$$

which gives:

$$D[k] = \alpha D[k-1] - \beta\,\Delta\omega[k]$$

**Backward-Euler alternative.** This form is unconditionally stable and simpler:

$$D[k] = \frac{\tau_d}{\tau_d + T_s}D[k-1] - \frac{K_d}{\tau_d + T_s}\,\Delta\omega[k]$$

> **Note:** For a rate loop, D acts on angular acceleration. Start with K_d = 0 and add it only if the PI loop shows overshoot or ringing that P alone can't damp. Also pre-filter the gyro (2nd-order LPF or the IMU's on-chip DLPF, around 150–250 Hz). Add a notch filter if you can identify a frame resonance.

### 4.2 Output and saturation

$$u^{*}[k] = P[k] + I[k] + D[k] + u_{ff}[k],\qquad u[k] = \operatorname{clamp}(u^{*}[k],\ -u_{max},\ u_{max})$$

Set u_max from the FOC voltage limit (for example 0.5·V_bus/√3) or from the motor's current/thermal limit.

---

## 5. Anti-Windup

### 5.1 Conditional integration (clamping)

Compute the output first using the *previous* integral value. Then decide whether to integrate:

$$\text{sat} = (u^{*} \neq u),\qquad \text{same\_sign} = \operatorname{sign}(e_\omega) = \operatorname{sign}(u^{*})$$

$$I[k] = \begin{cases} I[k-1] & \text{if sat} \wedge \text{same\_sign} \quad\text{(freeze)}\\[2pt] I[k-1] + K_i T_s e_\omega[k] & \text{otherwise}\end{cases}$$

The integrator only freezes when integrating would push the output further into saturation. It still unwinds immediately when the error reverses.

### 5.2 Hard integrator bound (always apply as well)

$$I[k] = \operatorname{clamp}\big(I[k],\ -I_{max},\ I_{max}\big),\qquad I_{max} \approx 0.3\text{–}0.6\,u_{max}$$

This limits how much a stall or a hand holding the gimbal can wind up the integrator. It also limits the torque spike when the gimbal is released.

### 5.3 Optional: back-calculation (smoother recovery)

$$I[k] = I[k-1] + T_s\Big(K_i e_\omega[k] + \frac{1}{T_t}\big(u[k] - u^{*}[k]\big)\Big),\qquad T_t \approx \sqrt{T_i T_d}\ \ \text{or}\ \ T_t \approx T_i/2$$

Here T_i = K_p/K_i and T_d = K_d/K_p. Use either clamping or back-calculation, not both. Keep the hard bound from §5.2 in either case.

### 5.4 Cascade-specific windup

When the inner loop saturates, the outer loop doesn't know about it. The fix:

- If the inner loop is saturated, also freeze the outer integrator I_θ.
- Pass an `inner_saturated` flag up to the outer loop each tick for this purpose.

---

## 6. Reference C Implementation

```c
typedef struct {
    