# Cascaded PID Design: Gimbal Roll Axis (ESP32-S3)

I don't have the earlier "Cycle 14" documents, so this design rests on the assumptions in the table below. Adjust the numbers to match your actual hardware and earlier decisions.

| Item | Assumption |
|---|---|
| Actuator | Gimbal BLDC (e.g., GBM2804, ~11 pole pairs) driven by SVPWM/FOC. Control output is normalized Uq ∈ [−1, 1]. |
| Sensor | 6-axis IMU (ICM-42688 or MPU6050) over SPI. Gyro ODR ≥ 2 kHz. Attitude comes from a complementary filter or Mahony filter. |
| MCU | ESP32-S3 at 240 MHz with single-precision FPU. All math is `float`. |
| Rates | Rate loop: 2 kHz (Ts_r = 500 µs). Angle loop: 500 Hz (Ts_a = 2 ms, decimated ÷4). |
| Bandwidth targets | Rate loop: ~50 Hz closed-loop. Angle loop: ~10 Hz (≥4–5× separation). |

---

## 1. Architecture

```
θ_ref ──►(+)──► [Angle PI(D)] ──► ω_ff + ω_cmd ──► clamp ±ω_max ──►(+)──► [Rate PID] ──► clamp ──► Uq ──► FOC ──► Motor
          ▲ −                                                         ▲ −                    │
          │                                                           │                      │ sat flag
     θ_meas (AHRS, 500 Hz)                               ω_meas (gyro, LPF + notch, 2 kHz)   ▼
                                                                                    back to angle-loop I-term
```

- **Outer loop (angle):** a P term, a small I term, and an optional tiny D term. Its output is a rate setpoint in rad/s, plus feed-forward of the commanded angle rate dθ_ref/dt.
- **Inner loop (rate):** a full PID on gyro rate. Its output is the torque/voltage command.
- **Saturation propagation:** when the inner loop saturates, the outer integrator is frozen. This is the cascade anti-windup.

---

## 2. Rate Loop (Inner, 2 kHz)

### 2.1 Signal conditioning

The gyro signal is filtered before it reaches the PID:

- A 2nd-order Butterworth low-pass at f_c ≈ 150–250 Hz. Keep phase lag at 50 Hz below ~15°.
- An optional biquad notch at the measured frame/camera resonance (find it with a chirp test).

### 2.2 Control law (parallel form)

Error:

$$e_\omega[k] = \omega_{ref}[k] - \omega_{meas}[k]$$

**Proportional term**, with setpoint weighting b (0.7–1.0) to soften the response to setpoint steps:

$$P[k] = K_p\,(b\,\omega_{ref}[k] - \omega_{meas}[k])$$

**Derivative term.** It acts on the measurement only, so setpoint steps do not cause a derivative kick. The continuous form is:

$$D(s) = -\frac{K_d\,s}{1 + T_f s}\,\omega_{meas}, \qquad T_f = \frac{1}{2\pi f_d}$$

Set f_d ≈ 60–120 Hz. That is roughly 1.5–2× the loop bandwidth and well below Nyquist (1 kHz).

### 2.3 Derivative filter discretization

**Option A: Tustin (preferred, preserves phase better):**

$$D[k] = \alpha\,D[k-1] - \beta\,(\omega_{meas}[k] - \omega_{meas}[k-1])$$

$$\alpha = \frac{2T_f - T_s}{2T_f + T_s},\qquad \beta = \frac{2K_d}{2T_f + T_s}$$

**Option B: Backward Euler (always stable, slightly more lag):**

$$D[k] = \frac{T_f}{T_f+T_s}D[k-1] - \frac{K_d}{T_f+T_s}(\omega_{meas}[k]-\omega_{meas}[k-1])$$

Worked example: with f_d = 100 Hz, T_f = 1.59 ms and T_s = 0.5 ms, Tustin gives α = 0.728. Precompute α and β whenever the gains change, never inside the ISR.

The D-term of a rate loop is effectively angular acceleration, which is noisy. Keep K_d small. It mainly adds damping against inertia.

### 2.4 Output and saturation

$$v[k] = P[k] + I[k] + D[k] + K_{ff}\,\dot\omega_{ref}$$

$$u[k] = \mathrm{sat}(v[k]) = \min(\max(v[k], u_{min}), u_{max})$$

Use u_max = +0.9 and u_min = −0.9. The 10% headroom is reserved for FOC/back-EMF margin.

### 2.5 Anti-windup: back-calculation with hard clamp

The integrator is updated after the output is computed:

$$I[k+1] = I[k] + K_i T_s\, e_\omega[k] + \frac{T_s}{T_t}\big(u[k] - v[k]\big)$$

Tracking time constant:

$$T_t = \sqrt{T_i T_d} = \sqrt{\frac{K_p}{K_i}\cdot\frac{K_d}{K_p}}$$

If K_d ≈ 0, use T_t ≈ 0.5·T_i instead. The last term is zero when unsaturated. When saturated, it bleeds the integrator back toward the value that just reaches the limit.

The integrator also gets a hard backstop:

$$I[k+1] = \mathrm{clamp}(I[k+1],\; -I_{max},\; I_{max}), \qquad I_{max} \approx 0.5\,u_{max}$$

This bounds the integrator even if T_t is mistuned. It also limits the stored "energy" after a motor stall or when someone holds the gimbal by hand.

**Alternative: conditional integration (clamping).** This is simpler and acceptable if back-calculation is not used:

$$I[k+1] = \begin{cases} I[k] & \text{if } u \ne v \text{ and } \mathrm{sign}(e) = \mathrm{sign}(v) \\ I[k] + K_i T_s e[k] & \text{otherwise}\end{cases}$$

---

## 3. Angle Loop (Outer, 500 Hz)

### 3.1 Error with wrap handling

Roll is normally limited (about ±45°), but wrap the error anyway for safety:

$$e_\theta = \mathrm{wrap}_{\pm\pi}(\theta_{ref} - \theta_{meas})$$

### 3.2 Control law

$$\omega_{cmd} = K_{p\theta}\,e_\theta + I_\theta + D_\theta + \underbrace{\dot\theta_{ref}}_{\text{feed-forward}}$$

- **D_θ:** usually zero, because the inner rate loop already provides damping. If it is used, apply it on the measurement with the same filter form as above (f_d ≈ 20–30 Hz). Alternatively, use −K_dθ·ω_gyro directly; this is noise-free and needs no differentiation.
- **Rate limit:** ω_ref = clamp(ω_cmd, ±ω_max), with ω_max ≈ 3–6 rad/s for smooth footage.

### 3.3 Outer-loop anti-windup (cascade-aware)

The outer integrator is held whenever **either** loop is saturated:

$$\text{hold} = (\omega_{ref} \ne \omega_{cmd}) \;\lor\; \text{rate\_loop\_saturated}$$

Then:

$$I_\theta[k+1] = \begin{cases} I_\theta[k] + K_{i\theta}\,T_{s,a}\,e_\theta & \text{if not hold} \\ I_\theta[k] + \dfrac{T_{s,a}}{T_{t,\theta}}(\omega_{ref}-\omega_{cmd}) & \text{if hold (back-calc)}\end{cases}$$

Finally clamp to ±I_θ,max (≈ 0.3·ω_max).

---

## 4. Starting Gains

These are for normalized Uq output and rad/s / rad units.

| Loop | Kp | Ki | Kd | Filter | Limits |
|---|---|---|---|---|---|
| Rate | 0.05–0.15 | 1–5 (Ti ≈ 30–50 ms) | 0.0005–0.002 | f_d = 100 Hz, gyro LPF 200 Hz | u 