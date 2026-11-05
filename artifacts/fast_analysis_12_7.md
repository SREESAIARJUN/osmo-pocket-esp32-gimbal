# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm under 20 kHz PWM Excitation

I don't have your project's Cycle 12 parameters (arm geometry, payload, motor pole count, loop bandwidth). The formulation below is complete, and the numerical example uses **assumed placeholder values** that you should replace with your actual design data.

---

## 1. Problem Definition

**Objective:** Make sure no structural mode of the roll arm coincides with:
1. **High-frequency electrical excitation.** This means the PWM carrier (f_c = 20 kHz), its harmonics, and its sidebands.
2. **Low-frequency control excitation.** The first mode must sit well above the gimbal or servo control bandwidth.

**Structural model:** The arm is treated as an Euler–Bernoulli cantilever. It is clamped at the roll-motor hub and carries a tip mass (the payload or pitch motor assembly).

---

## 2. Excitation Spectrum (Forcing Side)

### 2.1 Electrical fundamental

$$f_e = \frac{p \cdot n_{rpm}}{60}$$

where p is the number of pole pairs.

### 2.2 PWM-induced torque and force ripple

With SVPWM or sine-triangle PWM, current ripple and Maxwell-force ripple appear at:

$$f_{exc} = m \cdot f_c \pm k \cdot f_e, \quad m = 1,2,3\ldots;\ k = 0, 2, 4\ldots$$

**Dominant components:**

| Component | Frequency |
|---|---|
| Carrier group | 20 kHz ± 2f_e |
| 2nd carrier group (often dominant with center-aligned PWM) | 40 kHz ± f_e |

For a gimbal motor at low speed, f_e is only a few Hz to tens of Hz. The sidebands therefore cluster tightly around 20 kHz and 40 kHz.

### 2.3 Low-frequency excitation

- Cogging torque: f_cog = N_cog · n_rpm / 60
- Control-loop bandwidth: f_BW, typically 20–150 Hz for gimbals
- Sensor and IMU sample-rate aliasing

---

## 3. Structural Natural Frequencies (Response Side)

### 3.1 Material: Al 6061-T6

| Property | Value |
|---|---|
| E | 68.9 GPa |
| G | 26 GPa |
| ρ | 2700 kg/m³ |
| Structural damping ζ | ≈ 0.001–0.005 (Q ≈ 100–500) |

### 3.2 Bending modes: bare cantilever (distributed mass)

$$f_n = \frac{(\beta_n L)^2}{2\pi L^2}\sqrt{\frac{EI}{\rho A}}$$

| Mode n | β_nL | (β_nL)² |
|---|---|---|
| 1 | 1.8751 | 3.516 |
| 2 | 4.6941 | 22.03 |
| 3 | 7.8548 | 61.70 |
| 4 | 10.9955 | 120.9 |
| 5 | 14.1372 | 199.9 |

### 3.3 First bending mode with tip mass (Rayleigh)

$$k_b = \frac{3EI}{L^3}, \qquad f_1 = \frac{1}{2\pi}\sqrt{\frac{k_b}{M_{tip} + 0.236\,m_{arm}}}$$

### 3.4 Torsional mode (roll-axis twist of the arm)

$$J \approx b h^3\left[\tfrac{1}{3} - 0.21\tfrac{h}{b}\left(1 - \tfrac{h^4}{12b^4}\right)\right], \quad k_t = \frac{GJ}{L}, \quad f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{I_{payload}}}$$

### 3.5 Plate and local modes

At 20 kHz, the relevant modes are usually **local panel or flange modes** of pockets and thin webs, not global beam modes. For a thin pocket wall of thickness t and span a, use the plate formula:

$$f_{plate} = \frac{\lambda^2}{2\pi a^2}\sqrt{\frac{E t^2}{12\rho(1-\nu^2)}}$$

Here λ² depends on the boundary conditions (≈ 35.99 for a clamped square plate).

---

## 4. Worked Example (Placeholder Values)

**Assumed values:** L = 80 mm, section b × h = 15 × 4 mm (weak-axis bending), M_tip = 150 g, I_payload = 1.5×10⁻⁴ kg·m².

### 4.1 Section properties

| Quantity | Calculation | Result |
|---|---|---|
| I | 15·4³/12 | 80 mm⁴ = 8.0×10⁻¹¹ m⁴ |
| EI | | 5.51 N·m² |
| A | | 60 mm² |
| ρA | | 0.162 kg/m |
| m_arm | | ≈ 13 g |
| √(EI/ρA) | | 5.83 m²/s |

### 4.2 Bare-beam bending modes and carrier margin

| Mode | f_n | Margin to 20 kHz |
|---|---|---|
| 1 | 510 Hz | — |
| 2 | 3.20 kHz | — |
| 3 | 8.95 kHz | — |
| **4** | **17.5 kHz** | **−12.3% ⚠️** |
| 5 | 29.0 kHz | +45% (and −27.5% from 40 kHz) |

### 4.3 With tip mass

- k_b = 32.3 kN/m
- **f₁ ≈ 73 Hz**

The higher modes shift only slightly, because the tip mass mainly affects mode 1.

### 4.4 Torsion

- J ≈ 266 mm⁴
- k_t ≈ 86.5 N·m/rad
- **f_t ≈ 121 Hz**

---

## 5. Acceptance Criteria

**1. Carrier separation.** For every mode n and every excitation component:

$$\left|\frac{f_n - f_{exc}}{f_{exc}}\right| \geq 15\text{–}20\%$$

In the example, **mode 4 (17.5 kHz) fails a 15% margin.**

**2. Control-bandwidth separation:**

$$f_{1},\ f_t \;\geq\; 3\text{–}5 \times f_{BW}$$

In the example, if f_BW = 30 Hz, f₁ = 73 Hz gives only a 2.4× ratio. This is marginal.

**3. Dynamic amplification check** (if separation cannot be achieved):

$$|H(r)| = \frac{1}{\sqrt{(1-r^2)^2