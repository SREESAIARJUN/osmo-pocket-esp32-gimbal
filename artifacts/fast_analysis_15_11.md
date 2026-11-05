# Roll Arm Resonance & Natural Frequency Analysis (Cycle 15)

**Note:** I don't have the geometry or data from earlier cycles. The numbers below use **stated placeholder assumptions** (6061-T6 arm, 80 mm cantilever, roll motor and payload at the tip). Substitute your actual dimensions, and the method carries through unchanged.

---

## 1. Problem Framing

The 20 kHz PWM carrier is **not** a threat to the arm's fundamental mode, which will sit in the 100–800 Hz range. The real risks are:

1. **Low-order modes** (bending and torsion) interacting with the **gimbal control loop**. This is the dominant risk.
2. **High-order beam and plate modes** landing near **20 kHz, 40 kHz, and their sidebands**. This causes audible whine, fretting, and fastener loosening.
3. **Aliasing** of carrier-related vibration into the IMU or gyro sample band.

The analysis must therefore cover the full modal spectrum, not just f₁.

---

## 2. Assumed Parameters (replace with Cycle 15 values)

| Parameter | Symbol | Value |
|---|---|---|
| Material | 6061-T6 | E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³ |
| Free length | L | 80 mm |
| Section (width × thickness) | b × h | 20 × 6 mm (bending about the weak axis) |
| Tip mass (motor + payload share) | M | 0.150 kg |
| Tip rotary inertia about arm axis | J_tip | 1.0 × 10⁻⁴ kg·m² |
| Structural damping (bolted Al) | ζ | 0.005–0.02 |

**Derived section properties:**
- I = bh³/12 = 3.6 × 10⁻¹⁰ m⁴ → EI = 24.8 N·m²
- A = 1.2 × 10⁻⁴ m² → m′ = ρA = 0.324 kg/m, beam mass m_b = 25.9 g

---

## 3. Bending Modes: Euler–Bernoulli Cantilever

### 3.1 Bare beam (upper bound, used for the high-order modes)

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{m' L^4}}
$$

with (β_nL) = 1.875, 4.694, 7.855, 10.996, …

$$
\sqrt{\frac{EI}{m'L^4}} = \sqrt{\frac{24.8}{0.324 \times 4.096\times10^{-5}}} = 1367\ \text{s}^{-1}
$$

| Mode | (βL)² | f_n (bare) |
|---|---|---|
| 1 | 3.516 | **765 Hz** |
| 2 | 22.03 | 4.79 kHz |
| 3 | 61.70 | 13.4 kHz |
| 4 | 120.9 | 26.3 kHz |
| 5 | 199.9 | 43.5 kHz |

### 3.2 With tip mass (realistic fundamental, Rayleigh method)

$$
k = \frac{3EI}{L^3} = \frac{3(24.8)}{5.12\times10^{-4}} = 1.453\times10^{5}\ \text{N/m}
$$

$$
m_{eff} = M + 0.2427\,m_b = 0.150 + 0.0063 = 0.1563\ \text{kg}
$$

$$
f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}} = \frac{1}{2\pi}\sqrt{9.30\times10^{5}} \approx \mathbf{153\ Hz}
$$

The tip mass pulls f₁ down by a factor of about 5. If the motor's rotary inertia about the bending axis is significant, f₁ drops further. Correct for it with a tip-mass-plus-inertia cantilever formulation or with FEA.

---

## 4. Torsional Mode (twist about the arm's long axis)

For a rectangular section with b/h = 3.33, β ≈ 0.29:

$$
J_t = \beta b h^3 = 0.29(20)(6^3) = 1253\ \text{mm}^4 = 1.253\times10^{-9}\ \text{m}^4
$$

$$
k_\theta = \frac{GJ_t}{L} = \frac{26\times10^9 \times 1.253\times10^{-9}}{0.08} = 407\ \text{N·m/rad}
$$

$$
f_\theta = \frac{1}{2\pi}\sqrt{\frac{k_\theta}{J_{tip}}} = \frac{1}{2\pi}\sqrt{4.07\times10^{6}} \approx \mathbf{321\ Hz}
$$

Open, thin sections are weak in torsion. If the CNC arm is pocketed or C-channel, J_t can drop by an order of magnitude. Check this first.

---

## 5. Excitation Spectrum Map

| Source | Frequency | Notes |
|---|---|---|
| PWM carrier | f_sw = 20 kHz | Torque and force ripple, magnetostriction |
| Current ripple (center-aligned SVPWM) | 2·f_sw = 40 kHz | Often dominant in phase current |
| Carrier sidebands | f_sw ± k·f_e, 2f_sw ± k·f_e | f_e = p·n/60 is small for a gimbal, so the sidebands are tightly clustered |
| Cogging | N_cog · n/60, with N_cog = LCM(slots, poles) | Low frequency, overlaps the control band |
| Electrical fundamental and torque ripple | f_e, 6f_e | Low frequency |
| Position/rate control loop | BW ~ 20–80 Hz | **Primary interaction with f₁** |
| IMU sample rate | f_s (e.g. 1–8 kHz) | Alias check: f_alias = abs(f_vib − k·f_s) |

---

## 6. Separation Criteria & Assessment

**Rules:**
- Excitation lines: f_n must lie outside f_exc ± 20%.
- Control interaction: f₁ ≥ 5× the loop crossover (≥3× is the minimum, and then only with a notch filter).
- Resonance amplification: Q = 1/(2ζ), which gives **25–100** for bolted aluminum. Any coincidence is severe.

| Check | Mode / value | Exclusion band | Result |
|---|---|---|---|
| Carrier 20 kHz | Mode 3 = 13.4 k, Mode 4 = 26.3 k | 16–24 kHz | ✅ Clear (bare beam; verify with tip mass in FEA) |
| 2·f_sw = 40 kHz | Mode 5 = 43.5 kHz | 32–48 kHz | ⚠️ **Inside the band.** Needs FEA confirmation |
| Control loop (assume 40 Hz BW) | f₁