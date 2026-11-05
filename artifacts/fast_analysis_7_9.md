# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

> **Note on "Cycle 7":** I don't have your project's earlier cycles, so I can't see your specific hardware, prior gains, or requirements. The design below rests on explicit assumptions (Section 1). Replace them with your values. All equations are written so the coefficients recompute from your parameters.

---

## 1. Assumptions

| Parameter | Assumed value | Notes |
|---|---|---|
| MCU | ESP32-S3 @ 240 MHz, single-precision FPU | Use `float`, not `double`. Doubles are software-emulated. |
| IMU | 6-axis (e.g. ICM-42688-P / BMI270), SPI | Gyro ODR ≥ 2 kHz |
| Actuator | Gimbal BLDC (e.g. 2804/4108) with FOC driver | Inner loop outputs torque/current (or q-axis voltage) |
| Roll inertia J | ~2×10⁻⁴ kg·m² (incl. camera) | Measure or identify. Gain scaling depends on it. |
| Inner (rate) loop | **1 kHz**, Ts_r = 1 ms | Target bandwidth 30–50 Hz |
| Outer (angle) loop | **250 Hz**, Ts_a = 4 ms | Target bandwidth 5–10 Hz (≥5× separation) |
| Attitude estimate | Mahony/complementary or EKF, 1 kHz | Roll angle φ̂ |

---

## 2. Loop Architecture

```
            Outer loop (250 Hz)                  Inner loop (1 kHz)
 φ_sp ─►[slew limit]─►(+)─►[ Angle PI ]─► ω_sp ─►(+)─►[ Rate PID ]─►(+)─► u ─►[sat]─► FOC/Motor ─► Gimbal
                        ▲−                  ▲     ▲−                  ▲                                 │
                        │                   │     │                   │ K_ff·ω̇_sp (optional)            │
                        │            ω_ff ──┘     │                   │                                 │
                        φ̂ ◄──── attitude filter ◄─┴── ω_gyro (LPF) ◄───────────── IMU ◄────────────────┘
```

**Design rules:**
- **Inner loop uses raw gyro rate**, not the derivative of the angle estimate. It has lower latency and less noise.
- **Outer loop is P or PI only.** The inner loop already provides the damping, so an outer D term is redundant and adds noise.
- **Feedforward:** `ω_ff = dφ_sp/dt` for smooth pan/follow commands. An optional torque feedforward `J·ω̇_sp` goes into the inner loop.
- **Saturation chain:** the outer output (ω_sp) is clamped to ±ω_max, and the inner output (u) is clamped to the driver limit. Each loop has its own anti-windup.

---

## 3. Controller Equations (Continuous Form)

**Outer (angle):**

$$\omega_{sp}(t) = K_{p,a}\,e_a + K_{i,a}\!\int e_a\,dt + \omega_{ff},\qquad e_a = \text{wrap}(\phi_{sp}-\hat\phi)$$

**Inner (rate)**, with derivative on measurement and setpoint weighting b:

$$u(t) = K_p\,(b\,\omega_{sp}-\omega) + K_i\!\int(\omega_{sp}-\omega)\,dt - K_d\,\frac{s}{T_f s+1}\,\omega$$

- b ≈ 0.7–1.0 reduces overshoot on step commands.
- The derivative acts on −ω (measurement), **not on the error**. This avoids "derivative kick" when ω_sp steps, which happens every outer-loop update.

---

## 4. Derivative Filtering

### 4.1 Filter corner choice

The filtered derivative is $D(s) = -K_d \dfrac{s}{T_f s + 1}\,\omega(s)$, with $T_f = \dfrac{K_d}{K_p N}$, N = 5–20.

The derivative filter corner should satisfy:

$$f_c = \frac{1}{2\pi T_f}\approx 3\text{–}5\times f_{bw,inner}\quad\text{and}\quad f_c < \frac{f_s}{4}$$

For a 40 Hz inner bandwidth at 1 kHz, use **f_c ≈ 150 Hz → T_f ≈ 1.06 ms**.

Gimbals also suffer from **motor-frame structural resonances (typically 80–300 Hz)**. Add a separate 2nd-order biquad LPF (≈100–150 Hz) or a notch on the gyro signal feeding the D term. Mind the phase lag it adds at crossover: keep it under about 10°.

### 4.2 Discretization (Tustin / bilinear, recommended)

Substituting $s = \frac{2}{T_s}\frac{z-1}{z+1}$:

$$D[k] = \alpha\,D[k-1] \;-\; \beta\,\big(\omega[k]-\omega[k-1]\big)$$

$$\alpha = \frac{2T_f - T_s}{2T_f + T_s},\qquad \beta = \frac{2K_d}{2T_f + T_s}$$

With T_s = 1 ms and T_f = 1.06 ms: α = 0.36, β = 0.32·K_d·10³.

### 4.3 Alternative: Backward Euler (always stable, more lag)

$$D[k] = \frac{T_f}{T_f+T_s}D[k-1] - \frac{K_d}{T_f+T_s}\big(\omega[k]-\omega[k-1]\big)$$

Use this if T_f < T_s/2. In that case Tustin's α goes negative, which causes sign-alternating ringing.

---

## 5. Anti-Windup

The **inner loop saturates often**, for example on fast pans, at motor current limits, or on hand bumps. Use **clamping (conditional integration) plus back-calculation**.

### 5.1 Discrete PID with back-calculation

Per step k, with $e[k] = \omega_{sp}[k]-\omega[k]$:

$$P[k] = K_p\,(b\,\omega_{sp}[k] - \omega[k])$$

$$v[k] = P[k] + I[k] + D[k] + u_{ff}[k]\quad\text{(unsaturated)}$$

$$u[k] = \text{sat}(v[k]) = \min(\max(v[k],\,u_{min}),\,u_{max})$$

Integrator update (forward Euler, includes tracking correction):

$$I[k+1] = I[k] + K_i T_s\,e[k] + \frac{T_s}{T_t}\big(u[k]-v[k]\big)$$

Tracking time constant: $T_t = \sqrt{T_i T_d}$, with $T_i = K_p/K_i$ and $T_d = K_d/K_p$. If $T_d = 0$, use $T_t \approx 0.1\text{–}0.5\,T_i$.

### 5.2 Conditional clamping (belt and braces)

Freeze integration when saturated **and** the error would drive further into saturation:

$$\text{freeze} = \big(u[k]\neq v[k]\big)\;\wedge\;\big(\text{sign}(e[k]) = \text{sign}(v[k])\big)$$

$$I[k+1] = \begin{cases} I[k] + \frac{T_s}{T_t}(u[k]-v[k]) & \text{freeze}\\ I[k] + K_iT_s\,e[k] + \frac{T_s}{T_t}(u[k]-v[k]) & \text{otherwise}\end{cases}$$

Then hard-clamp the integrator state itself:

$$I[k+1] = \text{clamp}\big(I[k+1],\; -I_{max},\; +I_{max}\big),\quad I_{max}\approx 0.5\text{–}0.7\,u_{max}$$

`I_max` exists to counter cogging and cable drag. It should never need to equal the full actuator range.

### 5.3 Outer-loop anti-windup

When ω_sp is clamped to ±ω_max, apply the same rules to the angle integrator.

**Cascade-aware rule:** if the **inner** loop is saturated, also freeze the **outer** integrator. Otherwise the outer loop winds up demanding rate the inner loop cannot deliver.

$$\text{freeze}_{outer} = \text{freeze}_{outer,local}\;\vee\;\text{inner\_saturated}$$

---

## 6. Gain Starting Points

Model the inner plant as roughly $G(s) = \dfrac{K_t}{J s}$ (torque to rate), with motor/driver lag folded into the phase margin.

**Inner (rate) loop**, target crossover $\omega_{c,r} = 2\pi\cdot40$ rad/s:
- $K_p \approx \dfrac{J\,\omega_{c,r}}{K_t}$
- $K_i \approx K_p\cdot\omega_{c,r}/5$
- $K_d$: small, set so the D-term phase lead peaks near crossover. Often $K_d \approx K_p/(3\,\omega_{c,r})$.
- In practice, tune P first, then add D until ringing disappears, then add I.

**Outer (angle) loop**, target $\omega_{c,a} = 2\pi\cdot6$ rad/s (≥5× below inner):
- $K_{p,a} \approx \omega_{c,a} \approx 38$ (rad/s)/rad. The inner closed loop is ≈1 at outer frequencies.
- $K_{i,a} \approx K_{p,a}\cdot\omega_{c,a}/10$, or 0 if the steady-state angle error is acceptable.

**Verification:** inject a chirp at the inner loop and measure the Bode response from logged data.
- Target phase margin ≥ 45° and gain margin 