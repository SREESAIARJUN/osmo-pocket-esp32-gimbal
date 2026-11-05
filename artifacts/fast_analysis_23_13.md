# Resonance and Natural Frequency Analysis: CNC Aluminum Roll Arm under 20 kHz PWM Carrier

I don't have the geometry or data from your "Cycle 23," so this gives the general formulation plus a worked example with **assumed dimensions**. Substitute your measured values and the procedure carries over directly.

---

## 1. Problem Definition

**Goal:** Ensure no structural mode of the roll arm is excited by electromagnetic forces at the motor's PWM carrier frequency, its harmonics, or its sidebands.

**Model the arm as:** a cantilever fixed at the roll motor hub, free (or tip-loaded) at the payload or next-axis end.

**Assumed baseline (replace with yours):**

| Parameter | Symbol | Value |
|---|---|---|
| Material | 6061-T6 | E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³ |
| Free length | L | 60 mm |
| Width | b | 20 mm |
| Thickness | t | 4 mm |
| Tip mass (motor + payload) | M | 80 g |
| Damping ratio (bolted Al) | ζ | 0.005–0.02 |

---

## 2. Excitation Spectrum (Forcing Side)

With a 20 kHz carrier, the phase voltage and current spectra contain:

- **Carrier group:** f_c ± k·f_e, where k = 2, 4… for SVPWM. Here f_c = 20 kHz.
- **Second carrier group:** 2f_c ± k·f_e ≈ 40 kHz. This group is dominant in phase current ripple for center-aligned PWM.
- **Maxwell / magnetostrictive force:** this force is proportional to B², so it doubles frequencies. Expect lines near 2f_c and also at f_c-related products.
- **Electrical frequency:** f_e = p·n/60, where p is pole pairs and n is speed in rpm. For a gimbal, f_e is low (tens of Hz), so sidebands sit tightly around 20 kHz and 40 kHz.

**Forbidden bands** (for a separation margin of ±15–20%):

$$
\text{Band A: } 16\text{–}24\ \text{kHz}, \qquad \text{Band B: } 32\text{–}48\ \text{kHz}
$$

---

## 3. Bending Modes (Euler–Bernoulli Cantilever)

The bending natural frequencies are:

$$
f_i = \frac{(\beta_i L)^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}
$$

with the cantilever eigenvalues βᵢL = 1.875, 4.694, 7.855, 10.996.

**Section properties:**
- I = bt³/12 = 1.067×10⁻¹⁰ m⁴
- EI = 7.35 N·m²
- ρA = 0.216 kg/m (beam mass ≈ 13 g)
- √(EI/ρAL⁴) ≈ 1620 s⁻¹

**Resulting bending frequencies (bare arm):**

| Mode | (βL)² | f_n | Status vs. bands |
|---|---|---|---|
| B1 | 3.516 | 907 Hz | clear |
| B2 | 22.03 | 5.68 kHz | clear |
| B3 | 61.70 | 15.9 kHz | **marginal: −20.5% from 20 kHz** |
| B4 | 120.9 | 31.2 kHz | **marginal: −2.6% from Band B edge** |

**Fundamental with tip mass (Rayleigh method):**

$$
k = \frac{3EI}{L^3} = 1.02\times10^5\ \text{N/m}, \qquad m_{\text{eff}} = M + 0.236\,m_{\text{beam}} = 0.083\ \text{kg}
$$

$$
f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{\text{eff}}}} \approx 176\ \text{Hz}
$$

This mode matters for the **control loop**, not the PWM. The tip mass primarily lowers low-order modes. Higher modes shift toward pinned-tip eigenvalues and must be confirmed by FEA.

---

## 4. Torsional Modes (often the critical one for a roll arm)

The torsional natural frequencies are:

$$
f_n = \frac{2n-1}{4L}\sqrt{\frac{GJ}{\rho I_p}}
$$

**Section properties:**
- J ≈ β·b·t³, with β = 0.291 for b/t = 5, giving J = 3.73×10⁻¹⁰ m⁴
- I_p = bt(b² + t²)/12 = 2.77×10⁻⁹ m⁴
- Torsional wave speed ≈ 1137 m/s

**Resulting torsional frequencies:**

| Mode | f_n | Status |
|---|---|---|
| T1 | 4.74 kHz | clear |
| T2 | 14.2 kHz | clear (−29%) |
| T3 | 23.7 kHz | **inside Band A (+18.5%)** |

Note that bending modes in the thin direction (t) are shown above. The in-plane (b-direction) bending modes scale by b/t = 5. In-plane B1 ≈ 4.5 kHz and B2 ≈ 28 kHz, so check these against Band B as well.

---

## 5. Response Assessment

Dynamic amplification at frequency ratio r = f/f_n is:

$$
H(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad Q = \frac{1}{2\zeta} \approx 25\text{–}100
$$

**Design criteria:**
1. **Separation margin:** |f_n − f_exc| / f_exc ≥ 15–20%. Equivalently, the mode should sit outside about 3 half-power bandwidths, where Δf = 2ζf_n.
2. **Amplification limit:** H ≤ 1.5 at all carrier lines. At a 20% margin, H ≈ 2.2–3, which is acceptable only if the forcing amplitude is small.
3. **Acoustic / imaging check:** 20 kHz is near inaudible, but resonant arm motion can show up as image jitter or IMU aliasing. Check the IMU sample rate against f_c folding.

---

## 6. Mitigation Options (if a mode falls in a band)

| Lever | Effect |
|---|---|
| Increase t | Bending f scales with t. A 4→5 mm change moves B3 to ~19.9 kHz, which is worse. Choose the direction deliberately. |
| Add ribs or a pocket-frame section | Raises I and J with little mass. Pushes torsional modes up. |
| Change carrier | Move to 24–32 kHz, or use spread-spectrum / random PWM to smear lines. |
| Damping | Constrained-layer damping, viscoelastic washers at the joint. Raises ζ toward 0.05. |
| Joint stiffness | Bolt preload and dowel pins. Real f_n is often 10–30% below clamped theory. |

---

## 7. Verification Procedure

1. **FEA modal analysis:** first 15 modes up to 50 kHz, including the actual pocketing, fillets, motor and payload masses, and a bolted-contact or spring boundary condition.
2. **Campbell / interference diagram:** f_n versus speed, overlaid with