# Natural Frequency and Resonance Analysis: CNC Aluminum Roll Arm vs. 20 kHz PWM Carrier (Cycle 7)

I don't have the Cycle 7 drawings or earlier design data, so this uses representative gimbal roll-arm parameters, labeled as assumptions. Replace them with your actual geometry and masses. The method and conclusions carry over.

---

## 1. Problem Definition

**Objective:** Find the roll arm's natural frequencies (f_n) and check whether any mode can be excited by the 20 kHz motor PWM carrier, its harmonics, or its sidebands.

**Assumed baseline (replace with Cycle 7 values):**

| Parameter | Symbol | Value |
|---|---|---|
| Material | – | Al 6061-T6 |
| Young's modulus | E | 68.9 GPa |
| Density | ρ | 2700 kg/m³ |
| Effective arm length (root to motor axis) | L | 80 mm |
| Section width | b | 20 mm |
| Section thickness (bending direction) | h | 4 mm |
| Tip mass (roll motor + payload share) | M_tip | 0.15 kg |
| Root condition | – | Clamped (bolted to yaw/pitch hub) |

---

## 2. Excitation Spectrum from the Drive

A 20 kHz carrier does not excite the arm at a single frequency. It produces several excitation components:

| Source | Frequency | Mechanism |
|---|---|---|
| PWM carrier | f_sw = 20 kHz | Current ripple causes Maxwell/Lorentz force ripple in the stator |
| Ripple doubling (center-aligned SVPWM) | 2·f_sw = 40 kHz | Dominant phase-current ripple component |
| Sidebands | f_sw ± k·f_e, 2f_sw ± k·f_e | Modulation by the electrical frequency f_e |
| Low-frequency torque ripple / cogging | p·n_rpm/60 × harmonics | Usually < 500 Hz in gimbals |
| Control-loop excitation | DC up to servo bandwidth (~50–300 Hz) | Closed-loop interaction |

For a gimbal motor at low speed (f_e of a few Hz), the sidebands sit very close to 20 kHz. The **critical band is about 19.5–20.5 kHz and 39–41 kHz.**

---

## 3. Analytical Model (Euler–Bernoulli Cantilever)

### 3.1 Section properties

$$I = \frac{b h^3}{12} = \frac{(0.020)(0.004)^3}{12} = 1.067\times10^{-10}\ \text{m}^4$$

$$EI = 68.9\times10^9 \times 1.067\times10^{-10} = 7.35\ \text{N·m}^2$$

$$m' = \rho A = 2700 \times (0.020\times0.004) = 0.216\ \text{kg/m}, \qquad m_{beam} = 0.0173\ \text{kg}$$

### 3.2 Bare arm (no tip mass): local high-order mode check

$$f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{m' L^4}}, \qquad \sqrt{\frac{EI}{m'L^4}} = \sqrt{\frac{7.35}{0.216\times4.096\times10^{-5}}} = 911.4\ \text{s}^{-1}$$

| Mode n | β_nL | f_n (bare arm) |
|---|---|---|
| 1 | 1.875 | **510 Hz** |
| 2 | 4.694 | 3.20 kHz |
| 3 | 7.855 | 8.95 kHz |
| 4 | 10.996 | **17.5 kHz** |
| 5 | 14.137 | **29.0 kHz** |
| 6 | 17.279 | 43.3 kHz |

The 20 kHz carrier falls between modes 4 and 5, with margins of +14% and −31%. The 40 kHz ripple component sits about 8% below mode 6, which is a marginal case.

### 3.3 Loaded arm (with motor/payload tip mass): global first mode

Using the Rayleigh approximation:

$$k = \frac{3EI}{L^3} = \frac{3(7.35)}{(0.08)^3} = 43{,}060\ \text{N/m}$$

$$m_{eff} = M_{tip} + 0.2357\, m_{beam} = 0.150 + 0.004 = 0.154\ \text{kg}$$

$$f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}} = \frac{1}{2\pi}\sqrt{\frac{43{,}060}{0.154}} \approx \mathbf{84\ Hz}$$

**Torsional mode** (roll arm twisting about its long axis; matters for an offset payload):

$$f_t = \frac{1}{2\pi}\sqrt{\frac{GJ/L}{J_{tip}}}, \quad G = 26\ \text{GPa},\quad J \approx \beta b h^3\ (\beta\approx0.29\ \text{for } b/h=5)$$

Evaluate this with the actual tip inertia J_tip. It is often the lowest mode in gimbal arms.

---

## 4. Forced Response Assessment

For each mode, use the transmissibility:

$$T(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad r = \frac{f_{exc}}{f_n}$$

Bolted aluminum typically has ζ ≈ 0.01–0.03.

| Check | r | T (ζ=0.02) | Assessment |
|---|---|---|---|
| 20 kHz vs mode 4 (17.5 kHz) | 1.14 | ~3.2 | Mild amplification |
| 20 kHz vs mode 5 (29.0 kHz) | 0.69 | ~1.9 | Acceptable |
| 40 kHz vs mode 6 (43.3 kHz) | 0.92 | ~6.4 | **Watch item** |
| 20 kHz vs global f₁ (84 Hz) | 238 | ≈ 2×10⁻⁵ | Fully isolated |

**Interpretation:**
- Force ripple at 20 kHz is small (milli-newtons), and high-order modes have low modal mass participation. Displacement amplitudes are therefore nanometer-scale, with **no structural or fatigue risk**.
- The real symptoms of near-coincidence are **audible or ultrasonic whine**, coil/stator buzz radiated through the arm, and IMU aliasing if the gyro sits on the arm and samples near these frequencies.
- **The larger engineering risk is the low-frequency global mode (~84 Hz) interacting with the stabilization loop, not the 20 kHz carrier.**

---

## 5. Design Criteria for Cycle 7

1. **Carrier separation:** No mode with significant participation within ±15% of f_sw, 2f_sw, and their sidebands.
2. **Control separation:** First bending/torsion mode ≥ 3–5× the roll-loop bandwidth. For example, an 84 Hz mode supports roughly a 17–28 Hz loop bandwidth. If more is needed, stiffen the arm.
3. **IMU:** Gyro anti-alias filter cutoff well below the first arm mode. Keep the IMU off the thinnest web sections.
4. **Pocketed CNC webs:** Thin floors (< 1.5 mm) form local plate modes that can land at 15–45 kHz. Check them in FEA, since the beam model above cannot capture them.

### Mitigation options, in priority order

| Lever | Effect |
|---|---|
| Increase h (bending thickness) | f ∝ h^1.5 for the global mode; most effective |
| Add ribs / switch to a U- or box-section | Raises both bending and torsion modes |
| Reduce tip mass or move CG toward the root | Raises f₁ |
|