# Roll Arm Natural Frequency & Resonance Analysis vs. 20 kHz PWM Carrier (Cycle 10)

I don't have your Cycle 10 geometry or mass data, so this uses a representative 6061-T6 roll arm. All equations are parametric. Substitute your values, and use the worked numbers as a template and sanity check.

---

## 1. Problem Definition

**Objective:** Make sure no structural mode of the roll arm is excited by the motor drive's electrical forcing, and that the first mode stays well above the control-loop bandwidth.

**Excitation sources from a 20 kHz carrier:**

| Source | Frequency | Mechanism |
|---|---|---|
| PWM current ripple (edge-aligned) | f_c = 20 kHz | Torque ripple, magnetostriction |
| Center-aligned SVPWM ripple | 2f_c = 40 kHz | Dominant ripple component |
| Carrier sidebands | f_c ± k·f_e (k = 1, 2, 3…) | f_e = electrical frequency = p·n/60 |
| Low-frequency excitation | f_e, 6f_e (cogging/commutation) | Relevant to modes 1–2 |

**Excitation band of concern:** about 18–22 kHz and 38–42 kHz, plus the sidebands.

---

## 2. Assumed Baseline (replace with Cycle 10 data)

| Parameter | Symbol | Value |
|---|---|---|
| Material | 6061-T6 | E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³ |
| Free length | L | 80 mm |
| Section (width × thickness) | b × h | 20 × 5 mm |
| Tip mass (motor + payload) | M_tip | 0.150 kg |
| Tip polar inertia (about arm axis) | J_tip | 1.0×10⁻⁴ kg·m² |
| Boundary condition | — | Clamped at the yaw-side hub, free at the roll motor |

---

## 3. Analytical Formulation

### 3.1 Section properties
$$I = \frac{b h^3}{12} = 2.083\times10^{-10}\ \text{m}^4,\quad A = bh = 1.0\times10^{-4}\ \text{m}^2$$
$$EI = 14.35\ \text{N·m}^2,\quad \rho A = 0.27\ \text{kg/m},\quad m_{beam} = 21.6\ \text{g}$$

### 3.2 Bare-beam bending modes (Euler–Bernoulli cantilever)
$$f_n = \frac{(\beta_n L)^2}{2\pi L^2}\sqrt{\frac{EI}{\rho A}}$$

with βₙL = 1.875, 4.694, 7.855, 10.996.

| Mode | (βₙL)² | f_n (bare arm) |
|---|---|---|
| 1 | 3.516 | **637 Hz** |
| 2 | 22.03 | 3.99 kHz |
| 3 | 61.70 | 11.2 kHz |
| 4 | 120.9 | **21.9 kHz ⚠** |

### 3.3 Fundamental with tip mass (Rayleigh)
$$k = \frac{3EI}{L^3} = 84.1\ \text{kN/m},\qquad m_{eff} = M_{tip} + 0.2357\,m_{beam} = 0.155\ \text{kg}$$
$$f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}} \approx \mathbf{117\ Hz}$$

### 3.4 First torsional mode (roll axis)
$$J_t = \beta_t\, b h^3 \quad (\beta_t = 0.281 \text{ for } b/h = 4) = 7.03\times10^{-10}\ \text{m}^4$$
$$k_t = \frac{G J_t}{L} = 229\ \text{N·m/rad},\qquad f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_{tip}}} \approx \mathbf{241\ Hz}$$

### 3.5 Excitation amplitude (PWM torque ripple)
Peak-to-peak current ripple at 50% duty:
$$\Delta i_{pp} = \frac{V_{dc}}{4 L_{ph} f_c}$$

Example: with V_dc = 16 V and L_ph = 200 µH, Δi ≈ 1.0 A, and the resulting torque ripple is T_ripple ≈ K_t · Δi / 2.

### 3.6 Forced response and amplification
$$|H(r)| = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}},\quad r = \frac{f_{exc}}{f_n}$$

Monolithic CNC aluminum has very low damping: ζ ≈ 0.001–0.005, so Q = 1/(2ζ) ≈ 100–500. A coincident mode amplifies the response by 100–500×.

---

## 4. Evaluation Criteria

| Criterion | Requirement | Baseline result | Status |
|---|---|---|---|
| Separation margin from f_c | \|f_n − f_c\| / f_c ≥ 20% | Mode 4: 21.9 kHz, 9.5% margin | ❌ FAIL |
| Separation from 2f_c | ≥ 20% | Nearest mode ~ 40 kHz must be checked by FEA | ⚠ Verify |
| f₁ vs. control bandwidth | f₁ ≥ 5 × BW_loop | 117 Hz → BW ≤ 23 Hz | ⚠ Marginal if BW > 25 Hz |
| Torsional f_t vs. roll loop | f_t ≥ 5 × BW | 241 Hz → BW ≤ 48 Hz | ✔ OK for typical gimbals |
| Acoustic | No audible whine near 18–20 kHz | Mode 4 risk | ⚠ |

**Note:** The tip mass lowers the higher modes as well as the fundamental, so the in-assembly mode 4 will likely sit between 18 and 21 kHz. That moves it closer to the carrier, not away from it. The analytical estimate understates the risk.

---

## 5. FEA Verification Procedure

1. **Model:** Import the actual CNC geometry, including pockets, fillets and bolt bosses. Use a second-order tet mesh with at least 3 elements through the thinnest wall.
2. **Boundary conditions:**
   - Use bonded/bolted contact at the hub, not an ideal fixed support. Ideal clamping overestimates f_n by 10–30%.
   - Model the motor and payload as a remote point mass carrying the correct inertia tensor.
3. **Solve:** Run a modal analysis to 50 kHz (≥ 2.5 × f_c), at least 30 modes.
4. **Prestress:** Include bolt preload if the joints are thin.
5. **Campbell diagram:** Plot f_n against motor speed with the f_c ± k·f_e lines overlaid, and flag any intersections.
6. **Harmonic response:** Apply the torque ripple from §3.5 at the motor mount over 15–45 kHz with ζ = 0.002. Report tip acceleration and the IMU-location acceleration.

---

## 6. Experimental Validation

- **Impact