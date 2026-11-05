# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm vs. 20 kHz PWM Carrier

> **Note:** I don't have the specific geometry or results from your earlier cycles ("Cycle 8" context). Below is a complete formulation with **assumed baseline parameters**, clearly marked. Replace them with your actual CAD values and the method carries through unchanged.

---

## 1. Problem Definition

**Objective:** Make sure no structural mode of the roll arm is excited by the motor drive's electrical excitation spectrum, centered on the 20 kHz PWM carrier.

**Excitation sources from the drive:**

| Source | Frequency | Notes |
|---|---|---|
| PWM carrier | f_sw = 20 kHz | Current ripple causes torque ripple and magnetostriction |
| Carrier ×2 | 40 kHz | Dominant ripple for center-aligned SVPWM |
| Carrier sidebands | f_sw ± k·f_e | f_e is the electrical commutation frequency |
| Commutation / cogging | f_e = (p/2)·(rpm/60), and the cogging order | Low-frequency band, critical for gimbal stability |
| Control loop | f_loop (e.g. 1–8 kHz) | Interacts with low modes and can cause loop instability |

**Design criterion (typical):** Every natural frequency f_n must sit outside **±20%** of each excitation frequency (±10% minimum if damping is added).

---

## 2. Assumed Baseline (Replace with Actual)

| Parameter | Symbol | Value |
|---|---|---|
| Material | — | Al 6061-T6 |
| Young's modulus | E | 68.9 GPa |
| Density | ρ | 2700 kg/m³ |
| Shear modulus | G | 26 GPa |
| Arm free length | L | 80 mm |
| Cross-section (width × thickness) | b × h | 20 × 5 mm |
| Tip payload (motor + camera/load) | M | 150 g |
| Boundary condition | — | Clamped at motor hub, free at tip (cantilever) |
| Material damping | ζ | 0.001–0.005 |

---

## 3. Analytical Formulation

### 3.1 Section properties

$$A = bh = 1.0\times10^{-4}\ \text{m}^2$$

$$I_{weak} = \frac{bh^3}{12} = 2.083\times10^{-10}\ \text{m}^4, \qquad I_{strong} = \frac{hb^3}{12} = 3.333\times10^{-9}\ \text{m}^4$$

$$EI_{weak} = 14.35\ \text{N·m}^2$$

### 3.2 Bare beam modes (Euler–Bernoulli cantilever)

$$f_n = \frac{(\beta_n L)^2}{2\pi L^2}\sqrt{\frac{EI}{\rho A}}, \qquad \beta_n L = 1.875,\ 4.694,\ 7.855,\ 10.996,\ \dots$$

$$\sqrt{EI/\rho A} = \sqrt{14.35/0.27} = 7.29\ \text{m}^2/\text{s}$$

| Mode | β_nL | Ratio to f₁ | f_n (weak axis) | f_n (strong axis, ×4) |
|---|---|---|---|---|
| 1 | 1.875 | 1.00 | **637 Hz** | 2.55 kHz |
| 2 | 4.694 | 6.27 | **3.99 kHz** | 15.97 kHz ⚠️ |
| 3 | 7.855 | 17.55 | **11.18 kHz** | 44.7 kHz ⚠️ |
| 4 | 10.996 | 34.39 | **21.91 kHz** ⚠️ | 87.6 kHz |

### 3.3 First mode with tip mass (Rayleigh method)

$$k = \frac{3EI}{L^3} = \frac{3(14.35)}{(0.08)^3} = 84{,}100\ \text{N/m}$$

$$m_{eff} = M + \frac{33}{140}m_{beam} = 0.150 + 0.2357(0.0216) = 0.1551\ \text{kg}$$

$$f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}} = \frac{1}{2\pi}\sqrt{\frac{84{,}100}{0.1551}} \approx \mathbf{117\ Hz}$$

The tip mass strongly lowers the **first** mode. Higher modes, whose nodes fall near the tip, are much less affected, so the bare-beam values for modes 3 and 4 stay roughly valid. Confirm them with FEA.

### 3.4 Torsional mode (roll-arm twist)

$$f_t = \frac{1}{2\pi}\sqrt{\frac{K_t}{J_{tip}}}, \quad K_t = \frac{G\,\beta\, b h^3}{L}$$

Here β ≈ 0.291 for b/h = 4 and J_tip is the payload's polar inertia about the arm axis. This mode often lands in the 0.5–3 kHz range and couples directly into the gimbal control loop.

---

## 4. Resonance Response at the Carrier

Single-degree-of-freedom amplification near a mode:

$$H(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad r = \frac{f_{exc}}{f_n}$$

Consider weak-axis mode 4 (21.91 kHz) against the 20 kHz carrier:

- r = 20/21.91 = 0.913, which is a **separation of only 8.7%**
- With ζ = 0.002: H ≈ 1 / √(0.0277 + 0.000013) ≈ **6.0×**
- At exact coincidence, Q = 1/(2ζ) = **250×**

Small manufacturing variation (±0.1 mm on h changes f by about ±2%) or payload changes can push this mode onto the carrier.

**Strong-axis mode 2 (~16 kHz):** r = 1.25, a 25% separation. This is marginal and should be checked by FEA, because 3D effects shift the frequencies.

---

## 5. Compliance Check

| Mode | f_n | Nearest excitation | Separation | Status |
|---|---|---|---|---|
| Bending 1 (with tip mass) | ~117 Hz | Control bandwidth / f_e | — | Check against loop bandwidth (need f₁ > 3–5× BW) |
| Bending 2 weak | 3.99 kHz | Loop rate (if 4 kHz) | ~0% | ❌ if loop = 4 kHz |
| Bending 3 weak | 11.18 kHz | 20 kHz | 44% | ✅ |
| Bending 4 weak | 21.91 kHz | 20 kHz | 8.7% | ❌ **Fails ±20%** |
| Bending 2 strong | 15.97 kHz | 20 kHz | 20% | ⚠️ Marginal |
| Bending 3 strong | 44.7 kHz | 40 kHz (2×f_sw) | 12% | ❌ |

---

## 6. Mitigation Options (ranked)

1. **Move the carrier, not the metal (cheapest).** Raising f_sw to 24–25 kHz clears mode 4. It also moves the carrier above hearing, though switching losses increase. Alternatively, spread-spectrum or randomized PWM (±2 kHz dither) flattens the peak excitation.
2. **Change the thickness.** Since f ∝ h for bending, setting h = 6 mm makes the weak-axis modes ×1.2, putting mode 4 at ~26.3 kHz (+31%) ✅ and mode 3 at ~13.4 kHz ✅. Re-check strong-axis modes against 40 kHz.
3. **Add a pocket or ribs.** A CNC I-section or a rib raises I/A, which pushes f up without a mass penalty.
4. **Add damping.** A constrained-layer damping tape (ζ → 0.02–0.05) cuts Q from 250 to roughly 10–25. Use this as backup, not as the primary fix.
5. **Isolate electrically.** An LC output filter or higher motor inductance reduces current ripple at f_sw, which lowers the excitation amplitude.

---

## 7. Verification Workflow

1. **FEA modal analysis** (ANSYS / SolidWorks