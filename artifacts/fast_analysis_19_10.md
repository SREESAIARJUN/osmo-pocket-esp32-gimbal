# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm under 20 kHz PWM Carrier (Cycle 19)

I don't have the actual Cycle 19 geometry, mass, or controller data. This analysis therefore gives the full formulation, then works one example with **stated assumptions**. Replace the bracketed values with your design data.

---

## 1. Objective & Acceptance Criteria

Verify that no structural mode of the roll arm is excited by motor or drive harmonics, and that the arm does not destabilize the servo loop.

| Criterion | Requirement |
|---|---|
| C1: Control-structure separation | f₁ ≥ 3–5 × roll position-loop bandwidth (≥ 2× velocity-loop BW minimum) |
| C2: Carrier separation | No mode within ±15% of f_c = 20 kHz, its sidebands, or 2f_c = 40 kHz |
| C3: Low-order harmonic separation | No mode within ±15% of n·f_e (n = 1, 2, 6, 12) across the operating speed range |
| C4: Forced response | Dynamic tip displacement < [image-blur / encoder tolerance, e.g. 1 µm] |

---

## 2. Excitation Spectrum

**Electrical frequency:**
$$f_e = \frac{p \cdot N_{rpm}}{60}, \quad p = \text{pole pairs}$$

**Torque ripple harmonics (BLDC/PMSM):** 6f_e and 12f_e, plus cogging at
$$f_{cog} = \frac{\text{LCM}(N_s, 2p)\cdot N_{rpm}}{60}$$

**PWM-induced current and torque ripple (SVPWM, center-aligned):**
$$f_{PWM} = m f_c \pm k f_e, \quad m = 1, 2, \dots;\; k = 0, 2, 4\ldots$$
The dominant terms are at f_c ± 2f_e and 2f_c (40 kHz).

**Ripple torque amplitude:**
$$\Delta T = K_t\,\Delta I_{pp}, \qquad \Delta I_{pp} \approx \frac{V_{dc}}{4 L_s f_c} \text{ (worst case, 50\% duty)}$$

Gimbal roll motors run slowly, so f_e is typically < 50 Hz. This means:
- The **low-frequency** risk is control-loop interaction (C1).
- The **high-frequency** risk is the 3rd/4th bending or torsional modes near 20 kHz, which cause audible whine and micro-jitter (C2).

---

## 3. Analytical Model

### 3.1 First bending mode (cantilever with tip mass: motor + payload)

$$k = \frac{3EI}{L^3}, \qquad m_{eff} = M_{tip} + 0.2357\,m_{beam}$$
$$f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}$$

### 3.2 Higher bending modes (Euler–Bernoulli, unloaded arm)

$$f_n = \frac{(\beta_n L)^2}{2\pi L^2}\sqrt{\frac{EI}{\rho A}}$$

with β_nL = 1.875, 4.694, 7.855, 10.996, 14.137 (≈ (2n−1)π/2 for n ≥ 3).

Frequency ratios to f₁: 1, 6.27, 17.55, 34.39, 56.84.

### 3.3 Torsional mode (arm twisting about its long axis, tip inertia J_tip)

$$k_\theta = \frac{G K_t}{L}, \quad K_t \approx \beta\, b h^3 \;(\text{rectangular}), \qquad f_\theta = \frac{1}{2\pi}\sqrt{\frac{k_\theta}{J_{tip} + \tfrac{1}{3}J_{arm}}}$$

### 3.4 Forced response (modal SDOF)

$$X_n = \frac{F_n / k_n}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \quad r = \frac{f_{exc}}{f_n}, \quad Q = \frac{1}{2\zeta}$$

For bare machined 6061, ζ ≈ 0.001–0.005, so **Q ≈ 100–500**. Any coincidence with an excitation is severe.

---

## 4. Worked Example (Assumed Cycle 19 Values)

**Material:** 6061-T6 (E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³)

**Assumed geometry and loads:**
- Arm: L = 60 mm, b = 12 mm, h = 4 mm (bending in the thin direction)
- M_tip = 80 g

**Section properties:**
- I = bh³/12 = 64 mm⁴ = 6.4×10⁻¹¹ m⁴
- A = 48 mm²
- ρA = 0.1296 kg/m, so m_beam = 7.8 g
- EI = 4.41 N·m²

**First mode with payload:**
- k = 3(4.41)/(0.06)³ = **61.2 kN/m**
- m_eff = 0.080 + 0.2357(0.0078) = 0.0818 kg
- **f₁ ≈ 138 Hz**

**Unloaded arm modes (relevant to the kHz range):**
- f₁,bare = (3.516 / (2π·0.0036)) · √(4.41/0.1296) ≈ **907 Hz**

| Mode | Ratio | f_n | Margin to 20 kHz |
|---|---|---|---|
| 3rd bending | 17.55 | ≈ 15.9 kHz | −20% (borderline vs 15% criterion) |
| 4th bending | 34.39 | ≈ 31.2 kHz | +56% ✔ |
| 5th bending | 56.84 | ≈ 51.5 kHz | vs 40 kHz (2f_c): +29% ✔ |

**Assessment:**
- **C1:** f₁ = 138 Hz supports a roll position-loop bandwidth of about 28–45 Hz. This is adequate for typical stabilization, but marginal if the target is > 50 Hz.
- **C2:** The 3rd mode sits only about 20% below the carrier. Euler–Bernoulli theory over-predicts high modes, because shear deformation and rotary inertia lower them by roughly 5–15% at these mode orders. Pockets, fillets, and bolt joints add further uncertainty. **This mode must be confirmed by FEA and test.**

---

## 5. FEA Procedure

1. **Model:** Solid-mesh the CNC part with quadratic tets, at least 3 elements through the thinnest wall. Model the motor and camera as lumped mass/inertia (RBE3). Represent bolted interfaces with bonded contact plus a joint-stiffness sensitivity case (0.5× to 1.0×).
2. **Modal extraction:** Extract modes 0–50 kHz (covering 2f_c plus margin). Report effective modal mass in each direction