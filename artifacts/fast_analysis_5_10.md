# Roll Arm Mechanical Resonance & Natural Frequency Analysis (Cycle 5)

I don't have the prior cycles' geometry or results, so this analysis uses **stated baseline assumptions**. Replace them with your Cycle 5 CAD values and the equations still apply.

---

## 1. Problem Framing

The question is whether any structural mode of the roll arm coincides with, or is excited by, motor-induced forcing. The relevant excitation sources are:

| Source | Frequency | Typical relevance |
|---|---|---|
| PWM carrier | f_PWM = 20 kHz | High-order modes, acoustic whine |
| PWM sidebands | 20 kHz ± k·f_e, 40 kHz (2× for center-aligned) | Same band |
| Electrical frequency | f_e = p · (RPM/60), where p is the number of pole pairs | Low-to-mid modes |
| FOC torque ripple | 6·f_e, 12·f_e | Low-to-mid modes |
| Cogging | N_slots·p-related harmonics × mechanical frequency | Low modes |
| Control loop | Position/velocity bandwidth f_BW (typically 20–150 Hz) | **The first mode is critical here** |

**Key insight:** The 20 kHz carrier rarely threatens the *fundamental* mode. It can, however, excite **3rd/4th bending modes or plate modes**. Separately, the **first mode versus control bandwidth** is usually the real stability limiter in a gimbal.

---

## 2. Baseline Assumptions

| Parameter | Value |
|---|---|
| Material | 6061-T6: E = 68.9 GPa, ρ = 2700 kg/m³ |
| Arm model | Cantilever (fixed at roll motor hub) |
| Free length | L = 60 mm |
| Cross-section | b = 20 mm wide × h = 4 mm thick (bending about the thin axis) |
| Tip payload | M = 0.30 kg (pitch motor + camera) |
| Damping | ζ ≈ 0.002–0.005 (bolted Al, Q ≈ 100–250) |

---

## 3. Governing Equations

### 3.1 Euler–Bernoulli Cantilever (Bare Arm)

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\mu L^4}}, \quad I = \frac{bh^3}{12}, \quad \mu = \rho b h
$$

The eigenvalues come from the cantilever characteristic equation:

$$
1 + \cos(\beta L)\cosh(\beta L) = 0
$$

which gives β₁L = 1.875, β₂L = 4.694, β₃L = 7.855, β₄L = 10.996, and approximately (2n−1)π/2 for higher modes.

### 3.2 Cantilever with Tip Mass (Fundamental Mode, Rayleigh)

$$
k = \frac{3EI}{L^3}, \quad m_{eff} = M + 0.236\,\mu L, \quad f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}
$$

If the payload's rotary inertia J is large, add the rotational DOF. That lowers f₁ further.

### 3.3 Torsional Mode (Roll Arm Twisting Under Payload Inertia)

$$
f_t = \frac{1}{2\pi}\sqrt{\frac{GJ_t/L}{J_{payload}}}, \quad J_t \approx \frac{bh^3}{3}\left(1 - 0.63\frac{h}{b}\right), \quad G = 26 \text{ GPa}
$$

### 3.4 Forced Response (Dynamic Amplification)

$$
\frac{X}{X_{static}} = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \quad r = \frac{f_{exc}}{f_n}
$$

At r = 1, the amplification equals Q = 1/(2ζ), which is roughly 100–250× for aluminum. This is why coincidence must be avoided, not merely tolerated.

---

## 4. Worked Numbers (Baseline)

The section properties are:
- I = 20·4³/12 = 106.7 mm⁴ = 1.067×10⁻¹⁰ m⁴
- EI = 7.35 N·m²
- μ = 0.216 kg/m

### 4.1 Bare-Arm Bending Modes

| Mode | Ratio to f₁ | f_n | Distance from 20 kHz |
|---|---|---|---|
| 1 | 1.00 | **906 Hz** | far |
| 2 | 6.27 | 5.68 kHz | far |
| 3 | 17.55 | **15.9 kHz** | −20% ⚠️ |
| 4 | 34.39 | **31.2 kHz** | +56% |

### 4.2 With Tip Mass (Realistic Fundamental)

- k = 3(7.35)/(0.06)³ ≈ 1.02×10⁵ N/m
- m_eff ≈ 0.303 kg
- **f₁ ≈ 92 Hz**

The higher modes shift as well. Tip mass mostly lowers modes 1 and 2, while modes 3 and above stay closer to the bare-beam values because the tip behaves like a near-node or pinned condition. **FEA is required** to place modes 3/4 accurately.

---

## 5. Assessment Criteria

1. **Control-bandwidth separation:**
   f₁ ≥ 3–5 × f_BW.
   With f₁ ≈ 92 Hz, f_BW must stay ≤ about 20–30 Hz. This is likely the **binding constraint**.

2. **Carrier separation margin:**
   No mode within ±15–20% of 20 kHz, 40 kHz, or the 20 kHz ± 2f_e sidebands.
   Mode 3 at ~15.9 kHz sits **at the edge**, so verify it.

3. **Ripple sweep (Campbell diagram):**
   Plot 6f_e and 12f_e against RPM across the operating speed range. Flag every intersection with f₁, f₂, or f_t.

4. **Torsional mode:**
   f_t should also satisfy criterion 1, because it couples directly into roll-axis jitter.

---

## 6. Sensitivity (Design Levers)

| Change | Effect on f (bending) |
|---|---|
| Thickness h ↑ | f ∝ h (bare). With a heavy tip mass, f ∝ h^1.5 |
| Length L ↓ | f ∝ L⁻² (bare), L⁻¹·⁵ (tip mass) |
| Ribs / I-section / closed box | Large I gain for small mass gain. **Best lever** |
| Material → 7075 | Negligible (E is nearly the same). Gains strength, not stiffness |
| Payload mass ↓ | f ∝ M⁻⁰·⁵ |

Example: going from h = 4 to 6 mm gives f₁ ≈ 92 → ~170 Hz.

---

## 7. Mitigation if a Coincidence Is Found

- **Structural:** add ribs, reduce overhang, use fillets at the root (root compliance often dominates), and ensure