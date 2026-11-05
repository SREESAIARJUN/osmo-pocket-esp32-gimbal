# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have the details of the previous cycles, so this design states its assumptions explicitly. Replace the numbers marked as assumed with your measured values.

| Item | Assumed value |
|---|---|
| Actuator | Gimbal BLDC driven by FOC; the controller outputs a torque or current command (`iq_ref`) |
| Sensor | 6-axis IMU (ICM-42688-class) with gyro ODR ≥ 2 kHz, fused roll angle (Mahony/complementary) |
| Roll inertia (payload + motor) | J ≈ 2.0×10⁻⁴ kg·m² |
| Motor torque constant | Kt ≈ 0.05 N·m/A, current limit ±1.5 A |
| MCU | ESP32-S3 at 240 MHz, single-precision FPU, control on Core 1 |

---

## 1. Loop Architecture

```
θ_ref ──►(+)──► [Angle PI] ──ω_ref──►(+)──► [Rate PID] ──u──► [Sat] ──► FOC iq_ref ──► Motor/Payload
          ▲−        ▲ω_ff (feedforward)  ▲−                           │
          │                              │                            │
       θ̂ (AHRS, 500 Hz) ◄────────────── ω_gyro (LPF + notch, 2 kHz) ◄─┘
                         ▲
            sat flag ────┘  (inner saturation freezes outer integrator)
```

| Loop | Rate | Ts | Target bandwidth |
|---|---|---|---|
| Rate (inner) | 2 kHz | 0.5 ms | ~40 Hz |
| Angle (outer) | 500 Hz | 2 ms | ~6–8 Hz (≥5× below inner) |
| FOC current loop | ≥ 10 kHz (separate) | — | ≥ 500 Hz |

The rule is that each loop's bandwidth is at least 5–10× below the loop inside it. That lets the outer loop treat the inner loop as approximately unity gain.

---

## 2. Signal Conditioning (before the PID)

**Gyro anti-noise filter.** Use a 2nd-order Butterworth low-pass with fc ≈ 150–250 Hz. Keep it well above the 40 Hz crossover so it adds little phase lag. The added lag at the crossover frequency is approximately:

φ ≈ −1.41·(f/fc) rad, which gives about −20° at 40 Hz with fc = 200 Hz. Budget for this lag in your phase margin.

**Notch filter.** Add a notch at the frame/arm resonance, identified from a gyro FFT taken during a chirp test. The biquad form is:

H(z) = (b₀ + b₁z⁻¹ + b₂z⁻²)/(1 + a₁z⁻¹ + a₂z⁻²)

The coefficients come from the standard RBJ notch formulas, with Q ≈ 2–5.

**Angle error wrapping.** Wrap the angle error so the loop never takes the long way around:

e_θ = atan2(sin(θ_ref − θ̂), cos(θ_ref − θ̂))

---

## 3. Outer Angle Loop (PI + Feedforward)

Continuous form:

ω_ref = K_pθ·e_θ + K_iθ ∫e_θ dt + ω_ff,  then ω_ref is clamped to ±ω_max

Discrete form, with Ts = 2 ms:

- P_k = K_pθ · e_θ,k
- I_k = I_{k−1} + K_iθ · Ts · e_θ,k (subject to the anti-windup logic in §5)
- ω_ref,k = sat(P_k + I_k + ω_ff, ±ω_max)

Notes:

- **Derivative term.** No D term is needed in the angle loop. The rate loop already provides damping, and adding D here would double-differentiate noise.
- **Feedforward.** ω_ff = dθ_ref/dt. This comes from the trajectory or joystick smoother and greatly improves tracking.
- **Starting gains.** K_pθ ≈ ω_c,outer = 2π·7 ≈ 45 s⁻¹. K_iθ ≈ K_pθ·ω_c,outer/10 ≈ 20 s⁻². Set ω_max ≈ 6 rad/s.

---

## 4. Inner Rate Loop (PID with Derivative on Measurement)

Continuous form:

u = K_p·e_ω + K_i ∫e_ω dt − K_d·(s/(1 + sT_f))·ω_meas

Here e_ω = ω_ref − ω_meas, and u is the torque command (N·m), converted to current by iq_ref = u/Kt.

**Why derivative on measurement.** Taking D on −ω_meas instead of on e_ω avoids a "derivative kick" when ω_ref steps, for example when the angle setpoint jumps.

### 4.1 Derivative Filtering

The filtered derivative has the transfer function:

D(s) = −K_d·s / (1 + s·T_f) · ω_meas, with T_f = K_d/(N·K_p) and N ≈ 8–15

Choose N so the filter pole (1/T_f) sits around 3–5× the crossover frequency, and below the gyro LPF cutoff.

**Tustin (bilinear) discretization (recommended):**

D_k = α·D_{k−1} − β·(ω_k − ω_{k−1})

where α = (2T_f − Ts)/(2T_f + Ts) and β = 2K_d/(2T_f + Ts).

**Backward Euler alternative (always stable, slightly more lag):**

D_k = [T_f/(T_f + Ts)]·D_{k−1} − [K_d/(T_f + Ts)]·(ω_k − ω_{k−1})

Precompute α and β whenever the gains change, not every cycle.

### 4.2 Proportional and Integral Terms

- P_k = K_p·e_k
- I_k = I_{k−1} + K_i·Ts·e_k, gated by the anti-windup logic in §5

**Plant-based starting gains.** The rigid-body plant is ω/τ = 1/(J·s). For a target crossover ω_c = 2π·40 ≈ 251 rad/s:

- K_p = J·ω_c = 2×10⁻⁴ · 251 ≈ **0.050 N·m/(rad/s)**
- K_i = K_p·ω_c/5 ≈ **2.5 N·m/rad** (places the PI zero at ω_c/5, costing about 11° of phase)
- K_d ≈ 0.1·K_p/ω_c·… → start at **≈ 2×10⁻⁵ N·m·s²/rad** and raise only if overshoot persists
- N = 10, so T_f = K_d/(N·K_p) ≈ 4×10⁻⁵ s. Clamp T_f to at least 2·Ts, so T_f ≥ 1 ms in practice.

### 4.3 Output Saturation

u_sat = clamp(u, −u_max, +u_max), with u_max = Kt·I_max = 0.05 × 1.5 = 0.075 N·m

---

## 5. Anti-Windup

### 5.1 Conditional Integration (Clamping)

Compute the unsaturated output with the *previous* integrator value:

u_unsat = P_k + I_{k−1} + D_k

Then decide whether the integrator may update:

- **Freeze** the integrator (I_k = I_{k−1}) if **(u_unsat > u_max and e_k > 0)** OR **(u_unsat < −u_max and e_k < 0)**.
- **Otherwise integrate:** I_k = I_{k−1} + K_i·Ts·e_k.

In both cases, also apply a hard clamp:

I_k = clamp(I_k, −I_lim, +I_lim), with I_lim ≈ 0.6·u_max

The hard clamp bounds the integrator against long-term disturbances such as a cable snag or payload imbalance.

### 5.2 Back-Calculation