# Cascaded PID Design: ESP32-S3 Gimbal, Roll Axis

I don't have your earlier "Cycle" documents, so I don't know your specific motor, IMU, or driver. The design below uses typical brushless-gimbal assumptions, which are listed first. Replace them with your values and the equations still apply.

---

## 1. Assumptions

| Item | Assumed value |
|---|---|
| MCU | ESP32-S3 @ 240 MHz, single-precision FPU |
| IMU | ICM-42688 / MPU-6500 class, gyro ODR ≥ 1 kHz, SPI |
| Attitude estimate | Complementary or Mahony filter → roll angle φ |
| Actuator | Gimbal BLDC (e.g., 2804, ~100 KV) with voltage-mode FOC. Control output is Uq (V), limited to ±U_max |
| Inner (rate) loop | 1 kHz, Ts_r = 1 ms |
| Outer (angle) loop | 500 Hz, Ts_a = 2 ms (runs every 2nd inner tick) |
| Plant | J·φ̈ = K_t·u − b·φ̇ − τ_dist |

---

## 2. Architecture

```
 φ_ref ─►(+)─► Angle P(I) ─► ω_ff + ω_cmd ─►(+)─► Rate PID ─► sat(±U_max) ─► FOC ─► Motor ─► φ
          ▲ −   (500 Hz)      [clamp ±ω_max]  ▲ −  (1 kHz)                                    │
          │                                   └──────── gyro_x (filtered, bias-removed) ◄─────┤
          └───────────────────────── φ̂ (attitude filter) ◄────────────────────────────────────┘
```

**Bandwidth separation rule:** the outer loop crossover should be 5–10× lower than the inner one.

- Inner (rate) crossover: f_c,r ≈ 40–60 Hz. This is limited by gyro LPF phase lag, PWM/FOC delay, and frame resonance.
- Outer (angle) crossover: f_c,a ≈ 6–10 Hz.

**Rationale for the split:**
- The rate loop rejects torque disturbances (cable drag, cogging, wind) directly from the gyro, which has a low-delay signal.
- The angle loop only has to shape a well-behaved, near-integrator inner plant.

---

## 3. Outer Loop: Angle Controller (P + small I)

### Error with wrap-around

Roll stays within ±90° in normal use, but wrapping guards against flips:

e_a[k] = wrap_π( φ_ref[k] − φ̂[k] )

### Control law

ω_cmd[k] = K_pa·e_a[k] + I_a[k] + ω_ff[k],  then  ω_cmd ← clamp(ω_cmd, ±ω_max)

- **Feedforward:** ω_ff = dφ_ref/dt from the setpoint trajectory (e.g., the joystick slew generator). This removes lag in follow mode.
- **No D term in the outer loop.** The inner rate loop already provides damping. A D term on angle would duplicate the gyro feedback with more noise.
- **Small integral, Ki_a:** removes residual steady-state error from IMU mounting or estimator bias. Typically Ki_a ≈ K_pa·(2π·0.5 Hz).

### Starting values

- K_pa ≈ 2π·f_c,a ≈ 40–60 (rad/s per rad)
- ω_max ≈ 3–6 rad/s for smooth camera motion

---

## 4. Inner Loop: Rate PID

### Error

e_r[k] = ω_cmd[k] − ω_meas[k],  where ω_meas = gyro_x − bias_x

### Control law

u[k] = P[k] + I[k] + D[k]

| Term | Equation |
|---|---|
| P | P[k] = K_p·( β·ω_cmd[k] − ω_meas[k] ), with β ≈ 1 (setpoint weighting, optional) |
| I | see §6 (with anti-windup) |
| D | see §5 (filtered, on measurement) |

### Starting gain derivation (normalized plant)

The plant is G(s) ≈ K_t / (J·s), a pure integrator once friction is small.

1. For crossover ω_c: **K_p = ω_c·J / K_t**.
2. Place the integral corner a decade below crossover: **K_i = K_p·ω_c/10**.
3. Use K_d only to add phase near crossover against delay/resonance. Typically **K_d ≈ K_p / (5·ω_c)** or zero.

**Practical tuning order:**
1. Set K_i = K_d = 0.
2. Raise K_p until a buzz or oscillation appears, then back off 40–50%.
3. Add K_d to damp the step overshoot.
4. Add K_i until hold-stiffness against a finger push is acceptable.

---

## 5. Derivative Filtering

### Why the filter is needed

A pure derivative amplifies gyro noise. At 1 kHz, gyro noise of about 0.01 rad/s differentiated gives roughly 10 rad/s² of jitter, which shows up as motor hiss and heating.

The fix has two parts:
- Take the derivative **on measurement**, which avoids setpoint "kick".
- **Band-limit** it with a first-order low-pass.

### Continuous form

D(s) = −K_d · s / (1 + T_f·s) · Ω_meas(s),  with T_f = 1/(2π f_d)

### Choosing f_d

- f_d ≈ 3–5× the rate crossover, i.e., 150–250 Hz.
- It must stay well below Nyquist (500 Hz).
- With f_c,r = 50 Hz, use f_d = 200 Hz, giving T_f = 0.796 ms.

### Discrete form (backward Euler, unconditionally stable)

D[k] = α·D[k−1] − (K_d / (T_f + T_s))·(ω_meas[k] − ω_meas[k−1])

α = T_f / (T_f + T_s)

**Example:** T_s = 1 ms, T_f = 0.796 ms gives α = 0.443.

### Tustin alternative (better phase match near f_d)

D[k] = ((2T_f − T_s)/(2T_f + T_s))·D[k−1] − (2K_d/(2T_f + T_s))·(ω_meas[k] − ω_meas[k−1])

Tustin requires 2T_f > T_s; otherwise the pole goes negative and the output rings. **Prefer backward Euler when f_d is close to f_s/4.**

### Recommended pre-filtering of the gyro

- Configure the IMU's internal DLPF at about 250–500 Hz.
- Optionally add a software biquad notch at any measured frame resonance.
- Keep the total phase lag at f_c,r under about 20°.

---

## 6. Anti-Windup Clamping

When u saturates at ±U_max (motor voltage limit, hard stop, or large disturbance), the integrator would keep growing. This causes large overshoot when the gimbal is released. Three combined mechanisms prevent it.

### (a) Hard integrator bounds

I[k] ∈ [ I_min, I_max ],  with I_max = −I_min ≈ 0.5–0.7·U_max

This guarantees the P and D terms always keep some authority.

### (b) Conditional integration (clamping)

1. Compute the unsaturated and saturated outputs:
   - u_unsat = P + I[k−1] + D
   - u_sat = clamp(u_unsat, −U_max, +U_max)
2. Decide whether integration is allowed:
   - saturated = (u_unsat ≠ u_sat)
   - same_sign = sign(e_r) == sign(u_unsat)
   - **integrate only if ¬(saturated ∧ same_sign)**
3. If allowed: I[k] = clamp( I[k−1] + K_i·T_s·e_r[k], I_min, I_max ). Otherwise: I[k] = I[k−1] (frozen).

In words: freeze the integrator only when it would push the output deeper into saturation. It may still integrate back out.

### (c) Back-calculation (optional, smoother than pure clamping)

I[k] = I[k−1] + K_i·T_s·e_r[k] + (T_s/T_t)·(u_sat[k] − u_unsat[k])

- T_t is the tracking time constant, with T_t ≈ √(T_i·T_d), or T_t ≈ T_i if there is no D term.
- T_i = K_p/K_i and T_d = K_d/K_p.
- Use **either** (b) **or** (c), plus the hard bounds (a).

### Applying this to the outer loop

The outer loop's saturation is the ω_max clamp. Apply the same conditional-integration rule there.

Additionally, if the **inner** loop is saturated, freeze the outer integrator too. This prevents cascade windup, where the outer loop keeps demanding rate the inner loop cannot deliver.

---

## 7. ESP32-