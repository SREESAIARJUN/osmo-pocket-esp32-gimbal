# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm (Cycle 3)

> **Note:** I don't have your Cycle 1–2 data, so this uses a parameterized model with representative values. These are a 6061-T6 roll arm of a motor-driven gimbal with FOC/SVPWM at a 20 kHz carrier. Replace the values marked ⟨ ⟩ with your actual geometry and masses.

---

## 1. Problem Statement

Verify that the roll arm's structural modes are adequately separated from all motor-induced excitation frequencies. There are two separate concerns:

| Band | Source | Risk |
|---|---|---|
| **Low (10 Hz – 2 kHz)** | Control loop bandwidth, cogging, electrical frequency, torque ripple | Loop instability, jitter, limit cycling |
| **High (15 – 45 kHz)** | PWM carrier (20 kHz), sidebands, 2× carrier | Acoustic whine, fretting, fastener loosening, IMU aliasing |

---

## 2. Structural Model

### 2.1 Idealization
- The arm is treated as a **cantilever beam**: fixed at the roll motor stator/hub, free at the payload/pitch-motor mount.
- The pitch motor and payload are a **lumped tip mass M** with rotary inertia J_tip.
- The modes considered are:
  1. Out-of-plane bending (weak axis), which is usually critical.
  2. In-plane bending (strong axis).
  3. Torsion about the arm axis.
  4. Local plate/pocket modes, which matter for the high-frequency band.

### 2.2 Material (6061-T6)

| Property | Value |
|---|---|
| E | 68.9 GPa |
| G | 26 GPa |
| ρ | 2700 kg/m³ |
| ν | 0.33 |
| Structural damping ζ | 0.001 – 0.005 (Q ≈ 100–500) |

The **very low damping** of aluminum is the central concern. Any coincidence between excitation and a mode produces large amplification.

### 2.3 Example Geometry
- L = ⟨80 mm⟩, b = ⟨20 mm⟩, h = ⟨4 mm⟩ (rectangular section)
- Tip mass M = ⟨0.15 kg⟩
- Payload roll inertia J_tip = ⟨1×10⁻⁴ kg·m²⟩

---

## 3. Governing Equations

### 3.1 Section Properties

$$I_{weak} = \frac{b h^3}{12}, \qquad I_{strong} = \frac{h b^3}{12}, \qquad A = bh, \qquad \mu = \rho A$$

### 3.2 Euler–Bernoulli Beam (distributed modes)

$$EI\frac{\partial^4 w}{\partial x^4} + \mu\frac{\partial^2 w}{\partial t^2} = 0$$

The cantilever solution is:

$$f_i = \frac{(\beta_i L)^2}{2\pi L^2}\sqrt{\frac{EI}{\mu}}, \qquad \beta_i L = 1.875,\ 4.694,\ 7.855,\ 10.996,\ 14.137$$

The mode ratios are f_i / f_1 = 1 : 6.27 : 17.55 : 34.39 : 56.85.

### 3.3 Fundamental Mode with Tip Mass (Rayleigh approximation)

$$k_{tip} = \frac{3EI}{L^3}, \qquad m_{eff} = M + 0.236\,m_{arm}, \qquad f_1 = \frac{1}{2\pi}\sqrt{\frac{k_{tip}}{m_{eff}}}$$

### 3.4 Torsional Mode

$$J_t \approx \beta\, b\, h^3 \;\;(\beta = 0.291 \text{ for } b/h = 5), \qquad k_\theta = \frac{GJ_t}{L}, \qquad f_\theta = \frac{1}{2\pi}\sqrt{\frac{k_\theta}{J_{tip}}}$$

### 3.5 Exact Tip-Mass Frequency Equation (for higher modes)

Use this to refine Cycle 3 results, with α = M / (μL):

$$1 + \cos\beta L\cosh\beta L + \alpha\,\beta L\,(\cos\beta L\sinh\beta L - \sin\beta L\cosh\beta L) = 0$$

---

## 4. Worked Example

| Quantity | Calculation | Result |
|---|---|---|
| I_weak | 20·4³/12 | 106.7 mm⁴ = 1.067×10⁻¹⁰ m⁴ |
| EI | 68.9e9 × 1.067e-10 | 7.35 N·m² |
| μ | 2700 × 8×10⁻⁵ | 0.216 kg/m |
| m_arm | 0.216 × 0.08 | 17.3 g |
| k_tip | 3(7.35)/0.08³ | 43,070 N/m |
| m_eff | 0.15 + 0.236(0.0173) | 0.154 kg |
| **f₁ (with payload)** | (1/2π)√(43070/0.154) | **≈ 84 Hz** |
| f₁ (bare arm) | 87.4 × √(7.35/0.216) | ≈ 510 Hz |
| f₂ / f₃ / f₄ / f₅ (bare) | × 6.27 / 17.55 / 34.39 / 56.85 | 3.2 / 8.95 / **17.5** / 29 kHz |
| J_t | 0.291·20·4³ | 372 mm⁴ |
| k_θ | 26e9 × 3.72e-10 / 0.08 | 121 N·m/rad |
| **f_θ** | (1/2π)√(121/1e-4) | **≈ 175 Hz** |

**Observation:** The bare-arm 4th bending mode (~17.5 kHz) and the local pocket/plate modes can fall within ±20% of the 20 kHz carrier. This is the most likely root cause of audible whine in a PWM-driven gimbal.

---

## 5. Excitation Spectrum

### 5.1 High-Frequency (PWM-Related)
With center-aligned SVPWM at f_sw = 20 kHz and electrical frequency f_e:

$$f_{exc} = m\,f_{sw} \pm n\,f_e$$

| Component | Frequency |
|---|---|
| Carrier group | 20 kHz ± 2f_e, ± 4f_e |
| 2× carrier group (often dominant in current ripple) | 40 kHz ± f_e |
| Radial (Maxwell) force ripple | Related to the current ripple, transmitted through the stator to the arm |

### 5.2 Low-Frequency (Motor/Control-Related)

$$f_e = p \cdot \frac{n_{rpm}}{60}, \qquad f_{cog} = \text{LCM}(N_s,