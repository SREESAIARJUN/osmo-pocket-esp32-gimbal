# Cascaded PID Design: ESP32-S3 Gimbal, Roll Axis

**Note on "Cycle 27":** I don't have your project history, so I can't see what earlier cycles specified. This design uses the assumptions below. Swap in your actual hardware values where they differ.

| Item | Assumption |
|---|---|
| Actuator | Gimbal BLDC (~7–12 pole pairs), SimpleFOC-style voltage or current FOC |
| Sensors | 6-axis IMU (e.g., ICM-42688-P) for gyro rate and fused angle. Optional magnetic encoder (AS5600/MT6701) for commutation. |
| MCU | ESP32-S3 at 240 MHz. It has a single-precision FPU, so use `float`, never `double`. |
| Output | Roll torque command, as voltage `Uq` or current `Iq`, limited to ±U_max |

---

## 1. Loop Architecture & Timing

```
θ_ref ─►(+)─► [Angle PI] ─►(+)─► ω_ref ─► clamp ─►(+)─► [Rate PID] ─►(+)─► clamp ─► Uq/Iq ─► FOC ─► Motor
         ▲ -                 ▲ ω_ff               ▲ -                 ▲ τ_ff
         │                                        │
     θ_meas (fusion)                         ω_meas (gyro, filtered)
```

| Loop | Rate | Ts | Target bandwidth |
|---|---|---|---|
| FOC current/commutation | 10–20 kHz | — | > 500 Hz |
| **Rate (inner)** | **2 kHz** | 500 µs | 40–80 Hz |
| **Angle (outer)** | **500 Hz** (every 4th rate tick) | 2 ms | 5–12 Hz |

**Rule:** keep outer-loop bandwidth at least 5× lower than inner-loop bandwidth, so the outer loop sees the rate loop as roughly unity gain.

**ESP32-S3 placement:**
- Pin the control task to **core 1**. Leave Wi-Fi/BLE on core 0.
- Trigger it from a GPTimer ISR that notifies the task, or from the IMU data-ready interrupt (preferred, because it is synchronous with the sample).
- Read the IMU over SPI with DMA.

---

## 2. Outer Loop: Angle PI (+ feed-forward)

Derivative action is usually unnecessary here, because the rate loop already supplies the damping.

**Error with wrap-around:**
```
e_θ[k] = wrap_π( θ_ref[k] − θ_meas[k] )        wrap_π(x) = atan2(sin x, cos x)
```

**Control law:**
```
P_θ[k] = Kp_θ · e_θ[k]
I_θ[k] = I_θ[k−1] + Ki_θ · Ts_θ · e_θ[k]           (gated, see §4)
ω_ref_unsat[k] = P_θ[k] + I_θ[k] + ω_ff[k]
ω_ref[k] = clamp(ω_ref_unsat[k], −ω_max, +ω_max)    e.g. ω_max = 6 rad/s (~350°/s)
```

**Feed-forward:** `ω_ff = dθ_ref/dt` from the setpoint trajectory. This removes most of the tracking lag in follow mode.

**Setpoint shaping (optional but recommended):** slew-limit θ_ref to `|Δθ_ref| ≤ ω_max·Ts_θ`.

---

## 3. Inner Loop: Rate PID with Filtered Derivative

**Error:**
```
e_ω[k] = ω_ref[k] − ω_meas[k]
```

### 3.1 Proportional (with setpoint weight b)
```
P_ω[k] = Kp_ω · ( b·ω_ref[k] − ω_meas[k] )        b = 0.7–1.0
```

### 3.2 Filtered derivative, taken on measurement

Take the derivative of the *measurement*, not the error. This avoids a derivative kick when ω_ref steps.

The continuous form is a band-limited differentiator:
```
D(s) = − Kd · s / (Tf·s + 1) · ω_meas(s)
Tf = 1 / (2π·f_D)            f_D = derivative cutoff
```

The derivative of gyro rate is angular acceleration, which is very noisy. Choose f_D using these guidelines:
- f_D ≈ 3–5× the rate-loop bandwidth. With a ~60 Hz loop, f_D ≈ 150–250 Hz.
- f_D must be below Nyquist (1 kHz).
- Equivalently, Tf = Kd/(N·Kp) with N ≈ 5–20.

**Tustin (bilinear) discretization**, which is preferred because it preserves phase:
```
α = (2Tf − Ts) / (2Tf + Ts)
β = 2·Kd / (2Tf + Ts)

D_ω[k] = α · D_ω[k−1] − β · ( ω_meas[k] − ω_meas[k−1] )
```

**Backward-Euler alternative** (always stable, slightly more lag):
```
α = Tf / (Tf + Ts)
β = Kd / (Tf + Ts)
D_ω[k] = α · D_ω[k−1] − β · ( ω_meas[k] − ω_meas[k−1] )
```

**Example:** Ts = 500 µs and f_D = 200 Hz give Tf = 796 µs, so Tustin α = 0.522.

**Gyro pre-filtering** happens before the PID and also feeds the P term:
- 2nd-order Butterworth LPF on ω_meas at ~250–300 Hz. Alternatively, use the IMU's internal DLPF.
- Optional notch filters at structural resonance or motor commutation frequencies. Find these with an FFT of logged gyro data.
- Count the total phase lag of every filter in the loop. Each one erodes phase margin.

### 3.3 Integral
```
I_ω[k] = I_ω[k−1] + Ki_ω · Ts · e_ω[k]      (with anti-windup, §4)
```

### 3.4 Output
```
u_unsat[k] = P_ω[k] + I_ω[k] + D_ω[k] + τ_ff[k]
u[k]       = clamp(u_unsat[k], −U_max, +U_max)
```

`τ_ff` is optional feed-forward. Typical sources:
- Cogging compensation from an encoder-indexed lookup table.
- Gravity/imbalance compensation, `k_g·sin θ`.
- Inertia compensation, `J·dω_ref/dt`.

---

## 4. Anti-Windup Equations

Apply all three layers. They are cheap and complement each other.

### 4.1 Hard integrator clamp (always on)
```
I[k] = clamp( I[k], I_min, I_max )
```
- Rate loop: `I_max = 0.5–0.7 · U_max`. Leave headroom for P and D.
- Angle loop: `I_max_θ = 0.3 · ω_max`.

### 4.2 Conditional integration (clamping method)
Freeze the integrator when the output is saturated *and* the error would push it further into saturation:
```
sat     = ( u_unsat[k] ≠ u[k] )
same    = sign(e[k]) == sign(u_unsat[k])
if (sat && same)  →  I[k] = I[k−1]          // freeze
else              →  I[k] = I[k−1] + Ki·Ts·e[k]
```

### 4.3 Back-calculation (recommended for the rate loop)
Bleed the integrator using the saturation excess:
```
I[k] = I[k−1] + Ts · [ Ki·e[k] + (u[k] − u_unsat[k]) / Tt ]
```
- Tracking time constant: `Tt = √(Ti·Td)`, where `Ti = Kp/Ki` and `Td = Kd/Kp`.
- If Kd = 0, use `Tt ≈ 0.5·Ti`.
- Smaller Tt unwinds faster but is more sensitive to noise.

### 4.4 Cascade-specific windup
When the **inner loop saturates**, the outer loop must stop integrating too. Otherwise the angle integrator winds up against a rate loop that cannot respond. Propagate the inner saturation flag outward:
```
outer_freeze = inner_sat || (ω