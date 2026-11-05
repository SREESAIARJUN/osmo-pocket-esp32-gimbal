# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have your project's "Cycle 13" spec sheet, so this design uses the assumptions below. Replace the numbers with your actual motor, IMU, and loop-rate values; the structure and equations stay the same.

| Item | Assumed value |
|---|---|
| Actuator | Gimbal BLDC with FOC (e.g., SimpleFOC). Output is the Uq voltage, or the Iq current setpoint. |
| Sensor | 6-axis IMU (ICM-42688 / BMI270 class), gyro ODR ≥ 2 kHz |
| Attitude estimate | Mahony/complementary filter, run in the same task as the loops |
| Inner (rate) loop | **Ts_ω = 0.5 ms (2 kHz)**, target crossover ≈ 40–60 Hz |
| Outer (angle) loop | **Ts_θ = 1 ms (1 kHz)**, target crossover ≈ 6–10 Hz |
| Output limit | ±U_max (e.g., ±6 V on a 12 V bus, or the motor's rated Iq) |

---

## 1. Architecture

```
 θ_ref ──►(Σ)──► Angle P(I) ──►(Σ)──► sat(±ω_max) ──► ω_ref
           ▲-                  ▲+
           │              ω_ff (setpoint rate feed-forward)
         θ_meas (attitude filter)

 ω_ref ──►(Σ)──► Rate PID ──► sat(±U_max) ──► FOC Uq/Iq ──► Motor ──► Roll
           ▲-     (D on measurement, filtered)
         ω_meas (gyro X, notch + LPF)
```

**Bandwidth separation rule.** The outer crossover should be 5–10× lower than the inner crossover. That way the inner loop looks like a unity gain to the angle loop.

**Why this split works:**
- The gyro is clean and fast, so it drives the rate loop.
- The fused angle is slower and drift-corrected, so it drives the angle loop.

---

## 2. Outer Angle Loop (1 kHz)

Wrap the error so a ±180° crossing never produces a 360° step:

$$
e_\theta[k] = \operatorname{wrap}_{[-\pi,\pi]}\big(\theta_{ref}[k] - \theta[k]\big)
$$

Use a P controller plus optional small I and rate feed-forward:

$$
\omega_{ref}[k] = \operatorname{sat}_{\pm\omega_{max}}\Big(K_{p\theta}\,e_\theta[k] + I_\theta[k] + \omega_{ff}[k]\Big),
\qquad
\omega_{ff}[k] = \frac{\theta_{ref}[k]-\theta_{ref}[k-1]}{T_{s\theta}}
$$

**Choosing the gains:**
- **Kpθ** approximately sets the outer crossover in rad/s. Kpθ ≈ 2π·8 ≈ 50 s⁻¹ gives about 8 Hz.
- **Ki_θ** is usually unnecessary. The rate loop's integrator already rejects constant torque disturbances such as cable drag or imbalance. If you need it, keep Ki_θ ≤ Kpθ²/10 and use the same clamping logic as Section 4.
- **ω_max** limits slew rate, e.g., 300 °/s ≈ 5.2 rad/s. This also prevents the rate loop from saturating on large angle steps.

---

## 3. Inner Rate Loop (2 kHz): Discrete PID

Error:

$$
e_\omega[k] = \omega_{ref}[k] - \omega_{meas}[k]
$$

### 3.1 Proportional term

$$
P[k] = K_p\, e_\omega[k]
$$

Optional setpoint weighting (b ≈ 0.7–1) reduces overshoot on reference steps: P = Kp(b·ω_ref − ω_meas).

### 3.2 Derivative term: on measurement, with first-order filtering

Take the derivative of −ω_meas, not of the error. This avoids "derivative kick" when ω_ref steps.

The continuous form is:

$$
D(s) = -\frac{K_d\, s}{\tau_d s + 1}\,\omega_{meas}(s),
\qquad \tau_d = \frac{1}{2\pi f_d}
$$

**Discrete form, Tustin (bilinear).** This gives the best phase match and is recommended:

$$
D[k] = \frac{2\tau_d - T_s}{2\tau_d + T_s}\,D[k-1]
\;-\; \frac{2K_d}{2\tau_d + T_s}\big(\omega_{meas}[k]-\omega_{meas}[k-1]\big)
$$

**Discrete form, backward Euler.** This is simpler and unconditionally stable:

$$
\alpha = \frac{\tau_d}{\tau_d + T_s}, \qquad
D[k] = \alpha\,D[k-1] \;-\; \frac{K_d(1-\alpha)}{T_s}\big(\omega_{meas}[k]-\omega_{meas}[k-1]\big)
$$

**Filter design guidance:**
- **Cutoff placement.** Set f_d ≈ 3–5× the rate-loop crossover. With a 50 Hz crossover, f_d ≈ 150–250 Hz.
- **Derivative gain cap.** Equivalently, choose N = Kd/(Kp·τ_d) between 5 and 20. This limits high-frequency gain to Kp(1+N).
- **Aliasing.** f_d must be well below Nyquist: 1 kHz at a 2 kHz loop rate.
- **Gyro pre-filtering.** Before the PID, enable the IMU's internal anti-aliasing filter (~250–500 Hz). If you see motor electrical or PWM harmonics, add a biquad notch at the dominant frame or motor resonance, found with an FFT of logged gyro data.
- **Delay budget.** The total gyro filter delay should stay under about 1 ms. Every 1 ms costs about 18° of phase at 50 Hz.

### 3.3 Integral term

$$
I[k] = I[k-1] + K_i\,T_s\,e_\omega[k] \quad(\text{subject to Section 4})
$$

A trapezoidal version, Ki·Ts·(e[k]+e[k−1])/2, is also fine.

### 3.4 Output

$$
u_{unsat}[k] = P[k] + I[k] + D[k] + u_{ff}[k], \qquad
u[k] = \operatorname{sat}_{\pm U_{max}}(u_{unsat}[k])
$$

The u_ff term is optional static compensation, e.g., cogging or gravity-imbalance feed-forward.

---

## 4. Anti-Windup

### 4.1 Conditional integration (clamping), primary method

Freeze the integrator when the output is saturated *and* integrating would push it further into saturation:

$$
\text{sat\_active} = \big(u_{unsat}[k] \neq u[k]\big), \qquad
\text{same\_sign} = \operatorname{sgn}(e_\omega[k]) = \operatorname{sgn}(u_{unsat}[k])
$$

$$
I[k] =
\begin{cases}
I[k-1] & \text{if sat\_active} \land \text{same\_sign} \\[4pt]
\operatorname{sat}_{\pm I_{max}}\big(I[k-1] + K_i T_s\, e_\omega[k]\big) & \text{otherwise}
\end{cases}
$$

Add an independent hard clamp on the integrator: I_max ≈ 0.5–0.7·U_max. This bounds the worst-case stored correction, e.g., when the gimbal is held by hand.

**Implementation detail.** Compute u_unsat using the *candidate* I, test saturation, then commit or reject the update. This avoids a one-sample lag in the decision.

### 4.2 Back-calculation, alternative method

This gives smoother saturation recovery:

$$
I[k] = I[k-1] + T_s\left(K_i\,e_\omega[k] + \frac{u[k]-u_{unsat}[k]}{T_t}\right),
\qquad T_t \approx \sqrt{T_i T_d} = \sqrt{\tfrac{K_p}{K_i}\cdot\tfrac{K_d}{K_p}}
$$

If Kd = 0, use T_t ≈ 