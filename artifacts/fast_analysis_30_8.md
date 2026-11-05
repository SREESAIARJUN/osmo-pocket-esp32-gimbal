# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis

I don't have the details from your earlier design cycles, so this design states its assumptions explicitly. Replace the plant numbers with your measured values. The structure, equations and code apply regardless.

---

## 1. Assumptions and Architecture

| Item | Assumption |
|---|---|
| Actuator | Gimbal BLDC (e.g. GBM2804), FOC voltage/torque mode, commutation ≥ 10 kHz (MCPWM) |
| Sensors | IMU gyro + attitude estimate (e.g. ICM-42688 over SPI, ODR ≥ 2 kHz); motor encoder optional |
| Roll inertia (motor + payload) | J ≈ 2×10⁻⁴ kg·m² (measure it) |
| Command unit | Inner loop outputs torque/Iq command, normalized to ±1 = max |

```
 θ_ref ──►(+)──► Angle PI ──► ω_ref ──►(+)──► Rate PID ──► τ_cmd ──► FOC ──► Motor/Roll ──┐
  │        ▲-     (500 Hz)   (+ feedfwd)  ▲-   (2 kHz)                                    │
  │        │                              └─────────── gyro ω_x (filtered) ◄──────────────┤
  │        └──────────────────── attitude θ_roll (AHRS) ◄─────────────────────────────────┘
  └── d/dt ──► ω_ff ───────────────────────────────┘
```

**Loop rates and bandwidths.** Keep at least 5× separation between nested loops:

| Loop | Rate | Target bandwidth |
|---|---|---|
| FOC current | 10–20 kHz | > 1 kHz |
| Rate (inner) | 2 kHz | 40–80 Hz |
| Angle (outer) | 500 Hz (every 4th inner tick) | 8–15 Hz |

---

## 2. Plant Model (Roll Axis)

$$J\dot\omega = \tau_m - b\,\omega - \tau_d, \qquad \dot\theta = \omega$$

The rate plant is approximately an integrator: ω/τ = 1/(Js + b) ≈ 1/(Js).

Loop-shaping starting gains for a crossover of ω_c (rad/s):

$$K_{p,\omega} \approx J\,\omega_{c,\text{in}}, \qquad K_{i,\omega} \approx K_{p,\omega}\frac{\omega_{c,\text{in}}}{5}, \qquad K_{p,\theta} \approx \omega_{c,\text{out}}$$

**Example** with ω_c,in = 2π·60 ≈ 377 rad/s and ω_c,out = 2π·10 ≈ 63 rad/s:

- K_p,ω ≈ 0.075 N·m/(rad/s), K_i,ω ≈ 5.7 N·m/rad
- K_p,θ ≈ 63 s⁻¹

Treat these as initial values, then tune on hardware.

---

## 3. Inner Rate Loop: PID with Filtered Derivative

### 3.1 Control law
Use P on error, I on error, and **D on measurement** to avoid derivative kick when ω_ref steps:

$$u = K_p e + I - D_f, \qquad e = \omega_{ref} - \omega$$

### 3.2 Derivative filter
The continuous form is a first-order low-pass applied to the derivative:

$$D(s) = \frac{K_d\, s}{1 + T_f s}, \qquad T_f = \frac{K_d}{N K_p},\; N \in [5, 20]$$

Alternatively, specify the cutoff directly: T_f = 1/(2πf_c). Use f_c ≈ 3–5× the loop bandwidth and below Nyquist/4, e.g. **f_c = 150–250 Hz at 2 kHz**.

**Discretization: backward Euler.** This is unconditionally stable for any T_s, which makes it the recommended choice:

$$D_k = \alpha\, D_{k-1} + \frac{K_d}{T_f + T_s}\,(y_k - y_{k-1}), \qquad \alpha = \frac{T_f}{T_f + T_s}$$

With y = ω (measurement), the controller subtracts D_k.

**Discretization: Tustin.** This gives better phase fidelity near f_c:

$$D_k = \frac{2T_f - T_s}{2T_f + T_s} D_{k-1} + \frac{2K_d}{2T_f + T_s}(y_k - y_{k-1})$$

**Notes on the derivative path:**
- On a rate loop, D acts on angular acceleration, which is very noisy. Many gimbals run the rate loop as **PI only**. Add D only if you need extra phase margin with a stiff payload.
- Pre-filter the gyro feeding the whole loop. Use a 2nd-order Butterworth LPF at about 250 Hz, plus a **notch** at any frame/payload resonance identified from a sweep. Count this filter's phase lag in your margin budget.

### 3.3 Integral (forward Euler)

$$I_{k} = I_{k-1} + K_i T_s\, e_k \quad (\text{subject to anti-windup, §5})$$

---

## 4. Outer Angle Loop: PI + Rate Feed-forward

$$e_\theta = \operatorname{wrap}_{[-\pi,\pi]}(\theta_{ref} - \theta)$$

$$\omega_{ref} = \operatorname{sat}_{\pm\omega_{max}}\!\left(K_{p,\theta}\, e_\theta + I_\theta + K_{ff}\,\dot\theta_{ref}\right)$$

- **Feed-forward.** Set K_ff ≈ 1. Compute θ̇_ref from the filtered derivative of the setpoint (same filter form as §3.2), or directly from a trajectory generator. This removes most of the lag when following joystick or follow-mode commands.
- **D term.** Not needed. The inner loop already supplies rate damping, so an outer D duplicates it with extra noise.
- **Integral.** Keep it small (K_i,θ ≈ K_p,θ·ω_c,out/10). It is only for an unbalanced payload or cable drag.
- **Rate limit.** Use ω_max ≈ 3–6 rad/s for smooth footage.

---

## 5. Anti-Windup

Define:

$$u_{\text{unsat}} = K_p e + I - D, \qquad u = \operatorname{sat}(u_{\text{unsat}}, u_{min}, u_{max})$$

### 5.1 Conditional integration (clamping), used on the inner loop
Freeze the integrator when the output is saturated **and** the error would drive it further into saturation:

$$\text{sat\_flag} = (u_{\text{unsat}} \neq u) \;\wedge\; \operatorname{sign}(e) = \operatorname{sign}(u_{\text{unsat}})$$

$$I_k = \begin{cases} I_{k-1} & \text{if sat\_flag} \\ I_{k-1} + K_i T_s e_k & \text{otherwise} \end{cases}$$

Also apply a hard bound as a safety net:

$$I_k = \operatorname{sat}(I_k, I_{min}, I_{max}), \qquad |I_{max}| \approx 0.5\text{–}0.7\,u_{max}$$

### 5.2 Back-calculation (alternative, smoother recovery)

$$I_k = I_{k-1} + T_s\left[K_i e_k + \frac{1}{T_t}(u_k - u_{\text{unsat},k})\right], \qquad T_t \approx \sqrt{T_i T_d}\ \ (\text{or } T_t = T_i \text{ for PI})$$

Here T_i = K_p/K_i and T_d = K_d/K_p.

### 5.3 Cascade anti-windup (important)
When the **inner** loop saturates, the outer loop does not see it and its integrator winds up. To prevent this:
- Freeze