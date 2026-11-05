# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis (Cycle 6)

I don't have your earlier cycle documents, so this design rests on the assumptions in Section 0. Replace the placeholder plant numbers with your measured values (Section 7 shows how). The structure and equations do not depend on those numbers.

---

## 0. Assumptions

| Item | Assumed value |
|---|---|
| MCU | ESP32-S3, single-precision FPU, control pinned to Core 1 |
| Actuator | Gimbal BLDC under FOC (torque/current mode), loop ≥ 10 kHz |
| Sensor | 6-axis IMU, gyro ODR ≥ 2 kHz, data-ready interrupt |
| Attitude | Complementary or Mahony filter giving roll angle θ |
| Roll inertia J (motor + payload) | 1.5 × 10⁻⁴ kg·m² (placeholder) |
| Controller output | Torque command τ [N·m], converted to i_q by τ / K_t |
| Inner (rate) loop rate | **2 kHz** (Ts = 0.5 ms) |
| Outer (angle) loop rate | **500 Hz** (Ts = 2 ms, every 4th inner tick) |

---

## 1. Architecture

```
θ_ref ─►(slew limit)─►[Σ]─e_θ─► Angle PI ─►ω_ff+─►[clamp ±ω_max]─ω_cmd─►[Σ]─e_ω─► Rate PID ─►[clamp ±τ_max]─► FOC ─► Motor/Payload
                      ▲−                                                ▲−                  (D on measurement)            │
                      │                                                 │                                                 │
                      └──────── θ (attitude filter) ◄───────────────────┴──── ω_gyro (LPF + notch) ◄─────────── IMU ◄─────┘
```

**Design rules**

- The inner bandwidth must be 5–10× the outer bandwidth. That way the outer loop sees the inner loop as roughly unity gain.
- The rate loop uses the raw, filtered gyro rate, not the derivative of θ. This keeps it low-latency and low-noise.
- The derivative term acts on the measurement only. This avoids a derivative kick when the setpoint steps.

---

## 2. Plant Model and Gain Derivation

With FOC closing a fast current loop, the roll rate plant is approximately:

$$
G_\omega(s) = \frac{\omega(s)}{\tau(s)} \approx \frac{1}{J s}\,e^{-sT_d}, \qquad T_d \approx 1\text{–}1.5\,T_s \text{ (IMU + compute + PWM delay)}
$$

### 2.1 Inner rate loop (PI + filtered D)

Target crossover: ω_c,in = 2π · 40 Hz ≈ 251 rad/s. The 2 ms delay budget keeps phase loss at about 7° at 2 kHz.

Proportional gain:

$$
K_{p\omega} = J\,\omega_{c,in} = 1.5\times10^{-4}\cdot 251 \approx 3.8\times10^{-2}\ \tfrac{\text{N·m}}{\text{rad/s}}
$$

Integral gain, with the PI zero placed at ω_c / 4:

$$
K_{i\omega} = K_{p\omega}\,\frac{\omega_{c,in}}{4} \approx 2.4\ \tfrac{\text{N·m}}{\text{rad}}
$$

Derivative gain:

$$
K_{d\omega} \approx 0.05\text{–}0.1\cdot\frac{K_{p\omega}}{\omega_{c,in}} \;\Rightarrow\; K_{d\omega}\approx 1\times10^{-5}\ \text{N·m·s}^2
$$

The D term on a rate loop acts on angular acceleration, which is noisy. Start with K_d = 0 and add D only if you see overshoot or a lightly damped mechanical mode.

### 2.2 Outer angle loop (P or PI)

Target crossover: ω_c,out = 2π · 6 Hz ≈ 38 rad/s.

$$
K_{p\theta} = \omega_{c,out} \approx 38\ \text{s}^{-1}
$$

$$
K_{i\theta} \approx 0\text{–}20\ \text{s}^{-2} \quad \text{(only if steady-state offset from cable drag or imbalance persists)}
$$

Rate feed-forward: ω_ff = dθ_ref/dt, taken from the slew-limited setpoint. This removes tracking lag during pans.

Rate-command clamp: ω_max = 6 rad/s (about 340°/s). Set this to suit your follow-mode spec.

---

## 3. Derivative Filtering

Use a first-order filtered derivative on the measurement:

$$
D(s) = -\,\frac{K_d\,s}{T_f\,s+1}\,y(s), \qquad T_f = \frac{K_d/K_p}{N},\ N = 8\text{–}20
$$

Alternatively, specify T_f directly from a corner frequency f_D:

$$
T_f = \frac{1}{2\pi f_D}, \quad f_D \approx 150\ \text{Hz} \Rightarrow T_f = 1.06\ \text{ms}
$$

### 3.1 Discretization: backward Euler (recommended)

Backward Euler is always stable for any T_f > 0, including T_f < Ts:

$$
\boxed{D_k = \frac{T_f}{T_f+T_s}\,D_{k-1} \;-\; \frac{K_d}{T_f+T_s}\,(y_k - y_{k-1})}
$$

Precompute the coefficients:

$$
\alpha_D = \frac{T_f}{T_f+T_s}, \qquad \beta_D = \frac{K_d}{T_f+T_s}
$$

With Ts = 0.5 ms and T_f = 1.06 ms: α_D = 0.679, β_D = K_d / 1.56 × 10⁻³.

### 3.2 Discretization: Tustin (alternative)

Tustin has better phase near f_D, but it requires 2T_f > Ts or it rings:

$$
D_k = \frac{2T_f - T_s}{2T_f + T_s}\,D_{k-1} - \frac{2K_d}{2T_f + T_s}\,(y_k - y_{k-1})
$$

### 3.3 Gyro pre-filtering (applied to the measurement before both P and D)

1. **2nd-order Butterworth low-pass**, f_c ≈ 200–250 Hz, as a biquad in Direct Form II Transposed.
2. **Notch filter** at the dominant structural or motor-cogging resonance, if one exists. Find it with a sine sweep or an FFT of gyro data while hovering. Use Q ≈ 3–5.
3. **Group-delay budget**: total phase lag from all filters at ω_c,in should stay below 15°. A 200 Hz Butterworth cost