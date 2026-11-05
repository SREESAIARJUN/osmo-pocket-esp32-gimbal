# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have your project's Cycle 24 documents, so the hardware, rates, and limits below are stated assumptions. Replace them with your actual values. The equations and structure apply regardless.

---

## 1. Assumptions

| Item | Assumed value |
|---|---|
| Actuator | Gimbal BLDC (e.g., 2804/GBM-series), voltage-mode FOC or SVPWM via MCPWM |
| Controller output | Normalized torque/voltage command `u ∈ [-1, +1]` (fraction of V_bus or I_max) |
| IMU | 6-axis over SPI (e.g., ICM-42688-P), gyro ODR ≥ 2 kHz |
| Attitude estimate | Mahony/complementary filter giving roll angle φ (rad) |
| Inner (rate) loop | 1 kHz (Ts_r = 1 ms) |
| Outer (angle) loop | 250 Hz (Ts_a = 4 ms), i.e. a 4:1 decimation |
| CPU | Core 1 dedicated to control. FPU used for single-precision `float`. |

---

## 2. Loop Architecture

```
 φ_ref ─►(+)─► [Angle PI(D)] ─► ω_ref ─►(+)─► [Rate PID] ─► u ─► Motor/FOC ─► Gimbal
          ▲ -        (250 Hz)    │clamp   ▲ -    (1 kHz)  │sat           │
          │                      ▼        │               ▼              │
          φ̂ ◄── Attitude filter ◄─────── ω_gyro (filtered) ◄──── IMU ◄──┘
```

Design rules:

1. **The inner loop uses the gyro directly.** Never differentiate the angle estimate to get rate. The gyro is the cleanest rate signal you have.
2. **Separate the bandwidths.** Make the inner closed-loop bandwidth 5–10× the outer bandwidth. A typical target is ~40–60 Hz for the rate loop and ~5–10 Hz for the angle loop.
3. **Keep the outer loop mostly P.** An outer integrator is optional. Add it only if you see steady-state angle error from friction or imbalance that the inner loop's integrator doesn't remove.
4. **Clamp the outer output.** Limiting ω_ref to the motor's achievable slew, e.g. ±6 rad/s, prevents the outer loop from demanding impossible rates.

---

## 3. Signal Conditioning

**Gyro low-pass** (inner loop, first-order, cutoff f_g ≈ 150–250 Hz):

$$
\omega_f[k] = \omega_f[k-1] + \beta\,(\omega[k] - \omega_f[k-1]),\qquad \beta = \frac{T_s}{T_s + \frac{1}{2\pi f_g}}
$$

**Optional notch filter.** If a frame or mount resonance shows up in the gyro FFT, add a biquad notch at that frequency (Q ≈ 2–5). Gimbal arms often resonate at 80–300 Hz.

**Angle error wrapping.** Roll is usually limited to ±90°, so wrapping matters less here than on yaw. Wrap anyway:

$$
e_\phi = \operatorname{wrap}_{[-\pi,\pi)}(\phi_{ref} - \hat\phi)
$$

---

## 4. Control Law (Parallel Form)

For each loop:

$$
u_{unsat}[k] = P[k] + I[k] + D[k] + u_{ff}[k]
$$

Where:
- $P[k] = K_p\,(b\,r[k] - y[k])$. The setpoint weight $b \in [0.5, 1]$ reduces overshoot on step commands.
- $u_{ff}$ is feedforward. For the roll inner loop, it is a static imbalance/gravity term: $u_{ff} = K_g \sin\hat\phi$.

### 4.1 Derivative: on measurement, filtered (Tustin)

Taking the derivative on the measurement (−y rather than e) avoids derivative kick on setpoint steps.

Continuous form:

$$
D(s) = -\frac{K_d\,s}{T_f s + 1}\,Y(s), \qquad T_f = \frac{K_d}{K_p N},\; N \approx 5\text{–}20
$$

Discretized with Tustin:

$$
D[k] = \alpha_d\, D[k-1] \;-\; \gamma_d\,\big(y[k] - y[k-1]\big)
$$

$$
\alpha_d = \frac{2T_f - T_s}{2T_f + T_s}, \qquad \gamma_d = \frac{2K_d}{2T_f + T_s}
$$

Constraint: you need $T_f > T_s/2$, otherwise $\alpha_d < 0$ and the filter rings. In practice, choose the derivative filter cutoff $f_d = 1/(2\pi T_f)$ at about 1/5–1/10 of the loop rate. That is roughly 100–200 Hz for the 1 kHz rate loop.

**Inner-loop note.** In the rate loop, y is ω, so the D term acts on angular acceleration. That is noisy. Start with $K_d = 0$ on the rate loop. Add D only if you need more phase margin, and keep $f_d$ conservative.

### 4.2 Integrator (forward Euler)

$$
I^*[k+1] = I[k] + K_i\,T_s\,e[k]
$$

### 4.3 Anti-windup Method A: Conditional Integration (Clamping)

Saturate the output:

$$
u[k] = \operatorname{sat}(u_{unsat}[k]) = \min\!\big(\max(u_{unsat}, u_{min}),\, u_{max}\big)
$$

Then freeze the integrator when the output is saturated **and** the error would push it further into saturation:

$$
\text{clamp} = \big(u_{unsat} \neq u\big) \;\wedge\; \operatorname{sgn}(e[k]) = \operatorname{sgn}(u_{unsat}[k])
$$

$$
I[k+1] =
\begin{cases}
I[k] & \text{if clamp} \\[2pt]
\operatorname{sat}_{[I_{min},\,I_{max}]}\big(I^*[k+1]\big) & \text{otherwise}
\end{cases}
$$

Also apply a hard integrator limit, e.g. $|I| \le 0.5\,u_{max}$ for the rate loop. This prevents slow windup from sustained disturbances while the output is not saturated.

### 4.4 Anti-windup Method B: Back-Calculation (alternative)

Back-calculation recovers more smoothly from long saturation, such as when the gimbal hits a hard stop:

$$
I[k+1] = I[k] + K_i T_s\, e[k] + \frac{T_s}{T_t}\big(u[k] - u_{unsat}[k]\big), \qquad T_t \approx \sqrt{T_i T_d}
$$

where $T_i = K_p/K_i$ and $T_d = K_d/K_p$. If $K_d = 0$, use $T_t \approx 0.5\,T_i$.

**Recommendation:** use clamping (4.3) on the inner loop because it is simpler and deterministic. Use clamping on the outer loop as well, where the "saturation" is the ω_ref limit.

### 4.5 Output Slew Limit (inner loop)

To avoid current spikes:

$$
u[k] = u[k-1] + \operatorname{sat}_{[-\Delta_{max},\,\Delta_{max}]}(u[k] - u[k-1])
$$

---

## 5. C Implementation

```c
// pid.h
typedef struct {
    float kp, ki, kd;
    float b;              // setpoint weight for P
    float ts;
    float alpha_d, gamma_d;
    float i_min, i_max;
    float u_min, u_max;
    float du_max;         // slew limit per sample (<=0 disables)
    // state
    float integ, deriv, y_prev, u_prev;
} pid_t;

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

void pid_init(pid_t *p, float kp, float ki, float kd, float N, float ts,
              float u_min, float u_max, float i_lim, float du_max)
{
    p->kp = kp; p->ki = ki; p->kd = kd; p->ts = ts; p->b = 1.0f;
    float tf = (kd > 0 && kp > 0) ? kd / (kp * N) : ts;   // filter time const
    if (tf < ts) tf = ts;                                  // keep alpha_d > 0
    p->alpha_d = (2*tf - ts) / (2*tf + ts);
    p->gamma_d = (2*kd) / (2*tf + ts);
    p->u_min = u_min; p->u_max = u_max;
    p->i_min = -i_lim; p->i_max = i_lim;
    p->du_max = du_max;
    p->integ = p->deriv = p->y_prev = p->u_prev = 0.0f;
}

float IR