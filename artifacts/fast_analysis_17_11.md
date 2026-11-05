# Cascaded PID Design: Gimbal Roll Axis on ESP32-S3

I don't have the earlier cycles of this project, so this design states its assumptions explicitly. Adjust the numbers to match your hardware. The structure and equations carry over unchanged.

## 0. Assumptions

| Item | Assumed value |
|---|---|
| Actuator | BLDC gimbal motor (e.g., GBM2804, 2208), FOC driver, torque command = q-axis voltage *U_q* (or current *I_q* if you have current sense) |
| Sensor | 6-axis IMU (ICM-42688 / BMI270 / MPU6050) on the camera plate, SPI preferred |
| Attitude | Complementary or Mahony filter → roll angle θ; gyro X → roll rate ω |
| Loop rates | Inner (rate) loop 1–2 kHz; outer (angle) loop 500 Hz–1 kHz. Can be the same tick, running the outer loop every 2nd iteration |
| Core allocation | Control task pinned to **Core 1**; Wi-Fi/BLE/telemetry on Core 0 |

---

## 1. Architecture

```
 θ_ref ─►(+)─► [ Angle PI ] ─► ω_ff + ─► clamp(±ω_max) ─► ω_ref
          ▲-                                                │
          θ (attitude filter)                               ▼
                                     ω_ref ─►(+)─► [ Rate PID ] ─► clamp(±U_max) ─► FOC U_q ─► Motor
                                              ▲-                                          │
                                              ω (gyro X, filtered)  ◄──── plant ──────────┘
```

**Why cascade?** The gyro measures ω directly with low latency, so the inner loop rejects disturbances (wind, cable drag, handheld shake) before they show up as an angle error. The outer loop only corrects slow drift.

**Bandwidth separation rule:** f_BW,outer ≤ f_BW,inner / 5 (aim for 5–10×).

Typical targets:
- Inner rate loop: 30–60 Hz
- Outer angle loop: 5–10 Hz

---

## 2. Inner Loop: Rate PID

### 2.1 Control law (parallel form, discrete, sample time Tₛ)

$$e_\omega[k] = \omega_{ref}[k] - \omega[k]$$

$$u[k] = \underbrace{K_p\,e_\omega[k]}_{P} + \underbrace{I[k]}_{I} + \underbrace{D[k]}_{D} + \underbrace{u_{ff}[k]}_{\text{optional FF}}$$

$$u_{sat}[k] = \text{clamp}\big(u[k],\,-U_{max},\,+U_{max}\big)$$

### 2.2 Derivative term: derivative-on-measurement with first-order filter

**Derivative on measurement, not on error.** Differentiating the error would turn every step in ω_ref (from the outer loop or the joystick) into a "derivative kick." Differentiating −ω avoids this.

**The noise problem.** The derivative of the gyro signal is angular acceleration, which is very noisy. The term therefore needs a low-pass filter:

$$D(s) = -K_d\,\frac{s}{\tau_d s + 1}\,\Omega(s), \qquad \tau_d = \frac{1}{2\pi f_d}$$

**Discretization by backward Euler** (simple, unconditionally stable):

$$\alpha = \frac{\tau_d}{\tau_d + T_s}$$

$$D[k] = \alpha\,D[k-1] \;-\; K_d\,(1-\alpha)\,\frac{\omega[k]-\omega[k-1]}{T_s}$$

**Discretization by Tustin** (better phase match near f_d; use it if f_d > ~f_s/10):

$$D[k] = \frac{2\tau_d - T_s}{2\tau_d + T_s}\,D[k-1] \;-\; \frac{2K_d}{2\tau_d + T_s}\,\big(\omega[k]-\omega[k-1]\big)$$

**Choosing f_d:**
- Place f_d at 3–5× the inner-loop crossover, e.g. 100–200 Hz at a 1–2 kHz loop rate.
- Equivalently, use the classic N-factor: τ_d = K_d / (N·K_p), with N = 5…20.
- Also set the IMU's internal DLPF (e.g., ~180–250 Hz) so the D filter is not the only line of defense.
- If your frame has a structural resonance (common at 80–300 Hz on gimbal arms), add a biquad notch on ω **before** the PID.

### 2.3 Integral term with anti-windup

Windup happens when *u* saturates (motor voltage limit, or a mechanical stop during a hard tilt). The integrator keeps accumulating and then causes a large overshoot once the axis is freed. Two options follow; Option A is recommended for this axis.

#### Option A: Conditional integration (clamping) — recommended

$$I_{cand} = I[k-1] + K_i\,T_s\,e_\omega[k]$$

$$\text{freeze} = \big(u[k] \neq u_{sat}[k]\big)\;\wedge\;\big(\operatorname{sign}(e_\omega[k]) = \operatorname{sign}(u[k])\big)$$

$$I[k] = \begin{cases} I[k-1] & \text{if freeze} \\ \text{clamp}\big(I_{cand},\,-I_{max},\,+I_{max}\big) & \text{otherwise}\end{cases}$$

- Integration stops only when the output is saturated **and** the error is pushing further into saturation.
- Integration is still allowed when the error would pull the output back out of saturation.
- The hard bound I_max (e.g., 0.5·U_max) is a second safety net. It limits how much steady torque the I-term alone can command, which stops a slow runaway cooking the motor.

#### Option B: Back-calculation (alternative, smoother recovery)

$$I[k] = I[k-1] + T_s\Big(K_i\,e_\omega[k] + K_b\,\big(u_{sat}[k-1] - u[k-1]\big)\Big)$$

The tracking gain is typically:

$$K_b \approx \frac{1}{\sqrt{T_i T_d}} = \sqrt{\frac{K_i}{K_d}} \qquad \text{(or } K_b \approx K_i/K_p \text{ if } K_d = 0\text{)}$$

### 2.4 Gain changes without bumps

If gains are tuned live (over Wi-Fi or BLE), store the integrator as an **accumulated output** (Kᵢ·Tₛ·e summed), as both equations above already do. Then changing Kᵢ does not cause a step in *u*.

---

## 3. Outer Loop: Angle PI (P is usually enough)

### 3.1 Control law

$$e_\theta[k] = \operatorname{wrap}_{\pm\pi}\big(\theta_{ref}[k] - \theta[k]\big)$$

$$\omega_{ref}[k] = \text{clamp}\Big(K_{p\theta}\,e_\theta[k] + I_\theta[k] + \dot\theta_{ref}[k],\;\; -\omega_{max},\;+\omega_{max}\Big)$$

- **Wrap the error to ±π.** Roll rarely crosses ±180°, but the wrap makes the controller robust if it ever does.
- **No D term is needed.** The inner loop already provides damping, and the gyro supplies the rate directly.
- **Feed forward θ̇_ref** (the derivative of the commanded angle, e.g., from a joystick slew or follow mode). This gives smooth tracking without relying on error.
- **ω_max** (e.g., 200–400 °/s) is the slew-rate limit. It prevents violent snaps after power-up or recentering.

### 3.2 Outer integrator: cascade-aware anti-windup

Use the outer integrator only if steady-state angle error persists. It should be small, because the inner I-term usually handles gravity/imbalance torque. The outer integrator must freeze when **either** stage saturates:

$$\text{freeze}_\theta = \text{sat}_{outer}\;\vee\;\big(\text{sat}_{inner} \wedge \operatorname{sign}(e_\theta)=\operatorname{sign}(\omega_{ref})\big)$$

$$I_\theta[k] = \begin{cases} I_\theta[k-1] & \text{if freeze}_\theta \\ \text{clamp}\big(I_\theta[k-1] + K_{i\theta}\,T_{s,o}\,e_\theta,\;\pm I_{\theta,max}\big) & \text{otherwise}\end{cases}$$

This step is often missed. If the motor is at its voltage limit, the outer integrator must not keep asking for more rate.

---

