# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis (Cycle 19)

> **Note:** I don't have your earlier cycle documents, so the numbers below rest on stated assumptions (§1). Replace them with your measured values. The structure and equations carry over unchanged.

---

## 1. Assumptions and Plant Model

| Parameter | Symbol | Assumed value |
|---|---|---|
| Roll inertia (motor + camera) | J | 2.0 × 10⁻⁴ kg·m² |
| Viscous friction | b | 1.0 × 10⁻⁴ N·m·s/rad |
| Motor torque constant (FOC, q-axis) | K_t | 0.045 N·m/A |
| Max q-axis current | I_max | 1.5 A, so τ_max ≈ 0.0675 N·m |
| IMU | ICM-42688-P, gyro ODR 4 kHz, SPI | |
| Inner (rate) loop rate | f_i | 2 kHz (T_i = 500 µs) |
| Outer (angle) loop rate | f_o | 500 Hz (T_o = 2 ms) |
| Motor driver | FOC current loop ≥ 10 kHz (MCPWM) | |
| Roll mechanical limit | ±45° | |

**Plant (current to rate), assuming the current loop is much faster than the rate loop:**

$$
G_\omega(s) = \frac{\omega(s)}{I_q(s)} = \frac{K_t}{J s + b}
$$

**Angle plant:** θ(s) = ω(s)/s.

**Unbalance disturbance (roll-specific):**

$$
\tau_g = m g\, l_{cg} \sin\theta
$$

This term is handled by integral action plus optional feedforward (§5).

---

## 2. Loop Architecture

```
θ_ref ─►(+)─► [Angle PI + FF] ─ω_ref─►(+)─► [Rate PID] ─I_q,ref─► [FOC] ─► Motor/Payload ─┬─► ω
        ▲ −                    (clamp ±ω_max)  ▲ −    (clamp ±I_max)                      │
        │                                      └─────── gyro_x (notch + LPF) ◄────────────┤
        └──────────────── θ̂ (Mahony / complementary filter) ◄─────────────────────────────┘
                           ▲
           saturation flag from inner loop ───► outer integrator freeze
```

**Bandwidth separation rule:** ω_c,outer ≤ ω_c,inner / 5.

- Inner crossover: **60 Hz** (377 rad/s)
- Outer crossover: **10 Hz** (62.8 rad/s)

---

## 3. Inner Rate Loop (PID, 2 kHz)

### 3.1 Gain Derivation (Loop Shaping)

Place the crossover at ω_ci, with the PI zero a factor of 4 below it for about 55–60° phase margin.

**Proportional gain:**

$$
K_{p\omega} = \frac{J\,\omega_{ci}}{K_t} = \frac{2\times10^{-4}\cdot 377}{0.045} \approx 1.68\ \text{A/(rad/s)}
$$

**Integral gain:**

$$
K_{i\omega} = K_{p\omega}\cdot\frac{\omega_{ci}}{4} \approx 1.68 \cdot 94 \approx 158\ \text{A/rad}
$$

**Derivative gain.** Keep it small. D on the rate loop amplifies gyro noise into motor current. Use it only to damp payload/bracket resonance:

$$
K_{d\omega} \approx 0.0005\text{–}0.002\ \text{A·s/rad}\quad(\text{start at }0)
$$

### 3.2 Control Law (Derivative on Measurement)

$$
u[k] = \underbrace{K_p\, e[k]}_{P} + \underbrace{I[k]}_{I} + \underbrace{D[k]}_{D} + u_{ff}[k],
\qquad e[k] = \omega_{ref}[k] - \omega[k]
$$

D acts on −ω, not on e, so setpoint steps from the outer loop cause no derivative kick.

---

## 4. Derivative Filtering

### 4.1 Continuous Form

Pure differentiation is unrealizable and amplifies noise, so band-limit it:

$$
D(s) = -K_d\,\frac{s}{1 + \tau_f s}\,Y(s),\qquad \tau_f = \frac{1}{\omega_f},\quad \omega_f = N\cdot\frac{K_p}{K_d}\ \text{or set directly}
$$

**Choose** f_f ≈ 3–5 × f_c, but below gyro noise and above the loop crossover. For a 60 Hz inner loop, **f_f = 250 Hz**, giving τ_f = 6.37 × 10⁻⁴ s.

### 4.2 Discretization (Tustin, Preserves Stability)

$$
D[k] = \frac{2\tau_f - T}{2\tau_f + T}\,D[k-1] \;-\; \frac{2K_d}{2\tau_f + T}\,\big(y[k] - y[k-1]\big)
$$

Define the coefficients once:

$$
\alpha_d = \frac{2\tau_f - T}{2\tau_f + T},\qquad \beta_d = \frac{2K_d}{2\tau_f + T}
$$

With T = 500 µs and τ_f = 637 µs: α_d = 0.436 and β_d = 1.11 · K_d.

**Alternative (backward Euler, always non-oscillatory):**

$$
D[k] = \frac{\tau_f}{\tau_f + T}D[k-1] - \frac{K_d}{\tau_f + T}\big(y[k]-y[k-1]\big)
$$

Use this one if τ_f approaches T/2. Tustin's α_d goes negative there and rings at Nyquist.

### 4.3 Gyro Pre-Filtering (Before the PID)

1. **Hardware:** ICM-42688 AAF set to about 500 Hz.
2. **Notch** at the measured structural resonance (often 150–400 Hz on gimbal arms). Use a biquad with Q ≈ 3–5:

$$
H_n(z) = \frac{1 - 2\cos\omega_0 z^{-1} + z^{-2}}{1 - 2r\cos\omega_0 z^{-1} + r^2 z^{-2}},\quad \omega_0 = 2\pi f_n T,\ r \approx 0.9
$$

3. **First-order LPF** on the P-path gyro at about 300 Hz, only if needed. Budget its phase lag: at 60 Hz, a 300 Hz LPF costs about 11°.

---

## 5. Outer Angle Loop (PI + Feedforward, 500 Hz)

### 5.1 Gains

The inner loop is approximately unity at outer-loop frequencies, so the angle plant is roughly 1/s:

$$
K_{p\theta} = \omega_{co} = 62.8\ \text{(rad/s)/rad} \;\; (\text{start at } \sim 40\text{ and tune up})
$$

$$
K_{i\theta} = K_{p\theta}\cdot\frac{\omega_{co}}{8} \approx 40\text{–}500\ \text{s}^{-2}\ (\text{small; removes static unbalance error})
$$

No D term is needed. The inner rate loop already provides damping.

### 5.2 Control Law

$$
\omega_{ref}[k] = K_{p\theta}\,e_\theta[k] + I_\theta[k] + \dot\theta_{ref}[k]
$$

$$
e_\theta = \mathrm{wrap}_{\pm\pi}(\theta_{ref} - \hat\theta)
$$

$$
\omega_{ref} \in [-\omega_{max}, \omega_{max}],\quad \omega_{max} = 6\ \text{rad/s}
$$

### 5.3 Gravity Feedforward (Optional, Injected at the Inner Output)

$$
u_{ff} = \frac{\hat m g l_{cg}}{K_t}\sin\hat\theta
$$

This offloads the integrator so it only has to absorb the residual error.

---

## 6. Anti-Windup

Use **two mechanisms per loop** (clamping plus back-calculation), and **propagate saturation outward** in the cascade.

### 6.1 Output Saturation

$$
u_{unsat} = P + I + D + u_{ff},\qquad u_{sat} = \mathrm{sat}(u_