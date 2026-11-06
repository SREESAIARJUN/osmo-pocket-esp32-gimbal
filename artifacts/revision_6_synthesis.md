# SimpleFOC Cascaded Control Tuning: 3-Axis Gimbal

I don't have the Rev 6.0/150 hardware files or the job #13 requirements. This is the complete design method with worked example numbers. Replace the **[bracketed]** parameters with your measured values and the formulas give the final coefficients. If "150" means a specific filter cutoff or loop rate, plug it in where noted.

---

## 1. Architecture and Bandwidth Separation

```
θ_ref ─► [P angle] ─► ω_ref ─(+ω_ff)─► [PI velocity] ─► V_q/I_q ─(+τ_ff)─► [torque loop] ─► motor
              ▲                ▲                │
              θ_meas      LPF2(ω_meas) ◄─ encoder
```

Keep each outer loop 3–10× slower than the loop inside it:

| Loop | Target bandwidth | Rule |
|---|---|---|
| Current (FOC mode only) | f_ci ≈ 1–2 kHz | Limited by PWM/ADC rate |
| Velocity | f_cv ≈ f_ci / 5–10, or **≈ f_move / 10–20** in voltage mode | Main tuning target |
| Angle | f_cp ≈ f_cv / 3–5 | Stabilization loop |
| Velocity LPF cutoff | f_lpf ≈ 3–5 × f_cv | Limits phase lag at crossover to about 15–25° |

Gimbal motors are usually high-resistance (5–15 Ω) and driven in **voltage torque mode**. In that case the "current loop" is the electrical pole R/(2πL), typically several kHz, so it is effectively instantaneous.

---

## 2. Velocity Loop PI Formulation

**Plant (voltage mode)** per axis:

$$\frac{\omega(s)}{V_q(s)} = \frac{K_t/R}{J s + b + K_tK_e/R}$$

Here K_t ≈ K_e (in SI units). The back-EMF term acts as electrical damping.

**Pole-zero cancellation design** for target crossover ω_c = 2π·f_cv:

$$K_p = \frac{\omega_c\, J\, R}{K_t} \quad [\text{V/(rad/s)}],\qquad K_i = \frac{K_p\,\omega_c}{N},\; N = 4\text{–}10$$

Use N ≈ 5 for good disturbance rejection, which matters for handheld gimbal shake. Use N ≈ 10 for more phase margin.

**Worked example** (pitch axis, GBM2804-class motor):

| Param | Value |
|---|---|
| R | [10 Ω] |
| K_t | [0.045 N·m/A] (from KV ≈ 210 rpm/V) |
| J (rotor + payload about axis) | [2.0e-4 kg·m²] |
| f_cv target | [25 Hz] → ω_c = 157 rad/s |

- K_p = 157 × 2e-4 × 10 / 0.045 ≈ **7.0 V/(rad/s)**
- K_i = 7.0 × 157 / 5 ≈ **220 V/rad**

**Per-axis scaling:** J varies greatly between axes. Yaw usually carries the whole roll+pitch assembly, often 3–10× pitch J. Scale K_p and K_i proportionally to keep the same bandwidth, or lower f_cv on yaw if the voltage limit saturates.

**Angle loop:** P_angle ≈ ω_cp = 2π·f_cp. For example, f_cp = 6 Hz gives P ≈ **38 (rad/s)/rad**. Add a small D term only if there are structural resonances.

**Loop rates:** run `loopFOC()` at 10 kHz or more. Run `move()` at 1–2 kHz or more (`motor.motion_downsample`). The discrete design is valid when f_move ≥ 20 × f_cv.

---

## 3. Second-Order Low-Pass Filter (Velocity Feedback)

SimpleFOC's built-in `LowPassFilter` is first-order (`Tf`). A 2nd-order Butterworth biquad gives about 40 dB/decade of encoder-quantization noise rejection, versus 20 dB/decade for first-order, for similar phase lag at crossover.

**Bilinear transform with prewarping** (Q = 1/√2 for Butterworth):

$$K = \tan\!\left(\frac{\pi f_c}{f_s}\right),\quad \eta = \frac{1}{1 + K/Q + K^2}$$

$$b_0 = K^2\eta,\; b_1 = 2b_0,\; b_2 = b_0,\quad a_1 = 2(K^2-1)\eta,\; a_2 = (1 - K/Q + K^2)\eta$$

$$y_n = b_0x_n + b_1x_{n-1} + b_2x_{n-2} - a_1y_{n-1} - a_2y_{n-2}$$

**Example:** f_s = 2000 Hz (move rate), f_c = 150 Hz:

| Coef | Value |
|---|---|
| b₀ | 0.041261 |
| b₁ | 0.082522 |
| b₂ | 0.041261 |
| a₁ | −1.348941 |
| a₂ | 0.513995 |

Sanity check: Σb = 1 + a₁ + a₂ = 0.16504, so DC gain is 1. ✓

**Phase lag at crossover** (x = f_cv/f_c): φ = −atan(√2·x / (1 − x²)). At 25/150 the lag is about **−13.6°**. Budget this against the 45–60° phase margin target.

**Implementation:**

```cpp
struct Biquad {
  float b0,b1,b2,a1,a2, x1=0,x2=0,y1=0,y2=0;
  void design(float fc, float fs, float Q=0.70710678f){
    float K = tanf(PI*fc/fs), n = 1.0f/(1.0f + K/Q + K*K);
    b0 = K*K*n; b1 = 2*b0; b2 = b0;
    a1 = 2*(K*K-1)*n; a2 = (1 - K/Q + K*K)*n;
  }
  float operator()(float x){
    float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
    x2=x1; x1=x; y2=y1; y1=y; return y;
  }
};
```

Notes:
- If the move-loop rate jitters, recompute f_s or use a fixed-rate timer. Biquad coefficients assume constant f_s.
- Set `motor.LPF_velocity.Tf = 0` so the filters don't stack, and feed the biquad output into your velocity PI.
- If the frame has a structural resonance (often 80–300 Hz on gimbal arms), add a second biquad as a **notch** at that frequency with Q ≈ 2–5.

---

## 4. Saturation Limits and Anti-Windup

**Output (voltage) limit:**

$$V_{lim} = \min\left( \frac{V_{bus}}{\sqrt3}\ (\text{SVPWM})\ \text{or}\ \frac{V_{bus}}{2}\ (\text{SinePWM}),\; I_{max,thermal}\cdot R \right)$$

Example: 12 V bus with SVPWM gives 6.9 V. A thermal limit of 0.6 A × 10 Ω gives 6.0 V, so **V_lim = 6.0 V**. Set `motor.voltage_limit` and `motor.PID_velocity.limit` to this value.

**Integrator clamp:** limit the integral state to