# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis (Cycle 9)

> **Note:** I don't have your Cycle 1–8 artifacts (hardware choices, plant ID, prior gains). The design below uses explicit, commonly valid assumptions. Swap in your own numbers wherever a parameter is marked ⚙.

---

## 1. Assumptions and Architecture

| Item | Assumed value ⚙ |
|---|---|
| Actuator | Gimbal BLDC (high-pole, ~10 Ω), voltage-mode FOC, command = normalized `Uq ∈ [-1, 1]` |
| IMU | 6-axis gyro/accel on camera plate (e.g., ICM-42688), gyro ODR ≥ 2 kHz |
| Attitude estimate | Mahony/complementary filter, provides roll angle φ̂ |
| Inner (rate) loop | **1 kHz**, Ts_i = 1 ms |
| Outer (angle) loop | **250 Hz**, Ts_o = 4 ms (every 4th inner tick) |
| Roll travel | ±45° mechanical; no ±180° wrap handling needed |

```
 φ_ref ──►(+)──► ANGLE PI ──►[clamp ±ω_max]──►(+)──► RATE PID ──►[sat ±U_max]──► FOC ──► Motor ─► Roll plant
          ▲ -        ▲                   ff: φ̇_ref ─┘  ▲ -       │                                 │
          │          └──── sat flag ◄────────────────────┼────────┘                                 │
          φ̂ ◄──────── Mahony ◄─── accel + gyro ◄──────────┴── ω_x (filtered gyro) ◄─────────────────┘
```

**Bandwidth separation rule:** the inner crossover should be at least 5× the outer one.
- Inner rate loop: ≈ 40 Hz.
- Outer angle loop: ≈ 6–8 Hz.

---

## 2. Outer Loop: Angle Controller (PI + rate feed-forward)

The angle loop only needs P plus a small I, which removes steady offset from residual imbalance or cable drag. The D action effectively already lives in the rate loop.

```
e_φ[k]     = φ_ref[k] − φ̂[k]
ω_unsat[k] = Kp_a·e_φ[k] + I_a[k] + Kff·φ̇_ref[k]
ω_ref[k]   = sat(ω_unsat[k], ±ω_max)          ω_max ⚙ = 300 °/s
```

**Integrator update (conditional, see §5):**
```
I_a[k+1] = clamp( I_a[k] + Ki_a·Ts_o·e_φ[k] , ±I_a,max )   only if allowed
```

Notes:
- `φ̇_ref` is the derivative of the smoothed setpoint trajectory (from your joystick/follow-mode shaper), not of the raw stick.
- Set `Kff ≈ 1` so tracking does not rely on error.

---

## 3. Inner Loop: Rate Controller (PID, D-on-measurement)

```
e_ω[k]   = ω_ref[k] − ω_f[k]                       (ω_f = filtered gyro X, rad/s)
P[k]     = Kp_r · e_ω[k]
I[k]     : see §5
D[k]     : see §4 (on measurement −ω_f, so setpoint steps don't kick)
u_unsat  = P[k] + I[k] + D[k] + Kg·sin(φ̂)          (optional gravity/imbalance FF ⚙)
u[k]     = sat(u_unsat, ±U_max)                    U_max ⚙ = 0.8 (thermal/voltage headroom)
```

**Gyro pre-filtering (before the PID):**
- Use a 2nd-order Butterworth biquad LPF at about 150 Hz.
- Add a notch at any measured frame/motor resonance (identify it from a gyro FFT log).

---

## 4. Derivative Filtering

The ideal derivative is replaced with a band-limited one:

$$
D(s) = -K_d\,\frac{s}{\tau s + 1}\,Y(s), \qquad \tau = \frac{1}{2\pi f_d}
$$

**Choosing the cutoff `f_d`:**
- It should be 5–10× the inner crossover, and below roughly ¼ of the sample rate.
- At 1 kHz with 40 Hz crossover, use **f_d ≈ 80–150 Hz** (τ ≈ 1.1–2.0 ms).
- Equivalent "N" form: τ = K_d / (N·K_p), with N ≈ 8–20.

### 4a. Tustin (bilinear) discretization (recommended)

$$
D[k] = \underbrace{\frac{2\tau - T_s}{2\tau + T_s}}_{a_d}\,D[k-1] \;-\; \underbrace{\frac{2K_d}{2\tau + T_s}}_{b_d}\,\big(y[k] - y[k-1]\big)
$$

### 4b. Backward Euler (always stable, slightly more lag)

$$
D[k] = \frac{\tau}{\tau + T_s}\,D[k-1] \;-\; \frac{K_d}{\tau + T_s}\,\big(y[k] - y[k-1]\big)
$$

**Requirement for Tustin:** keep `2τ > Ts` so that `a_d > 0`. Otherwise the filter rings at Nyquist. At Ts = 1 ms, this means f_d < 318 Hz.

**Why D-on-measurement:** the derivative of `e_ω` would differentiate ω_ref steps from the outer loop and cause torque spikes. Using −ω_f removes this. Here the D term is effectively angular-acceleration feedback, which adds damping against plate resonance.

---

## 5. Anti-Windup Clamping

Saturation happens in practice from fast pans, hand shocks, and hitting the mechanical stop. Two mechanisms are combined.

### 5a. Conditional integration (clamping), primary method

Store the integrator **already multiplied by Ki**. This keeps gain changes bumpless.

```
sat_hi  = (u_unsat >  U_max)
sat_lo  = (u_unsat < -U_max)
pushing = (sat_hi && e_ω > 0) || (sat_lo && e_ω < 0)

if (!pushing):
    I[k+1] = I[k] + Ki_r · Ts · e_ω[k]
else:
    I[k+1] = I[k]                    # freeze: integrating would deepen saturation

I[k+1] = clamp(I[k+1], −I_max, +I_max)    # hard bound, I_max ⚙ ≈ 0.5·U_max
```

In words: integrate unless the output is saturated **and** the error would push it further into saturation. Integration resumes the moment the error changes sign.

### 5b. Back-calculation (optional alternative/supplement)

$$
I[k+1] = I[k] + T_s\Big(K_i\,e[k] + \frac{1}{T_t}\big(u[k] - u_{unsat}[k]\big)\Big),
\qquad T_t \approx \sqrt{T_i T_d}\;\;(\text{or } \approx T_i/2 \text{ if } K_d=0)
$$

When the output is not saturated, `u − u_unsat = 0`, so the extra term vanishes. During saturation it bleeds the integrator back toward the consistent value. This gives smoother recovery than a hard freeze but needs a well-chosen `T_t`.

### 5c. Cascade-level anti-windup (essential)

When the **inner** loop saturates, the outer integrator must also stop. Otherwise the outer loop keeps asking for more rate that can't be delivered.

```
outer_hold = inner_saturated
          || (ω_unsat > +ω_max && e_φ > 0)
          || (ω_unsat < −ω_max && e_φ < 0)
if (!outer_hold): I_a += Ki_a·Ts_o·e_φ
I_a = clamp(I_a, ±I_a,max)
```

Also reset (or preload) both integrators on motor enable or