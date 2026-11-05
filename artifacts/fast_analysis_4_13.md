# Cascaded PID Design for an ESP32-S3 Gimbal Roll Axis

I don't have the specifics of your "Cycle 4" (motor, IMU, prior tuning results). This design states its assumptions explicitly so you can substitute your values. The structure, filter equations and anti-windup logic carry over unchanged.

---

## 1. Assumptions

| Item | Assumed value | Notes |
|---|---|---|
| Motor | Gimbal BLDC (e.g., 2804 class, 7 pole pairs) | High-resistance winding, voltage-mode FOC |
| Driver | MCPWM, 3-phase, 20–25 kHz PWM | ESP32-S3 MCPWM peripheral |
| Motor feedback | AS5600 / AS5048A / MT6701 magnetic encoder | Used for FOC electrical angle |
| IMU | ICM-42688-P or BMI270 over SPI | Gyro ODR ≥ 1 kHz, data-ready interrupt |
| Attitude | Complementary or Mahony filter at 1 kHz | Provides roll angle φ |
| Inner (rate) loop | **1 kHz** (Ts = 1 ms) | Output: q-axis voltage command Vq |
| Outer (angle) loop | **500 Hz** (Ts = 2 ms), or 1 kHz decimated | Output: rate setpoint ω* |
| Supply | 12 V, Vq limit ±6 V (≈ Vbus/2) | Limits the I²R heating of the gimbal motor |

---

## 2. Control Architecture

```
          φ_ref ──►(+)──► [Angle PI(D)] ──► ω*_sat ──►(+)──► [Rate PID] ──► Vq_sat ──► FOC ──► Motor ──► Roll axis
                    ▲ −        │  rate limit           ▲ −       │  voltage clamp                        │
                    │          │                       │         │                                        │
                    │          └──── sat flag ◄────────┼─────────┘ (inner saturation propagates outward)  │
                    │                                  │                                                  │
                    └─────── φ (attitude filter) ◄─────┴────── ω_gyro,x (LPF'd) ◄──────── IMU ◄───────────┘
```

**Design rules**
- **Bandwidth separation:** outer loop bandwidth should be about **1/5 to 1/10** of the inner loop's.
  - Target inner (rate) crossover is roughly 40–80 Hz.
  - Target outer (angle) crossover is roughly 5–15 Hz.
- **Rate loop feedback:** use the **gyro directly**, not a differentiated angle.
- **D-term placement:**
  - The outer loop is usually **P or PI only**, because the inner rate loop already supplies damping.
  - The inner loop carries the D term, which acts on angular acceleration.
- **Derivative on measurement:** the D term acts on the measurement, not the error. This prevents a derivative kick on setpoint steps when the user commands a new roll angle.

---

## 3. Outer Loop: Angle Controller

### Error with wrap-around
```
e_φ[k] = wrap(φ_ref[k] − φ[k]),   wrap(x) = atan2(sin x, cos x)  ∈ (−π, π]
```
For a roll axis with limited travel (±45°), wrapping matters less. Keep it anyway for robustness.

### Control law (PI with optional feed-forward)
```
P_o[k]   = Kp_φ · e_φ[k]
I_o[k]   = I_o[k−1] + Ki_φ · Ts_o · e_φ[k]          (gated, see §5)
ω*_raw   = P_o[k] + I_o[k] + ω_ff[k]                 (ω_ff = dφ_ref/dt, from the trajectory)
ω*[k]    = sat(ω*_raw, ±ω_max)                       ω_max ≈ 3–6 rad/s for smooth footage
```

The outer integrator is optional. It mainly removes steady-state error caused by:
- attitude filter bias,
- residual imbalance not handled by the inner loop's integrator.

Many gimbals run **P-only** outside and **PI(D)** inside.

---

## 4. Inner Loop: Rate Controller with Filtered Derivative

### Continuous form (2-DOF, derivative on measurement)
```
Vq(s) = Kp·E(s) + (Ki/s)·E(s) − Kd·s/(Tf·s + 1) · Ω(s) + Vff
E(s)  = Ω*(s) − Ω(s)
```

### Gyro pre-filter (before the PID)
Apply a 2nd-order Butterworth LPF to ω_gyro,x, at about 150–250 Hz for 1 kHz sampling. Add a notch filter if you see a motor/frame resonance; find it with an FFT of logged gyro data.

### Derivative filter discretization

Define the time constant from a cutoff frequency f_c:
```
Tf = 1 / (2π · f_c)        typical f_c = 30–80 Hz for a 1 kHz loop
                           (the D term differentiates gyro → angular acceleration: very noisy)
```

**Option A: Backward Euler.** Unconditionally stable and simple.
```
D[k] = α · D[k−1] − β · (ω[k] − ω[k−1])

α = Tf / (Tf + Ts)
β = Kd / (Tf + Ts)
```

**Option B: Tustin (bilinear).** Better phase match near f_c, but requires Ts < 2·Tf.
```
D[k] = ((2Tf − Ts)/(2Tf + Ts)) · D[k−1] − (2Kd/(2Tf + Ts)) · (ω[k] − ω[k−1])
```

The minus sign comes from differentiating the measurement: d(e)/dt ≈ −dω/dt when ω* is piecewise constant.

**Equivalent "N" form.** This is the textbook parameterization:
```
Tf = Kd / (Kp · N)      N typically 5–20
```

### Proportional, feed-forward, output
```
P[k]   = Kp · e[k]
Vff[k] = Kff_ω · ω*[k] + Kg · sin(φ[k])       (back-EMF / viscous term + gravity-imbalance term)
U_raw  = P[k] + I[k] + D[k] + Vff[k]
Vq[k]  = sat(U_raw, ±Vq_max)
```

The `Kg·sin(φ)` term compensates residual pendulum imbalance on roll. Identify it by holding several angles and recording the steady-state integrator value. Feed-forward reduces how much the integrator has to work, which in turn reduces windup risk.

---

## 5. Anti-Windup

Two methods are given below. **Use clamping (5.1) as the primary method.** Back-calculation (5.2) is an alternative with smoother recovery.

### 5.1 Conditional integration (clamping)

```
Saturated  := (U_raw ≠ Vq[k])                     // i.e., |U_raw| > Vq_max
SameSign   := sign(e[k]) == sign(U_raw)

if (Saturated AND SameSign):
    I[k] = I[k−1]                                  // freeze: integrating would push deeper into saturation
else:
    I[k] = I[k−1] + Ki · Ts · e[k]

I[k] = sat(I[k], I_min, I_max)                     // hard bound, e.g. ±0.6·Vq_max
```

The integrator keeps integrating **when the error would pull the output out of saturation**. This avoids the sluggish recovery that naive freezing causes.

**Order of operations matters:**
1. Compute U_raw using the **previous** I.
2. Evaluate the saturation condition.
3. Then update I.

### 5.2 Back-calculation (alternative)
```
I[k] = I[k−1] + Ts · ( Ki · e[k] + (Vq[k] − U_raw) / Tt )

Tt (tracking time constant) ≈ sqrt(Ti · Td),  Ti = Kp/Ki,  Td = Kd/Kp
                           ≈ Ti  if there is no D term
```

When unsaturated, (Vq − U_raw) = 0, so this is the ordinary