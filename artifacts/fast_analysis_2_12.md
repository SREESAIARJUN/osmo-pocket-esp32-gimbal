# Cascaded PID Design: ESP32-S3 Gimbal, Roll Axis (Cycle 2)

I don't have your Cycle 1 documents, so this design states its assumptions explicitly. Replace the plant numbers with your measured values. The structure and equations do not depend on them.

---

## 1. Assumptions (substitute your measured values)

| Item | Assumed value | Notes |
|---|---|---|
| Motor | BLDC gimbal motor (2804-class), 7 pole pairs, R ≈ 10 Ω | Driven by FOC in voltage mode |
| Torque constant | K_t ≈ 0.08 N·m/A | Gives τ ≈ K_t·V_q / R |
| Roll inertia (motor + payload) | J ≈ 2.0×10⁻⁴ kg·m² | Measure with a step test (§7) |
| Viscous friction | b ≈ 1×10⁻⁴ N·m·s/rad | Small; treat as disturbance |
| Rate sensor | IMU gyro X-axis, 2 kHz ODR, internal LPF ≈ 200 Hz | Not the encoder: we stabilise in inertial space |
| Angle sensor | IMU attitude estimate (complementary/Mahony) at 1 kHz | Encoder used only for FOC commutation |
| Actuator limit | V_q,max = 0.6 · V_bus/√3 ≈ 4.1 V @ 12 V | Torque limit ≈ 0.033 N·m |
| MCU | ESP32-S3, single-precision FPU | Use `float`, never `double`, in the loop |

---

## 2. Architecture

```
          ┌────────────── Outer ANGLE loop (500 Hz) ─────────────┐   ┌──────── Inner RATE loop (1 kHz) ────────┐
θ_ref ──►(+)─ e_θ ─► PI (+ optional D) ─► ω_cmd ─► rate-limit/clamp ─►(+)─ e_ω ─► PID ─► τ_cmd ─► sat ─► V_q ─► FOC ─► motor
          ▲−                                         ▲ +ω_ff            ▲−                              │
          │                                          │                  │                               │
        θ_meas (attitude filter)            dθ_ref/dt (feedforward)  ω_meas (gyro, LPF)         [J s + b]⁻¹ ─► ω ─► 1/s ─► θ
```

**Loop rates and bandwidths.** Keep at least 5× separation between each layer:

| Layer | Rate | Target crossover |
|---|---|---|
| FOC current/voltage | 10–20 kHz (MCPWM) | > 500 Hz |
| Rate (velocity) loop | 1 kHz (T_s = 1 ms) | f_c,ω ≈ 40 Hz |
| Angle loop | 500 Hz (T_s = 2 ms) | f_c,θ ≈ 8 Hz |

---

## 3. Inner Rate (Velocity) Loop

### 3.1 Plant
$$
G_\omega(s)=\frac{\omega(s)}{\tau(s)}=\frac{1}{Js+b}\approx\frac{1}{Js}
$$

### 3.2 PI gains (loop-shaping)
Set the crossover where |K_p/(Jω_c)| = 1, and place the integral zero a factor of 5 below crossover:

$$
K_{p,\omega}=J\,\omega_{c,\omega},\qquad K_{i,\omega}=K_{p,\omega}\,\frac{\omega_{c,\omega}}{5}
$$

With ω_c = 2π·40 = 251 rad/s:

- K_p,ω = 2e-4 × 251 = **0.050 N·m/(rad/s)**
- K_i,ω = 0.050 × 50.3 = **2.5 N·m/rad**
- Phase margin ≈ 90° − atan(1/5) − (delay ≈ 1.5·T_s·ω_c ≈ 21.6°) ≈ **57°**

### 3.3 Derivative term (optional, use small)
On the rate loop, D acts on angular acceleration. That signal is noisy and amplifies gyro noise and frame resonances. Start with K_d,ω = 0. If you need extra phase lead because of payload compliance, use K_d,ω ≈ 0.1·K_p,ω/ω_c ≈ 2×10⁻⁵ N·m·s²/rad, heavily filtered (§5).

### 3.4 Torque to voltage
$$
V_q = \frac{R}{K_t}\,\tau_{cmd} + K_e\,\omega_{motor}\quad(\text{back-EMF compensation, optional})
$$

R/K_t = 125 V/(N·m). Converted to voltage-domain gains:

- K_p,ω ≈ **6.3 V/(rad/s)**
- K_i,ω ≈ **312 V/rad**

---

## 4. Outer Angle Loop

When the inner loop is closed, it looks like a first-order lag, ω/ω_cmd ≈ 1/(s/ω_c,ω + 1). The outer plant is therefore approximately an integrator, 1/s.

$$
K_{p,\theta}=\omega_{c,\theta}=2\pi\cdot 8\approx \mathbf{50\ (rad/s)/rad}
$$
$$
K_{i,\theta}=K_{p,\theta}\cdot\frac{\omega_{c,\theta}}{8}\approx \mathbf{315\ (rad/s)/(rad\cdot s)}
$$

The integral term removes steady error from cable drag and imbalance torque.

**Outer-loop derivative:** do not differentiate the angle. If you want angle-loop damping, use the gyro directly:

$$
D_\theta = -K_{d,\theta}\,\omega_{gyro}
$$

This is mathematically the derivative-on-measurement, with no numerical differentiation noise. It is usually unnecessary, because the inner loop already provides that damping. Keep K_d,θ = 0 initially.

**Feedforward:** add ω_ff = dθ_ref/dt so that pan and follow-mode commands do not rely on the integrator.

**Output limit:** clamp ω_cmd to ±ω_max (e.g. ±6 rad/s ≈ 340°/s). Rate-limit it to α_max = τ_max/J ≈ 160 rad/s² so the inner loop never sees an unachievable demand.

---

## 5. Derivative Filtering Equations

Use **derivative on measurement** (avoids setpoint kick) with a first-order low-pass:

$$
D(s) = -\frac{K_d\,s}{T_f s + 1}\,y(s),\qquad T_f=\frac{K_d}{K_p\,N},\ \ N\in[5,20]
$$

Alternatively, specify the cutoff directly: T_f = 1/(2πf_d). Choose f_d ≈ 3–5× f_c and well below Nyquist (e.g. 150 Hz at 1 kHz).

### 5.1 Tustin (bilinear) discretisation (recommended)
$$
D_k=\frac{2T_f-T_s}{2T_f+T_s}\,D_{k-1}\;-\;\frac{2K_d}{2T_f+T_s}\,(y_k-y_{k-1})
$$

Precompute the two coefficients:

$$
a_d=\frac{2T_f-T_s}{2T_f+T_s},\qquad b_d=\frac{2K_d}{2T_f+T_s}
$$

Then each step is:

$$
D_k = a_d D_{k-1} - b_d\,(y_k-y_{k-1})
$$

### 5.2 Backward Euler (always stable, more lag)
$$
D_k=\frac{T_f}{T_f+T_s}D_{k-1}-\frac{K_d}{T_f+T_s}(y_k-y_{k-1})
$$

Example at 1 kHz with f_d = 150 Hz: T_f = 1.06 ms, so a_d = 0.359.

If the gyro is noisy, add a second-order biquad (Butterworth, ~250 Hz) on ω_meas before the PID. Add a notch filter if there is a frame resonance; identify it from a gyro FFT.

---

## 6. Anti-Windup Equations

Saturation limits: u_min, u_max (torque/V_q for the inner loop, ±ω_max for the outer loop).

### 6.1 