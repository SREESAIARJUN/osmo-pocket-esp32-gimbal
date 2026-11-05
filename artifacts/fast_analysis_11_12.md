# Cascaded PID Design: Gimbal Roll Axis (ESP32-S3)

> **Note:** I don't have your project's "Cycle 11" spec. The structure and equations below are standard and correct, but the numeric values (rates, gains, limits) are assumptions. Swap in your actual motor, IMU, and loop-rate figures. The assumptions are listed in §1 so you can check them quickly.

---

## 1. Assumed System

| Item | Assumption |
|---|---|
| Actuator | Gimbal BLDC (e.g., 2204/2804 class), FOC driver, command = q-axis voltage `Uq` or current `Iq` |
| Sensor | 6-axis IMU (ICM-42688 / MPU-6500 class), gyro ODR ≥ 1 kHz, SPI |
| Attitude | Complementary or Mahony filter giving roll angle φ |
| Inner (rate) loop | **1 kHz**, Ts_i = 1 ms |
| Outer (angle) loop | **250 Hz**, Ts_o = 4 ms (runs every 4th inner tick) |
| Output limit | ±U_max (e.g., ±6 V on a 12 V bus, or a current limit) |
| Units | rad, rad/s internally |

---

## 2. Architecture

```
 φ_ref ──►(+)──► [Angle P(+I)] ──► ω_cmd ──►(+)──► [Rate PID] ──► sat ──► Uq ──► FOC ──► Motor/Roll
     ▲    ─▲                  ▲  clamp ±ω_max  ▲ ─▲                │
     │     │                  │                │  │                │
     │     φ (attitude est.)  └─ ω_ff (opt.)   │  ω (filtered gyro)│
     │                                         │                   │
     └──────────── inner_saturated flag ◄──────┴───────────────────┘
```

Design rules:
- **Bandwidth separation:** outer crossover ≈ 1/5 to 1/10 of inner. Target inner ≈ 30–50 Hz and outer ≈ 5–8 Hz.
- The **inner loop does all damping**. The outer loop is P, or PI if steady-state roll offset matters (cable drag, imbalance).
- The **D term exists only in the inner loop**. The gyro already supplies the rate, which acts as the outer loop's derivative.

---

## 3. Outer Loop: Angle Controller

### 3.1 Error with wrap

```
e_φ[k] = wrap_π( φ_ref[k] − φ[k] ),    wrap_π(x) = atan2(sin x, cos x)
```

Wrapping matters if roll can approach ±180°. Otherwise it is a cheap safety measure.

### 3.2 Control law (PI + feedforward)

```
ω_cmd_raw[k] = Kp_o · e_φ[k] + I_o[k] + ω_ff[k]
ω_cmd[k]     = clamp( ω_cmd_raw[k], −ω_max, +ω_max )
```

- `ω_ff = dφ_ref/dt` is the setpoint rate, taken from the trajectory generator or a smoothed difference of φ_ref. It improves tracking without adding gain.
- `ω_max` is typically 3–6 rad/s for a camera gimbal.

### 3.3 Outer anti-windup (clamping)

The outer integrator must freeze if **either** its own output clamps **or** the inner loop is saturated. Otherwise it winds up while the motor physically cannot respond.

```
sat_o   = (ω_cmd_raw ≠ ω_cmd)
pushing = sign(e_φ) == sign(ω_cmd_raw)

if NOT ((sat_o OR inner_saturated) AND pushing):
    I_o[k+1] = I_o[k] + Ki_o · Ts_o · e_φ[k]
I_o[k+1] = clamp( I_o[k+1], −I_o_max, +I_o_max )
```

---

## 4. Inner Loop: Rate PID

### 4.1 Gyro pre-filtering

The rate measurement passes through these stages:

1. IMU on-chip DLPF at about 250 Hz. This is the anti-alias stage.
2. Optional software biquad notch at any motor or frame resonance found in an FFT log.
3. Optional 1st-order LPF at about 150 Hz:
   ```
   ω_f[k] = ω_f[k−1] + α_g (ω[k] − ω_f[k−1]),   α_g = Ts / (τ_g + Ts),  τ_g = 1/(2π f_g)
   ```

Every filter adds phase lag at crossover, so keep cutoffs at least 3–5× the inner bandwidth.

### 4.2 Proportional

```
e_ω[k] = ω_cmd[k] − ω_f[k]
P[k]   = Kp_i · e_ω[k]
```

### 4.3 Derivative with filtering

Two design choices apply here:
- **Derivative on measurement** (−ω_f, not e_ω). This avoids a derivative kick when the outer loop steps ω_cmd.
- **First-order low-pass on D.** The derivative of gyro rate is angular acceleration, which is very noisy.

The continuous form is:
```
D(s) = −Kd · s / (τ_d·s + 1) · ω(s),     τ_d = 1/(2π f_d)   (or τ_d = Kd/(Kp·N), N ≈ 5–20)
```

**Backward-Euler discretization** is unconditionally stable for any τ_d. Use this one:
```
D[k] = (τ_d / (τ_d + Ts)) · D[k−1]  −  (Kd / (τ_d + Ts)) · (ω_f[k] − ω_f[k−1])
```

The precomputed form for the ISR is:
```
a_d = τ_d / (τ_d + Ts)
b_d = Kd  / (τ_d + Ts)
D[k] = a_d · D[k−1] − b_d · (ω_f[k] − ω_f[k−1])
```

The **Tustin alternative** has better phase match near Nyquist:
```
D[k] = ((2τ_d − Ts)/(2τ_d + Ts)) · D[k−1] − (2Kd/(2τ_d + Ts)) · (ω_f[k] − ω_f[k−1])
```
It requires 2τ_d > Ts; otherwise the pole goes negative and rings.

Typical f_d is 40–100 Hz at 1 kHz sampling. Start at 60 Hz.

### 4.4 Integral with clamping anti-windup

```
u_raw[k] = P[k] + I_i[k] + D[k]
u[k]     = clamp( u_raw[k], −U_max, +U_max )

sat_i    = (u_raw[k] ≠ u[k])
pushing  = sign(e_ω[k]) == sign(u_raw[k])

if NOT (sat_i AND pushing):                 // conditional integration
    I_i[k+1] = I_i[k] + Ki_i · Ts · e_ω[k]
I_i[k+1] = clamp( I_i[k+1], −I_i_max, +I_i_max )   // hard bound, e.g. 0.5·U_max

inner_saturated = sat_i                    // exported to outer loop
```

Integrating `Ki·e` (rather than multiplying the accumulated error by Ki) keeps the integrator output continuous if you retune Ki live.

**Optional back-calculation** is an alternative or addition to conditional integration:
```
I_i[k+1] = I_i[k] + Ki_i·Ts·e_ω[k] + (Ts/T_t)·(u[k] − u_raw[k]),
T_t ≈ sqrt(Ti·Td)   (or ≈ Ti if no D)
```
Clamping is simpler and deterministic, so it is used in the code below.

---

## 5. Starting Gains & Tuning Order

Starting values for a ~200 g payload with Uq output in volts:

| Loop | Kp | Ki | Kd | Filter |
|---|---|---|---|---|
| Rate (inner) | 0.05–0.15 V/(rad/s) | 1–5 V/rad | 0.0005–0.003 V·s²/rad | f_d = 60 Hz |
| Angle (outer) | 15–30 (rad/s)/rad | 0–10 | — | — |

Tuning procedure:
1. **Balance the payload mechanically first.** A gimbal that needs Ki to hold position is unbalanced.
2. Outer loop off. Inject ω_cmd steps of ±1 rad/s.
3. Raise Kp_i until you get slight oscillation, then back off 30–40%.
4. Add Kd_i to damp overshoot. Raise f_d only if D is ineffective. Watch motor temperature and audible hiss, which indicate D noise.
5. Add Ki_i until steady rate error disappears within about 50 ms.
6. Close the outer loop. Raise Kp_o until angle overshoot is about 5–10%. Check that the inner loop is at least 5× faster.
7. Add a small Ki_o only if a static offset remains.
8. Verify with