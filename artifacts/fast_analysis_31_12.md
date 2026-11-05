# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

> **Note:** I don't have your project's "Cycle 31" documents, so I can't see your motor, IMU, inertia, or earlier tuning values. The design below uses typical assumptions for a brushless gimbal: a BLDC gimbal motor driven by FOC, and an ICM-42688 or MPU-6050-class IMU. Replace the bracketed `[ ]` values with your measured parameters.

---

## 1. Architecture

```
          Angle loop (outer, 500 Hz)              Rate loop (inner, 2 kHz)
θ_ref ──►(Σ)──► P/PI ──► ω_cmd ──┬──►(Σ)──► PID + D-filter + AW ──► τ/Iq_cmd ──► FOC ──► Motor
          ▲−               (sat)  │   ▲−                                                     │
          │                       │   │                                                      │
          │      ω_ff (θ̇_ref) ───┘   └──── ω_meas (gyro X, notch + LPF) ◄──── IMU ◄────────┘
          └──────────── θ_meas (Mahony / complementary filter) ◄─────────────────────────────┘
```

| Parameter | Inner (rate) | Outer (angle) |
|---|---|---|
| Sample rate | 2 kHz (Ts = 0.5 ms) | 500 Hz (Ts = 2 ms) |
| Target bandwidth | 40–80 Hz | 6–12 Hz (at least 5× below the inner loop) |
| Controller | PID, derivative on measurement | P + small I (optional), rate feedforward |
| Output | Torque / Iq current command | Rate setpoint ω_cmd (deg/s) |
| Saturation | ±Iq_max [e.g. 1.0 A] | ±ω_max [e.g. 300 °/s] |

**Bandwidth separation.** Keep the outer loop at least 5× slower than the inner loop. The outer loop can then treat the inner loop as a unity gain, so you can tune the two loops independently.

**ESP32-S3 task layout:**
- Run the control loop on **Core 1**, triggered by a `gptimer` ISR that notifies a high-priority task. Wi-Fi/BLE stay on Core 0.
- Use `float` throughout. The S3's single-precision FPU handles it; avoid `double`.
- Place the ISR and the PID functions in `IRAM_ATTR` to avoid flash-cache stalls.
- Read the IMU over SPI with DMA at 8 kHz (if it is an ICM-42688), then decimate to 2 kHz after anti-alias filtering.

---

## 2. Inner Rate Loop: Discrete PID

Definitions:
- Error: e[k] = ω_cmd[k] − ω_meas[k]
- Derivative input: ω_meas only. This avoids "derivative kick" when the setpoint steps.

### 2.1 Proportional term
P[k] = Kp · e[k]

### 2.2 Derivative with first-order filter

Continuous form (derivative on measurement, filter pole at N/Td):

D(s) = −Kd · s / (1 + s·Tf) · Y(s), where Tf = Kd / (Kp·N) = Td/N, and N is typically 5–20.

Equivalently, choose the filter cutoff directly: f_c = 1/(2π·Tf). Typical values are 80–150 Hz, i.e. 2–3× the loop bandwidth and below any structural resonance.

**Tustin (bilinear) discretization**, which is recommended:

```
D[k] = α·D[k−1] − β·(y[k] − y[k−1])

α = (2Tf − Ts) / (2Tf + Ts)
β = 2Kd / (2Tf + Ts)
```

**Backward Euler**, a simpler alternative that is always stable:

```
D[k] = Tf/(Tf+Ts) · D[k−1] − Kd/(Tf+Ts) · (y[k] − y[k−1])
```

**Example:** Ts = 0.5 ms and f_c = 120 Hz give Tf = 1.326 ms and α = 0.6827.

**Gyro pre-filtering.** Apply this before the PID, on the measurement path:
1. A biquad notch at the motor/frame resonance. Find it with a sine sweep; it is often around 150–400 Hz.
2. A 1st- or 2nd-order LPF at about 200 Hz.

Every filter adds phase lag. Budget a total of ≤ 20° at the crossover frequency.

### 2.3 Integral with anti-windup

You can use either method below. Clamping is the simpler, deterministic default. Back-calculation recovers faster after long saturation.

**Method A: Conditional integration (clamping)**

```
u_unsat[k] = P[k] + I[k−1] + D[k] + u_ff[k]
u[k]       = sat(u_unsat[k], −u_max, +u_max)

saturated   = (u[k] ≠ u_unsat[k])
same_sign   = sign(e[k]) == sign(u_unsat[k])

if !(saturated && same_sign):
    I[k] = I[k−1] + Ki·Ts·(e[k] + e[k−1])/2      // trapezoidal
else:
    I[k] = I[k−1]                                // freeze

I[k] = sat(I[k], −I_max, +I_max)                 // hard limit, I_max ≈ 0.3–0.6·u_max
```

**Method B: Back-calculation (tracking)**

```
I[k] = I[k−1] + Ts·( Ki·e[k] + (1/Tt)·(u[k] − u_unsat[k]) )

Tt = √(Ti·Td)   with Ti = Kp/Ki, Td = Kd/Kp
(if Kd = 0: Tt ≈ 0.5·Ti)
```

The term `(u − u_unsat)` is zero when the output is unsaturated. When it saturates, the term bleeds the integrator back toward the value that just reaches the limit.

**Bumpless gain changes.** If Ki changes at runtime, store the integral state as the accumulated sum of Ki·e·Ts rather than as a raw error sum multiplied by Ki afterward. That way the output does not jump.

---

## 3. Outer Angle Loop

Definitions:
- e_θ[k] = wrap(θ_ref[k] − θ_meas[k]), where `wrap` maps to (−180°, 180°]. This matters for roll if continuous rotation or follow modes are possible.
- Command: ω_cmd[k] = sat( Kp_θ·e_θ[k] + I_θ[k] + Kff·θ̇_ref[k], ±ω_max )

Guidelines:
- **Mostly P.** The inner loop already provides the integral action against disturbances. Add a small I_θ only to remove steady angle offset from imbalance torque, using the same clamping scheme with I_θ,max ≈ 10–20 °/s.
- **No D term.** Damping comes from the inner loop. The outer D term would just re-differentiate the angle estimate.
- **Rate feedforward (Kff ≈ 0.8–1.0).** Use θ̇_ref from the trajectory or joystick smoother. It greatly reduces tracking lag.
- **Initial gain.** Kp_θ (in 1/s) roughly sets the outer bandwidth: ω_bw ≈ Kp_θ rad/s. Start with Kp_θ ≈ 2π·8 ≈ 50 s⁻¹. Since both e_θ and ω_cmd are in degrees, the gain is unit-consistent.
- **Angle estimate.** Use Mahony or a complementary filter running at 2 kHz with the accelerometer correction gain kept low (Kp_acc ≈ 0.5–2) so that handheld accelerations don't tilt the horizon. Roll is especially sensitive to lateral acceleration during walking.

---

## 4. Reference Implementation (ESP-IDF, C)

```c
typedef struct {
    float kp, ki, kd;
    float tf;              // derivative filter time constant [s]
    float ts;              // sample period [s]
    float u_max, i_max;
    // precomputed coefficients
    float alpha, beta;
    // state
    float i_acc, d_state, y_prev, e_prev;
} pid_t;

void pid_init(pid_t *p) {
    p->alpha   = (2.f*p->tf - p->ts) / (2.f*p->tf + p->ts);
    p->beta    = (2.f*p->kd) / (2.f*p->tf + p->ts);
    p->i_acc = p->d_state = p->y_prev = p->e_prev = 0.f;
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

IRAM_ATTR float pid_update(pid_t *p, float r, float y, float u_ff) {
    float e = r - y;

    float P = p->kp * e;

    // Filtered derivative on measurement (Tustin)
    p->d_state = p->alpha * p->d_state - p->beta * (y - p->y_prev);
    p->y_prev  = y;

    float u_