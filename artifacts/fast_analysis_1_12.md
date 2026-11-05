# Cascaded PID Design: ESP32-S3 Gimbal Roll Axis (Cycle 1)

I don't have your Cycle 1 hardware spec, so this design uses typical brushless gimbal hardware. The values marked **[ASSUMED]** are placeholders. The equations and code structure stay the same when you substitute your measured values.

---

## 1. System Architecture

```
            ┌──────────── OUTER: Angle loop (1 kHz) ────────────┐   ┌──────── INNER: Rate loop (2 kHz) ────────┐
 θ_ref ──►(+)── e_θ ──► [ P + I (clamped) ] ──(+)──► sat(±ω_max) ──►(+)── e_ω ──► [ P + I + D_filt ] ──► sat(±V_max) ──► FOC (voltage mode) ──► Motor ──► Roll axis
            ▲(−)                               ▲ ω_ff = dθ_ref/dt   ▲(−)                                                                             │
            │                                                       │                                                                                 │
            └──── θ_roll (Mahony/complementary fusion) ◄────────────┴──── ω_gyro_x (IMU, LPF) ◄───────────────────────────────────────────────────────┘
```

| Item | Choice | Rationale |
|---|---|---|
| IMU | ICM-42688-P via SPI, ODR 4 kHz **[ASSUMED]** | Low-latency gyro for the rate loop |
| Motor | 2208-class gimbal BLDC, R ≈ 10 Ω, Kt ≈ 0.06 N·m/A **[ASSUMED]** | Typical for small camera gimbals |
| Drive | SimpleFOC-style voltage-mode FOC, V_supply = 12 V, V_max = ±6 V | Torque ∝ voltage at low speed |
| Inner loop | Rate PID, Ts_ω = 0.5 ms (2 kHz) | About 10× faster than the outer loop |
| Outer loop | Angle PI + rate feedforward, Ts_θ = 1 ms (1 kHz) | Bandwidth about 6× below the inner loop |
| Core mapping | Core 1: control task (timer ISR → task notify). Core 0: Wi-Fi/BLE, telemetry | Keeps radio jitter out of the control loop |
| Math | `float` (single-precision FPU on the S3) | Avoid `double`, which is software-emulated |

---

## 2. Plant Model (Roll Axis)

In voltage mode, motor plus back-EMF gives:

$$
J\dot\omega = \frac{K_t}{R}V - \frac{K_t^2}{R}\omega - \tau_{dist}
\quad\Rightarrow\quad
G_\omega(s)=\frac{\omega}{V}=\frac{k_V}{Js+b}
$$

| Parameter | Value | Note |
|---|---|---|
| J (rotor + camera about roll) | 3.0 × 10⁻⁴ kg·m² **[ASSUMED]** | Measure with a step test (see §8) |
| k_V = Kt/R | 0.006 N·m/V | |
| b = Kt²/R | 3.6 × 10⁻⁴ N·m·s | Pole at b/J ≈ 1.2 rad/s, effectively an integrator at crossover |

Because the plant behaves like an integrator near crossover, a PI controller gives roughly a −20 dB/dec slope through crossover, with comfortable phase margin.

---

## 3. Inner Loop: Rate PID

**Target crossover:** f_c,ω = 40 Hz → ω_c = 251 rad/s. This is limited by gyro latency, PWM delay, and frame resonance (keep it below about 1/3 of the first structural mode).

**Gains (pole placement on the integrator plant):**

$$
K_{p\omega} = \frac{J\,\omega_c}{k_V} = \frac{3\times10^{-4}\cdot 251}{0.006} \approx 12.5\ \tfrac{\text{V}}{\text{rad/s}}
$$

$$
K_{i\omega} = K_{p\omega}\cdot\frac{\omega_c}{5} \approx 630\ \tfrac{\text{V}}{\text{rad}}
\qquad (\text{PI zero at } \omega_c/5 \Rightarrow \approx 70^\circ\ \text{PM before delays})
$$

$$
K_{d\omega} = 0 \text{ to } 0.005\ \tfrac{\text{V·s}}{\text{rad}} \quad (\text{start at 0; add only to damp overshoot})
$$

Two design rules for the rate loop:

- **Use D on measurement, not error.** The derivative of the gyro signal is angular acceleration, which is very noisy. If you use D at all, filter it hard (§5).
- **Check the noise floor.** Gyro noise of about 0.005 rad/s RMS × K_p = 0.06 V RMS of output noise. Acceptable, but watch for motor "hiss."

---

## 4. Outer Loop: Angle PI + Feedforward

**Target crossover:** f_c,θ = 6 Hz → ω_c = 38 rad/s. The inner loop, closed at 40 Hz, looks like unity gain at this frequency.

$$
K_{p\theta} = \omega_{c,\theta} \approx 38\ \text{s}^{-1}
\qquad
K_{i\theta} = K_{p\theta}\cdot\frac{\omega_{c,\theta}}{10}\approx 145\ \text{s}^{-2}
$$

$$
\omega_{ref} = \text{sat}_{\pm\omega_{max}}\!\left(K_{p\theta}e_\theta + I_\theta + \omega_{ff}\right),
\quad \omega_{ff} = \dot\theta_{ref},\quad \omega_{max}=6\ \text{rad/s}\ (\approx 340^\circ/\text{s})
$$

Notes on the outer loop:

- **No D term is needed.** The inner rate loop already provides the damping, since it is effectively "D on angle" using a clean gyro signal.
- **The I term rejects constant torque.** It handles cable drag and camera imbalance that the inner I would otherwise absorb slowly.
- **Wrap the error** to (−π, π]: `e = atan2f(sinf(e), cosf(e))`, or a cheaper fmod-based wrap.

---

## 5. Derivative Filtering

**Continuous form (first-order filtered D):**

$$
D(s) = \frac{K_d\,s}{1+T_f s},\qquad T_f = \frac{1}{2\pi f_D}
$$

Classic choice: T_f = K_d/(N·K_p) with N = 5 to