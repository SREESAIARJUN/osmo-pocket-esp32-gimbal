# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have the specific documents for "Cycle 26" of your project, so this design states its assumptions explicitly. Replace the placeholder numbers with your measured plant data.

## 0. Assumptions

| Item | Assumed value |
|---|---|
| Actuator | Gimbal BLDC, FOC current or voltage mode (SimpleFOC-style) |
| IMU | 6-axis (e.g. ICM-42688 / BMI270), gyro ODR ≥ 2 kHz |
| Attitude estimate | Mahony or complementary filter, roll angle in rad |
| Inner (rate) loop | 1 kHz, Ts_r = 1 ms |
| Outer (angle) loop | 250 Hz, Ts_a = 4 ms |
| Output | Normalized torque command u ∈ [−1, 1] (or Iq in A) |
| MCU | ESP32-S3, single-precision FPU, control task pinned to Core 1 |

## 1. Architecture

```
 θ_ref ──►(+)──► Angle PI ──► ω_ff + ω_cmd ──►(+)──► Rate PID ──► sat ──► FOC ──► Motor/Roll
          ▲−      (250 Hz)        (rate-limited)  ▲−    (1 kHz)                    │
          │                                       │                                │
       θ_meas ◄──── attitude filter ◄──── gyro/accel ◄─────────────────────────────┘
                                          ω_meas (LPF'd gyro_x)
```

Design rules:
- **Bandwidth separation:** the inner loop bandwidth should be at least 5–10× the outer loop bandwidth. Targets here are 40 Hz for the inner loop and 5–8 Hz for the outer loop.
- **Outer loop structure:** use P or PI. The inner rate loop already provides the damping that a D term would add.
- **Inner loop structure:** use full PID, with derivative on measurement and a filtered D term.

## 2. Outer Loop: Angle → Rate Setpoint

**Angle error with wrap handling** (roll can approach ±π during stow and flip):

```
e_θ[k] = wrap(θ_ref[k] − θ_meas[k]),   wrap(x) = atan2(sin x, cos x)
```

**PI with clamping anti-windup:**

```
P_θ[k]   = Kp_θ · e_θ[k]
I_θ[k]   = I_θ[k−1] + Ki_θ · Ts_a · e_θ[k]       (conditionally, see §4)
ω_cmd[k] = P_θ[k] + I_θ[k] + ω_ff[k]
ω_cmd[k] = clamp(ω_cmd[k], −ω_max, +ω_max)       e.g. ω_max = 6 rad/s
```

Here ω_ff is the feed-forward rate. It is the derivative of the commanded trajectory (e.g. from joystick follow mode). It greatly reduces tracking lag.

**Setpoint slew limit**, to protect the inner loop from steps:

```
Δmax = α_max · Ts_a                (α_max ≈ 50 rad/s²)
ω_cmd[k] = ω_cmd[k−1] + clamp(ω_cmd[k] − ω_cmd[k−1], −Δmax, Δmax)
```

## 3. Inner Loop: Rate → Torque

### 3.1 Gyro pre-filter

The motor and frame excite structural modes, so filter the gyro before the PID:

```
ω_meas = Biquad_LPF(gyro_x, fc = 150–250 Hz)   [+ optional notch at the frame resonance]
```

Keep the total phase lag at the 40 Hz crossover below about 15°.

### 3.2 P and I terms

```
e_ω[k] = ω_cmd[k] − ω_meas[k]
P[k]   = Kp · e_ω[k]
I[k]   = I[k−1] + Ki · Ts · e_ω[k]          (backward Euler; trapezoid optional)
```

### 3.3 Derivative with first-order filtering

Take the derivative on the measurement, not the error. This avoids a derivative kick when ω_cmd steps.

The continuous form is:

```
D(s) = −Kd · s / (τ_d·s + 1) · ω_meas(s),     τ_d = 1 / (2π·f_d)
```

**Backward-Euler discretization** (simple and always stable):

```
D[k] = α · D[k−1] − Kd·(1−α)/Ts · (ω_meas[k] − ω_meas[k−1])
α    = τ_d / (τ_d + Ts)
```

**Tustin discretization** (better phase match near f_d):

```
D[k] = [(2τ_d − Ts)·D[k−1] − 2Kd·(ω_meas[k] − ω_meas[k−1])] / (2τ_d + Ts)
```

Choose the filter corner f_d at roughly 2–4× the loop crossover, e.g. f_d = 100 Hz. With Ts = 1 ms:
- τ_d = 1.59 ms
- α = 0.614

Alternative: if your IMU provides angular acceleration (rare), or you use a model-based estimate, use that in place of the differenced gyro.

### 3.4 Output

```
v[k] = P[k] + I[k] + D[k] + K_ff·ω_cmd[k]      (optional velocity FF for back-EMF/friction)
u[k] = clamp(v[k], u_min, u_max)
```

## 4. Anti-Windup

Use both mechanisms below. Clamping is the primary one; back-calculation smooths recovery.

### 4.1 Conditional integration (clamping)

Freeze the integrator when the output is saturated and the error would push it further into saturation:

```
sat_hi = (v[k] ≥ u_max) and (e[k] > 0)
sat_lo = (v[k] ≤ u_min) and (e[k] < 0)

if !(sat_hi or sat_lo):
    I[k] = I[k−1] + Ki·Ts·e[k]
else:
    I[k] = I[k−1]
```

Compute v[k] with the previous I, decide, then update I.

### 4.2 Back-calculation

Bleed the integrator toward consistency with the saturated output:

```
I[k] = I[k−1] + Ts · ( Ki·e[k] + (u[k] − v[k]) / T_t )
T_t  ≈ sqrt(Ti · Td)    (Ti = Kp/Ki, Td = Kd/Kp);  if Kd=0, T_t ≈ 0.5·Ti
```

### 4.3 Hard integrator bound

Add this as a safety net:

```
I[k] = clamp(I[k], −I_max, +I_max),    I_max ≈ 0.3–0.5 · u_max
```

### 4.4 Cascade-specific windup

When the inner loop saturates, the outer integrator must also freeze. Otherwise it winds up chasing a rate the motor cannot deliver.

```
if inner_saturated:  hold I_θ  (do not integrate e_θ)
```

## 5. Reference Implementation (C, ESP-IDF)

```c
typedef struct {
    float kp, ki, kd, kff;
    float ts, alpha_d;          // alpha_d = tau/(tau+ts)
    float i_term, d_term;
    float y_prev;
    float out_min, out_max, i_max;
    float tt_inv;               // 1/T_t back-calc gain (0 to disable)
    bool  saturated;
} pid_t;

static inline float clampf(float x, float lo, float hi)
{ return x < lo ? lo : (x > hi ? hi : x); }

void pid_init(pid_t *p, float kp, float ki, float kd, float ts, float fd_hz,
              float umin, float umax, float imax, float tt)
{
    *p = (pid_t){ .kp=kp, .ki=ki, .kd=kd, .ts=ts,
                  .out_min=umin, .out_max=umax, .i_max=imax,
                  .tt_inv = tt > 0 ? 1.0f/tt : 0.0f };
    float tau = 1.0f / (2.0f * (float)M_PI * fd_hz);
    p->alpha_d = tau / (tau + ts);
}

void pid_reset(pid_t *p, float y_now) { p->i_term = p->d_term = 0; p->y_prev = y_now; }

IRAM_ATTR float pid_update(pid_t *p, float sp, float y, float ff)
{
    float e = sp - y;

    // Derivative on measurement, 1st-order filtered (backward Euler)
    p->d_term = p->alpha_d * p->d_term
              - p->kd * (1.0f - p->alpha_d) * (y - p->y_prev) / p->ts;
    p->y_prev = y;

    float pterm = p->kp * e;
    float v = pterm + p->i_term + p->d_term + p->kff * ff;
    float u = clampf(v, p->out_min, p->out_max);

    // Conditional integration + back-calculation
    bool push_hi = (v >= p->out_max) && (e > 0);
    bool push_lo = (v <= p->out_min) && (e < 0);
    p->saturated = (u != v);
    if (!(push_hi || push_lo))
        p->i_term += p->ki * p->ts * e;
    p->i_term += p->ts * p->tt_inv * (u - v);
    p->i_term  = clampf(p->i_term, -p->i_max, p->i_max);

    return u;
}
```

**Scheduling:** run the control loop in a task on Core 1. Keep Wi-Fi/BLE on Core 0.

```c
static pid_t rate_pid, angle_pid;

void control_task(void *arg)            // notified by gptimer ISR every 1 ms
{
    uint32_t tick = 0;
    float w_cmd = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        imu_read(&gyro, &ac