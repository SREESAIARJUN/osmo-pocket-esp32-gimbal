# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm (Cycle 11)

> **Note on inputs:** I don't have your Cycle 11 drawing, so the analysis below is fully parametric. The numerical example uses **assumed reference values** (marked ⚠). Substitute your actual dimensions and masses, or send them and I'll recompute.

---

## 1. Problem Definition

**Objective:** Verify that no structural mode of the roll arm is excited by the motor drive's spectral content. Two frequency bands matter:

| Band | Source | Risk |
|---|---|---|
| **Low (10–500 Hz)** | Control loop bandwidth, cogging torque, imbalance | Loop instability, jitter, oscillation |
| **High (~20 kHz and up)** | PWM carrier (f_c = 20 kHz), its harmonics (2f_c, 3f_c…), and sidebands f_c ± k·f_elec | Audible whine, ripple-torque excitation, fatigue at bolted joints |

---

## 2. Structural Model

The roll arm is idealized as a **cantilever beam**:
- One end is fixed at the yaw/pitch motor hub (clamped).
- The other end carries the roll motor and payload as a lumped tip mass M_tip and tip inertia J_tip.

### 2.1 Material (6061-T6, ⚠ confirm alloy; 7075-T6 has E ≈ 71.7 GPa)

| Property | Value |
|---|---|
| Young's modulus, E | 68.9 GPa |
| Shear modulus, G | 26 GPa |
| Density, ρ | 2700 kg/m³ |
| Structural damping ratio, ζ | 0.001–0.005 (monolithic CNC; very low) |

### 2.2 Reference geometry ⚠

| Parameter | Value |
|---|---|
| Free length, L | 80 mm |
| Width, b | 20 mm |
| Thickness, h | 5 mm (bending in the thin direction) |
| Tip mass, M_tip (motor + payload share) | 150 g |
| Tip polar inertia, J_tip | 1.5×10⁻⁴ kg·m² |

---

## 3. Governing Equations

### 3.1 Euler–Bernoulli bending (distributed mass, no tip mass)

$$
EI\frac{\partial^4 w}{\partial x^4} + \rho A\frac{\partial^2 w}{\partial t^2}=0
\quad\Rightarrow\quad
f_n=\frac{(\beta_nL)^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}
$$

Clamped-free eigenvalues: β₁L = 1.875, β₂L = 4.694, β₃L = 7.855, β₄L = 10.996, and β_nL ≈ (2n−1)π/2 for higher modes.

### 3.2 First bending mode with tip mass (Rayleigh)

$$
k_b=\frac{3EI}{L^3},\qquad m_{eff}=M_{tip}+0.2357\,m_{beam},\qquad
f_1=\frac{1}{2\pi}\sqrt{\frac{k_b}{m_{eff}}}
$$

### 3.3 First torsional mode

For a rectangular section, J_t = β_t·b·h³, with β_t ≈ 0.281 for b/h = 4.

$$
k_t=\frac{GJ_t}{L},\qquad f_t=\frac{1}{2\pi}\sqrt{\frac{k_t}{J_{tip}}}
$$

### 3.4 Forced response (transmissibility)

$$
T(r)=\sqrt{\frac{1+(2\zeta r)^2}{(1-r^2)^2+(2\zeta r)^2}},\quad r=\frac{f_{exc}}{f_n},\qquad Q\approx\frac{1}{2\zeta}
$$

With ζ = 0.001–0.005, **Q ≈ 100–500**. Any coincident mode will ring hard.

---

## 4. Numerical Evaluation (reference values)

**Section properties**
- I = bh³/12 = 208.3 mm⁴ = 2.083×10⁻¹⁰ m⁴
- EI = 14.35 N·m²
- A = 1×10⁻⁴ m², so ρA = 0.27 kg/m
- m_beam = 21.6 g

**Mode 1, bending with tip mass**
- k_b = 3(14.35)/0.08³ = 84,080 N/m
- m_eff = 0.150 + 0.2357(0.0216) = 0.1551 kg
- **f₁ ≈ 117 Hz**

**Torsion**
- J_t = 7.03×10⁻¹⁰ m⁴
- k_t = 228 N·m/rad
- **f_t ≈ 196 Hz**

**Bare-beam bending ladder (local arm modes)**

The base frequency term is √(EI/ρAL⁴) = 1139 s⁻¹.

| Mode | β_nL | f_n | Proximity to 20 kHz |
|---|---|---|---|
| 1 | 1.875 | 637 Hz | — |
| 2 | 4.694 | 3.99 kHz | — |
| 3 | 7.855 | 11.2 kHz | — |
| **4** | **10.996** | **21.9 kHz** | **⚠ only +9.6% from f_c** |
| 5 | 14.137 | 36.2 kHz | 2f_c = 40 kHz, −9.5% |

A tip mass lowers the higher modes slightly, typically pushing mode 4 *toward* 20 kHz.

---

## 5. Assessment Criteria

1. **Carrier separation:** every f_n must lie outside f_c·(1 ± 0.2), i.e. **16–24 kHz**, and likewise around 2f_c (32–48 kHz).
   - Reference result: **FAIL** for mode 4 (21.9 kHz).
   - Reference result: **marginal** for mode 5 versus 2f_c.
2. **Control separation:** f₁ ≥ 5–10× the position/velocity loop bandwidth.
   - With f₁ = 117 Hz, the loop bandwidth must stay ≤ 12–23 Hz.
   - Torsion (196 Hz) couples directly into the roll axis and must be notch-filtered if the loop bandwidth exceeds ~20 Hz.
3. **Sidebands:** check f_c ± k·p·f_mech across the full speed range (p = pole pairs). These sweep ±1–2 kHz and can cross modes that are statically "safe."

**Physical context:** 20 kHz excitation is low-energy. It arises from current-ripple torque and magnetostriction, so it rarely threatens structural integrity. The practical consequences of a high-Q coincidence are:
- audible or ultrasonic whine,
- IMU aliasing (MEMS gyros sample at 1–8 kHz; 20 kHz energy can fold into the band),
- fretting at screw joints.

The **low-frequency modes are the dominant risk** for gimbal performance.

---

## 6. Recommendations for Cycle 12

| Issue | Action |
|---|---|
| Mode 4 near 20 kHz | Shift the carrier to **24–32 kHz** (cheapest fix), *or* change the thickness or add a rib so the mode moves outside 16–24 kHz. Bending scales as f ∝ h, so 5 → 5.5 mm raises it ~10%. |
| Low Q | Add constrained-layer damping (viscoelastic tape with a thin Al skin) or damping washers at the motor mount. These cut Q by 5–20×