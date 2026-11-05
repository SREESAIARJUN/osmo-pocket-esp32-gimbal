# Mechanical Resonance and Natural Frequency Analysis: CNC Aluminum Roll Arm under a 20 kHz PWM Carrier (Cycle 6)

I don't have your specific Cycle 6 drawing data, so the analysis below is fully parameterized. A worked example uses representative gimbal-arm values. Replace these with your actual dimensions and masses.

---

## 1. Problem Definition

**Structure:** A CNC-machined 6061-T6 roll arm. It is clamped at the roll-motor hub and carries the pitch motor and payload at its free end.

**Excitation sources:**

| Source | Frequency | Typical significance |
|---|---|---|
| PWM carrier (SVPWM) | f_c = 20 kHz, plus sidebands at f_c ± 2f_e and 2f_c ± f_e | Low torque, but can excite high-order modes, causing acoustic or ultrasonic whine |
| Cogging torque | N_cog · f_mech, where N_cog = LCM(slots, poles) | Low to mid frequency |
| Torque ripple (commutation) | 6·f_e, 12·f_e | Low frequency |
| Control loop (position/rate) | Crossover f_bw ≈ 20–60 Hz | **Dominant risk:** structural mode coupling and limit cycling |
| IMU sampling / aliasing | f_s ± f_mode | Can alias high-frequency vibration into the loop band |

**Key insight:** The 20 kHz carrier rarely threatens the first structural mode. It matters for high-order modes. The fundamental mode matters for control stability.

---

## 2. Material Constants (6061-T6)

| Property | Value |
|---|---|
| E | 68.9 GPa |
| G | 26 GPa |
| ρ | 2700 kg/m³ |
| Structural damping ζ | 0.001–0.005, so Q = 1/(2ζ) ≈ 100–500 |

The very low damping means any coincident resonance is amplified by roughly 100–500×.

---

## 3. Analytical Formulation

### 3.1 First Bending Mode: Cantilever with Tip Mass (Rayleigh)

$$
k_b = \frac{3EI}{L^3}, \qquad m_{eff} = M_{tip} + \frac{33}{140}\,m_{arm}
$$

$$
f_{1,b} = \frac{1}{2\pi}\sqrt{\frac{k_b}{m_{eff}}}
$$

Evaluate this about both section axes:
- Weak axis: I = b·h³/12
- Strong axis: I = h·b³/12

### 3.2 First Torsional Mode

$$
k_t = \frac{GJ}{L}, \qquad J \approx \beta\, b\, h^3 \quad (\beta \approx 0.291 \text{ for } b/h = 5)
$$

$$
f_{1,t} = \frac{1}{2\pi}\sqrt{\frac{k_t}{I_{tip}}}
$$

Here I_tip is the payload's mass moment of inertia about the arm's long axis.

### 3.3 Higher-Order Bending Modes (Euler–Bernoulli, for the 20 kHz Check)

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}
$$

The mode constants are β_nL = 1.875, 4.694, 7.855, 10.996, 14.137, … For n ≥ 3, these approach (2n−1)π/2.

The tip mass mainly lowers mode 1. Higher modes are bounded between the clamped-free and clamped-pinned values.

### 3.4 Forced Response Near Resonance

$$
\frac{X}{X_{st}} = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad r = \frac{f_{exc}}{f_n}
$$

---

## 4. Worked Example

**Assumed inputs:**
- L = 80 mm
- Rectangular section, b = 20 mm × h = 4 mm
- M_tip = 0.25 kg
- I_tip = 2×10⁻⁴ kg·m²

| Quantity | Calculation | Result |
|---|---|---|
| Area A | 20 × 4 | 80 mm² |
| Arm mass | ρ·A·L | 17.3 g |
| I (weak axis) | 20·4³/12 | 1.067×10⁻¹⁰ m⁴ |
| EI | | 7.35 N·m² |
| k_b | 3EI/L³ | 43.1 kN/m |
| m_eff | 0.25 + 0.236·0.0173 | 0.254 kg |
| **f₁ bending, weak axis** | | **≈ 65 Hz** |
| f₁ bending, strong axis | I is 25× larger, so f scales by ×5 | ≈ 330 Hz |
| J (torsion constant) | 0.291·20·4³ | 3.73×10⁻¹⁰ m⁴ |
| k_t | GJ/L | 121 N·m/rad |
| **f₁ torsion** | | **≈ 124 Hz** |
| Bare-beam mode 4 | (10.996)²·911.5/2π | ≈ 17.5 kHz |
| Bare-beam mode 5 | (14.137)²·911.5/2π | ≈ 29.0 kHz |

---

## 5. Evaluation Criteria

| Check | Criterion | Example result | Status |
|---|---|---|---|
| Control-structure separation | f₁ ≥ 3–5 × f_bw. With f_bw = 40 Hz, f₁ must be ≥ 120–200 Hz | 65 Hz (bending), 124 Hz (torsion) | **FAIL** |
| Carrier separation | No mode within ±15–20% of f_c (16–24 kHz) or of the sidebands | Mode 4 at 17.5 kHz, 12.5% below | **MARGINAL** |
| 2f_c (40 kHz) | ±15%