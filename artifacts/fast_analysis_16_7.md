# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

> **Note:** I don't have access to your project's "Cycle 16" documentation, so I've assumed a typical configuration: a brushless gimbal motor (e.g., 2804/GBM2804) driven by FOC in voltage or torque mode, a 6-axis IMU (ICM-42688 / BMI270 / MPU6050-class) on the camera plate, and an orientation estimate from a complementary or Mahony filter. Adjust the constants to match your Cycle 16 specs.

---

## 1. Architecture

```
            ┌────────────── OUTER LOOP (500 Hz) ──────────────┐   ┌──────── INNER LOOP (2 kHz) ────────┐
θ_ref ──►(+)─► e_θ ─► Angle PI ─► ω_cmd ─►(+)── clamp ──► ω_ref ─►(+)─► e_ω ─► Rate PID ─► u ─► sat ─► FOC (Uq / Iq) ─► Motor
          ▲ (−)                     ff: θ̇_ref ─┘                    ▲ (−)                                       │
          │                                                         │                                          │
     θ_roll (Mahony/CF) ◄──────────── IMU ◄─────────────────────── ω_roll (gyro, LPF) ◄───────────────────────┘
```

**Design rules**
- **Bandwidth separation:** the inner rate loop should be about 5–10× faster than the outer loop. Targets: rate loop ≈ 40 Hz closed-loop bandwidth, angle loop ≈ 5–8 Hz.
- **Inner loop uses the raw gyro rate**, not the derivative of the estimated angle. It is lower-latency and less noisy.
- **Derivative on measurement** in the rate loop avoids derivative kick when the setpoint steps.
- **Outer loop is P or PI only.** The inner loop already supplies damping, so the outer loop needs no D term.

### Timing on the ESP32-S3

| Item | Value |
|---|---|
| Inner loop | 2 kHz (Ts = 500 µs), `esp_timer` or GPTimer ISR → task notify |
| Outer loop | 500 Hz (every 4th inner tick) |
| Task | `xTaskCreatePinnedToCore(..., core 1, prio configMAX_PRIORITIES-1)` |
| IMU | SPI @ 10 MHz, ODR ≥ 2 kHz, DRDY interrupt as the time base |
| Math | `float` (the S3 has a single-precision FPU; avoid `double`) |

---

## 2. Inner Loop: Roll Rate PID

### 2.1 Continuous-time form

$$
u(t) = K_p\,e_\omega + K_i\!\int e_\omega\,dt \;-\; K_d\,\frac{s}{T_f s + 1}\,\omega_{meas}
$$

where $e_\omega = \omega_{ref} - \omega_{meas}$ and $T_f$ is the derivative filter time constant.

### 2.2 Derivative filtering

The unfiltered derivative amplifies gyro noise by $1/T_s$ (×2000 at 2 kHz), so the D term must be band-limited.

**Choosing the cutoff:** $f_c = \dfrac{1}{2\pi T_f}$, typically 5–10× the rate-loop bandwidth and well below Nyquist. Start with **$f_c$ = 150 Hz** ($T_f$ ≈ 1.06 ms).

**Option A: Backward Euler (simple, always stable)**

$$
D[k] = \frac{T_f}{T_f + T_s}\,D[k-1] \;-\; \frac{K_d}{T_f + T_s}\,\big(\omega[k] - \omega[k-1]\big)
$$

**Option B: Tustin / bilinear (better phase near $f_c$, recommended)**

$$
D[k] = \underbrace{\frac{2T_f - T_s}{2T_f + T_s}}_{\alpha}\,D[k-1] \;-\; \underbrace{\frac{2K_d}{2T_f + T_s}}_{\beta}\,\big(\omega[k] - \omega[k-1]\big)
$$

Requirement: $2T_f > T_s$, otherwise α < 0 and the filter rings. With $T_s$ = 0.5 ms and $T_f$ = 1.06 ms: α ≈ 0.618 and β ≈ 1.235·K_d·1000.

**Upstream gyro conditioning (applied before the PID):**
- A 1st-order LPF on the gyro at ~250 Hz feeds both the P and D paths.
- An optional biquad notch at the motor/frame resonance. Find it with a chirp test; it is usually 80–200 Hz on small gimbals.

### 2.3 Integrator

Forward Euler integration:

$$
I[k] = I[k-1] + K_i\,T_s\,e_\omega[k]
$$

Store $K_i$ inside the integrator, as above, so that changing $K_i$ live does not cause an output bump.

### 2.4 Anti-windup

Use three layers. Clamping is mandatory; the other two improve recovery.

**Unsaturated output:**
$$
u_{raw}[k] = K_p\,e_\omega[k] + I[k] + D[k]
$$

**(1) Output saturation:**
$$
u[k] = \text{sat}(u_{raw}) = \min\!\big(\max(u_{raw},\, -U_{max}),\, U_{max}\big)
$$
$U_{max}$ is the FOC limit, e.g. $U_q \le V_{bus}/\sqrt{3}$ or the current limit $I_{q,max}$.

**(2) Integrator clamping** (independent hard bounds):
$$
I[k] = \min\!\big(\max(I[k],\, -I_{max}),\, I_{max}\big), \qquad I_{max} \approx 0.3\text{–}0.5\,U_{max}
$$
For a balanced roll axis the integrator only needs to cover residual imbalance and cable drag, so keep $I_{max}$ small.

**(3a) Conditional integration** (simplest):
$$
\text{integrate only if } \neg\Big[\, (u_{raw} \ne u) \;\wedge\; \text{sign}(e_\omega) = \text{sign}(u_{raw}) \,\Big]
$$

**(3b) Back-calculation** (smoothest; use instead of 3a):
$$
I[k] = I[k-1] + T_s\Big[K_i\,e_\omega[k] + \frac{1}{T_t}\big(u[k] - u_{raw}[k]\big)\Big]
$$
Tracking time constant: $T_t = \sqrt{T_i T_d}$ where $T_i = K_p/K_i$ and $T_d = K_d/K_p$. If there is no D term, use $T_t \approx 0.5\,T_i$. Because $u_{raw}$ depends on $I[k]$, compute it using $I[k-1]$, then apply the correction. A one-sample lag is fine at 2 kHz.

### 2.5 Optional: output slew limit
$$
u[k] = u[k-1] + \text{clamp}\big(u[k] - u[k-1],\, -\Delta u_{max},\, \Delta u_{max}\big)
$$
This protects the motor and driver from current spikes on IMU glitches.

---

## 3. Outer Loop: Roll Angle PI

### 3.1 Error with wrap

$$
e_\theta = \text{wrap}_{[-\pi,\pi)}(\theta_{ref} - \theta_{roll})
$$

Roll on a gimbal rarely exceeds ±45°, but wrapping prevents a 2π error after a power-on flip.

### 3.2 Control law

$$
\omega_{cmd}[k] = K_{p\theta}\,e_\theta[k] + I_\theta[k] + K_{ff}\,\dot\theta_{ref}[k]
$$
$$
I_\theta[k] = \text{clamp}\big(I_\theta[k-1] + K_{i\theta}\,T_{s,o}\,e_\theta[k],\; \pm I_{\theta,max}\big)
$$

- **Rate feed-forward** ($K_{ff} \approx 1$): when following a joystick or smoothed follow-mode trajectory, the outer loop then only corrects error and does not lag.
- **Rate clamp:** $\omega_{ref} = \text{clamp}(\omega_{cmd}, \pm\Omega_{max})$, e.g. $\Omega_{max}$ = 300 °/s for roll.
- **Outer anti-windup:** freeze $I_\theta$ when $|\omega_{cmd}| \ge \Omega_{max}$ and the error drives it further. Also freeze it while the inner loop is saturated; pass an `inner_sat` flag upward. This is the classic cascaded windup trap.
- Often $K_{i\theta}$ = 0 is fine, because the inner integrator already removes steady-state torque bias.

---

## 4. Reference Implementation (C, ESP-IDF)

```c
#include <math.h>
#include <stdbool.h>

typedef struct {
    float kp, ki, kd;
    float Ts, Tf;
    float alpha, beta;       // derivative filter coefs (