# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have the earlier design cycles, so I can't see what "Cycle 29" fixed (motor, IMU, loop rates, gain units). I've stated the assumptions below. Replace any that differ from your build; the structure and equations still apply.

---

## 1. Assumptions

| Item | Assumed value |
|---|---|
| Actuator | Gimbal BLDC (e.g. GBM2804, 7 pole pairs), FOC in voltage-torque mode (Uq command) |
| Driver | 3-PWM at 20–25 kHz, LEDC/MCPWM on ESP32-S3 |
| Sensors | IMU gyro over SPI at ≥2 kHz (e.g. ICM-42688); roll angle from a Mahony/complementary filter; motor encoder (e.g. AS5600/MT6701) for commutation |
| Inner (rate) loop | 2 kHz, Ts_r = 0.5 ms |
| Outer (angle) loop | 500 Hz, Ts_a = 2 ms (runs every 4th rate tick) |
| Output limit | Uq ∈ [−U_max, +U_max], with U_max ≈ 0.5·V_bus·(1/√3) margin |

---

## 2. Loop Architecture

```
θ_sp ──►(Σ)──► Angle P(I) ──► ω_ff + ω_cmd ──► clamp ±ω_max ──►(Σ)──► Rate PID ──► clamp ±U_max ──► FOC Uq ──► Motor/Platform
         ▲ −                                                   ▲ −
         │                                                     │
       θ_roll (Mahony, 500 Hz)                          ω_gyro_x (LPF/notch, 2 kHz)
```

The two loops have separate jobs:

- **Inner rate loop.** It feeds back the gyro directly, so it has the least sensor lag. This loop rejects disturbances: handle shake and cable torque.
- **Outer angle loop.** It mostly uses P. I is optional and small. It outputs a rate setpoint, and the setpoint rate dθ_sp/dt is added as feedforward so that commanded pans don't lag.
- **Bandwidth separation.** Make the inner bandwidth 5–10× the outer. Typical targets are 40–60 Hz inner and 6–10 Hz outer.

---

## 3. Inner Loop: Rate PID (2 kHz)

### 3.1 Control law
Error: e_ω[k] = ω_sp[k] − ω[k]

The derivative acts on the measurement, not the error. This avoids a derivative kick when the outer loop steps ω_sp.

u_unsat[k] = P[k] + I[k] + D[k]

- P[k] = K_p · e_ω[k]
- I[k]: see the anti-windup section (3.3)
- D[k]: filtered derivative of −ω (section 3.2)

### 3.2 Derivative filtering
The raw derivative is d/dt of the gyro signal, which is effectively angular acceleration. It amplifies gyro noise and motor PWM/commutation ripple, so it must be low-passed.

Use a first-order filter on the D term, with time constant τ_d = 1/(2π f_d).

**Option A: backward-Euler (simple, robust)**

α = τ_d / (τ_d + Ts)

D[k] = α · D[k−1] − K_d · (1 − α) · (ω[k] − ω[k−1]) / Ts

**Option B: Tustin / bilinear (better phase near f_d)**

D[k] = [(2τ_d − Ts)/(2τ_d + Ts)] · D[k−1] − [2K_d/(2τ_d + Ts)] · (ω[k] − ω[k−1])

Recommended settings:

- **D-filter cutoff.** Set f_d = 80–150 Hz. Start at 100 Hz, where τ_d = 1.59 ms and α ≈ 0.761 at Ts = 0.5 ms.
- **Gyro pre-filter.** Before the PID, low-pass the gyro with a 2nd-order Butterworth biquad at ~150–250 Hz.
- **Notch.** If the frame has a structural resonance, add a notch at that frequency. Find it by logging an FFT of ω with the loop running at low gain.
- **Phase budget.** Every filter adds phase lag. At the crossover frequency f_c, total lag from the filters plus the 1–2 sample delay should stay under about 30°.
  - A rule of thumb: keep f_d ≥ 3·f_c and the gyro LPF ≥ 4·f_c.

### 3.3 Anti-windup
Saturation happens on the Uq voltage limit. Use **clamping with conditional integration** as the primary method, plus a hard integrator bound. Back-calculation is given as an alternative.

**Step 1: tentative integral (trapezoidal)**

I* = I[k−1] + K_i · Ts · (e[k] + e[k−1]) / 2

**Step 2: saturate the total output**

u_unsat = P + I* + D

u_sat = clamp(u_unsat, −U_max, +U_max)

**Step 3: conditional integration (clamping)**

Freeze the integrator when the output is saturated and the error would push further into saturation:

```
if (u_unsat ≠ u_sat) AND (sign(e[k]) == sign(u_unsat)):
    I[k] = I[k−1]        // freeze
else:
    I[k] = I*
```

**Step 4: hard bound (safety net)**

I[k] = clamp(I[k], −I_max, +I_max)

Set I_max ≈ 0.6·U_max. This leaves P and D authority even when I is full.

**Alternative: back-calculation**

Use this if you prefer smoother recovery from long saturation:

I[k] = I[k−1] + K_i·Ts·e[k] + (Ts / T_t)·(u_sat − u_unsat)

- Choose the tracking time T_t ≈ √(T_i·T_d), with T_i = K_p/K_i and T_d = K_d/K_p.
- If K_d = 0, use T_t ≈ 0.5·T_i.
- A smaller T_t unwinds faster but is more sensitive to noise.

### 3.4 Output
Uq = u_sat + U_cog_ff(θ_e)

The cogging feedforward U_cog_ff is optional: a lookup table indexed by electrical angle. It helps a lot with smoothness at low rates.

---

## 4. Outer Loop: Angle P(I) (500 Hz)

### 4.1 Error with wrap
Roll is normally within ±45°. Still, wrap the error so the loop never takes the long way around:

e_θ = wrap_π(θ_sp − θ) = atan2(sin(Δ), cos(Δ))

You can also use the cheaper fmodf-based equivalent.

### 4.2 Control law
ω_cmd = K_pa · e_θ + I_a + ω_ff

- **Feedforward:** ω_ff = dθ_sp/dt. Low-pass it at ~20 Hz if the setpoint comes from RC or the joystick.
- **D term:** none is needed, because the inner loop already provides damping.
  - If you add one, take it from the gyro (−K_da·ω). Never differentiate the fused angle.
- **Integrator:** I_a handles residual static error only, for example a slight imbalance plus the inner P-only behavior. Keep K_ia small, about K_pa·(0.5…1 s⁻¹).

### 4.3 Rate limit and anti-windup
ω_sp = clamp(ω_cmd, −ω_max, +ω_max)

ω_max is typically 3–6 rad/s, which keeps follow-mode moves smooth.

Apply the same conditional-integration rule as the rate loop, triggered by:

- the rate clamp ω_cmd ≠ ω_sp, **or**
- the inner loop reporting saturation (`rate.saturated == true`).

The second trigger matters in a cascade. The outer integrator must stop when the inner loop is out of authority, or the outer loop winds up through the inner one.

---

## 5. Starting Gains and Tuning Order

These units assume Uq in volts, ω in rad/s, and θ in rad. They are starting points for a balanced ~200 g camera on a 2804 motor at 12 V.

| Parameter | Start | Notes |
|---|---|---|
| Rate K_p | 0.05 V/(rad/s) | Raise until oscillation, then back off 40% |
| Rate K_i | 1.0 V/rad | Equivalent to T_i ≈ 50 ms |
| Rate K_d | 0.0005 V/(rad/s²) | Add last, only if oversh