# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm with 20 kHz PWM Carrier

I don't have the specific geometry from your earlier cycles, so this uses a **representative parametric baseline**. Replace the values in Section 1 with your actual dimensions; the equations and decision logic still apply.

---

## 1. Baseline Assumptions

| Parameter | Symbol | Value |
|---|---|---|
| Material | 6061-T6 Al | E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³ |
| Arm free length (cantilever from yaw/pitch hub) | L | 60 mm |
| Cross-section (width × thickness) | b × h | 20 × 4 mm |
| Tip mass (roll motor + payload share) | M | 50 g |
| Payload inertia about roll axis | J_p | 2×10⁻⁵ kg·m² |
| Structural damping (bolted Al) | ζ | 0.005–0.02 |
| PWM carrier | f_c | 20 kHz |
| IMU gyro output data rate | f_s | 1–8 kHz |

Derived section properties:
- I = bh³/12 = 1.067×10⁻¹⁰ m⁴, so EI = 7.35 N·m²
- A = 8×10⁻⁵ m², so m′ = ρA = 0.216 kg/m and m_beam ≈ 13 g

---

## 2. Bending Modes (Euler–Bernoulli Cantilever)

### 2.1 Bare arm (distributed mass)

$$
f_n = \frac{\lambda_n^2}{2\pi}\sqrt{\frac{EI}{m' L^4}}, \quad \lambda_n = 1.875,\ 4.694,\ 7.855,\ 10.996,\ \ldots
$$

$$
\sqrt{\frac{EI}{m'L^4}} = \sqrt{\frac{7.35}{0.216 \times 1.296\times10^{-5}}} \approx 1620\ \text{s}^{-1}
$$

| Mode | λₙ² | fₙ (bare arm) |
|---|---|---|
| 1 | 3.516 | **≈ 907 Hz** |
| 2 | 22.03 | ≈ 5.7 kHz |
| 3 | 61.70 | ≈ 15.9 kHz |
| 4 | 120.9 | ≈ 31.2 kHz |

The 20 kHz carrier falls **between modes 3 and 4** of the bare arm.

### 2.2 With tip mass (the dominant real case), using a Rayleigh lumped model

$$
k_b = \frac{3EI}{L^3} = \frac{3(7.35)}{(0.06)^3} \approx 1.02\times10^5\ \text{N/m}
$$

$$
m_{eff} = M + 0.2357\,m_{beam} = 0.050 + 0.003 = 0.053\ \text{kg}
$$

$$
f_1 = \frac{1}{2\pi}\sqrt{\frac{k_b}{m_{eff}}} \approx \mathbf{221\ Hz}
$$

The tip mass pulls the fundamental down by roughly 4×. Higher modes also shift down, but less, because the tip mass acts like a node-forming constraint.

### 2.3 Strong-axis bending (in-plane, h ↔ b)

The stiffness scales as (b/h)² = 25, so f₁ scales by about 5×, giving **≈ 1.1 kHz** with the tip mass.

---

## 3. Torsional Mode (Roll-Axis Twist)

For a rectangular section, J_t ≈ β b h³ with β ≈ 0.291 at b/h = 5:

$$
J_t \approx 3.73\times10^{-10}\ \text{m}^4,\quad k_t = \frac{GJ_t}{L} \approx 161\ \text{N·m/rad}
$$

$$
f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_p}} \approx \mathbf{452\ Hz}
$$

This mode matters most because it couples directly into the roll control loop and the gyro axis.

---

## 4. Excitation Spectrum Map

| Source | Frequency | Relevance |
|---|---|---|
| PWM carrier & sidebands | f_c ± k·f_e ≈ 20 kHz ± small offsets | Electromagnetic force ripple, audible whine, IMU aliasing |
| 2× carrier (center-aligned SVPWM) | 40 kHz | Usually negligible |
| Electrical / commutation | f_e = p·n/60 (gimbal: < 50 Hz typically) | Low and benign |
| Cogging torque | N_cog·n/60 | Low-speed jitter |
| Rotor/prop vibration (if UAV) | 50–300 Hz plus harmonics | **Most likely to hit f₁ = 221 Hz** |
| Control loop bandwidth | f_bw ≈ 20–80 Hz | Needs f_struct ≥ 5–10× f_bw |

---

## 5. Direct Response to the 20 kHz Carrier

### 5.1 Fundamental mode transmissibility

$$
r = \frac{f_c}{f_1} = \frac{20000}{221} \approx 90, \qquad |H| \approx \frac{1}{r^2 - 1} \approx 1.2\times10^{-4}
$$

The fundamental is **fully isolated** from the carrier (−78 dB).

### 5.2 High-order and local modes (the real risk)

- The arm's modes 3 and 4, plus **local plate modes** of the motor mounting flange and pocketed webs, populate the 10–40 kHz band.
- Aluminum's low damping gives a resonant amplification of Q = 1/(2ζ), which is **25 to 100**.
- Modal participation at those orders is low, so the effect is usually **acoustic noise and fatigue at fillets**, not pointing error.

**Separation criterion:**

$$
\left|\frac{f_{n,local} - f_c}{f_c}\right| \geq 15\%
$$

In practice, avoid any mode in the **17–23 kHz** window.

---

## 6. The Critical Hidden Issue: IMU Aliasing

A 20 kHz vibration is sampled by the gyro and aliases to:

$$
f_{alias} = \left| f_c - N f_s \right|_{\min}
$$

| Gyro ODR f_s | Alias | Consequence |
|---|---|---|
| 1 kHz | **0 Hz** | Appears as **DC gyro bias / drift** |
| 2 kHz | 0 Hz | Same |
| 3.2 kHz | 800 Hz | Out of band, filterable |
| 8 kHz | 4 kHz (at Nyquist) | Marginal |

