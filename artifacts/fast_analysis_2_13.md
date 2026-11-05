# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm (Cycle 2)

I don't have your Cycle 1 geometry or data, so this uses a **parametric formulation with representative baseline values**. Replace the bracketed values with your actual dimensions. The method and decision criteria carry over directly.

---

## 1. Objective & Pass Criteria

**Goal:** Verify that no structural mode of the roll arm, with the motor attached, is excited by the motor drive's switching or electromagnetic content, and that the first mode does not limit the gimbal control loop.

| Criterion | Requirement |
|---|---|
| C1 – Carrier avoidance | No mode within ±15% of f_sw = 20 kHz or 2·f_sw = 40 kHz |
| C2 – Control separation | f₁ ≥ 5 × position-loop bandwidth (e.g., ≥ 100 Hz for a 20 Hz loop) |
| C3 – EM harmonics | No mode coincident (±10%) with cogging or electrical harmonics across the operating speed range |
| C4 – Response | Predicted IMU-location acceleration at any crossing < sensor noise floor (e.g., < 0.05 g) |

---

## 2. Excitation Spectrum (What Actually Shakes the Arm)

| Source | Frequency | Notes |
|---|---|---|
| PWM carrier | f_sw = 20 kHz | Phase-current ripple produces Maxwell-force ripple in the stator |
| Ripple harmonic | 2·f_sw = 40 kHz | Dominant for center-aligned SVPWM |
| Sidebands | f_sw ± 2f_e, 2f_sw ± f_e | f_e = p·n/60 (p = pole pairs) |
| Cogging | N_cog = LCM(slots, poles) · n/60 | Low frequency, high amplitude in gimbal motors |
| Commutation/torque ripple | 6·f_e | |
| FOC current loop | Typically 10–20 kHz | Often synchronous with f_sw |
| Position/IMU loop | 0.5–8 kHz sample rate | Aliasing risk if a mode lies above Nyquist |

**Key insight:** At 20 kHz, the forcing amplitude is small, but aluminum damping is very low (ζ ≈ 0.001–0.005, so Q ≈ 100–500). A coincident high-order mode can therefore still produce audible whine and IMU noise. Cogging and control interaction usually dominate the first mode. The carrier mainly threatens high-order modes.

---

## 3. Analytical Model

### 3.1 Material (6061-T6 baseline)
- E = 68.9 GPa, ρ = 2700 kg/m³, G = 26 GPa, ν = 0.33

### 3.2 Geometry (baseline, replace with yours)
- Cantilever length L = [80 mm], width b = [20 mm], thickness h = [5 mm]
- Tip mass (motor + payload interface) m_tip = [0.15 kg]

### 3.3 Section properties
- A = b·h = 1.0×10⁻⁴ m², so ρA = 0.27 kg/m and m_beam = 21.6 g
- Weak axis: I_w = b·h³/12 = 2.08×10⁻¹⁰ m⁴, so EI_w = 14.35 N·m²
- Strong axis: I_s = h·b³/12 = 3.33×10⁻⁹ m⁴, so EI_s = 230 N·m²

### 3.4 Bare-beam bending modes (Euler–Bernoulli)

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}, \qquad \beta_n L = 1.875,\ 4.694,\ 7.855,\ 10.996
$$

### 3.5 First mode with tip mass (Rayleigh)

$$
k = \frac{3EI}{L^3}, \qquad m_{eff} = m_{tip} + 0.236\,m_{beam}, \qquad f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}
$$

### 3.6 Torsion (arm twisting about its own axis)

$$
f_t = \frac{1}{2\pi}\sqrt{\frac{GJ/L}{J_{tip}}}, \qquad J \approx \beta\, b\, h^3 \ \ (\beta \approx 0.29 \text{ for } b/h = 4)
$$

Here J_tip is the motor's rotary inertia about the arm axis.

---

## 4. Baseline Results

| Mode | Model | f (Hz) | vs 20 kHz | Status |
|---|---|---|---|---|
| 1st bending, weak axis + tip mass | Rayleigh | **~117** | — | C2 marginal (needs ≥100 Hz) |
| 1st bending, strong axis + tip mass | ×√16 | ~469 | — | OK |
| Bare beam mode 1 | E-B | 637 | — | Local |
| Bare beam mode 2 | E-B | 3,990 | — | Check vs IMU Nyquist |
| Bare beam mode 3 | E-B | 11,180 | — | OK |
| **Bare beam mode 4** | E-B | **~21,900** | **+9.5%** | **⚠ Fails C1 (±15%)** |
| Mode 5 | E-B | ~32,700 | — | OK |
| Mode 6 | E-B | ~45,700 | +14% vs 40 kHz | ⚠ Marginal |

The tip mass lowers higher modes only modestly, because the tip acts closer to a pinned constraint for high n. Mode 4 will likely fall to roughly 19–21 kHz, which is **directly on the carrier**. **FEA confirmation is mandatory.**

---

## 5. Response Estimate at a Crossing

$$
T(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad r = f_{exc}/f_n
$$

| ζ | Condition | T at r = 1 | T at r = 0.91 (±9.5%) |
|---|---|---|---|
| 0.002 | Monolithic CNC, no joints | 250 | ~5.8 |
| 0.02 | Bolted joint / damping pad | 25 | ~5.7 |

Away from exact coincidence, damping barely matters. **Frequency separation is the main lever, and damping is the backup.**

---

## 6. Mitigation Options (Ranked)

1. **Shift the carrier** to 24–32 kHz. This is still inaudible and costs some switching loss. Alternatively, use randomized/spread-spectrum PWM (±2 kHz dither) to flatten the peak.
2. **Shift the mode:** f ∝ h/L². Changing h from 5 to 6 mm raises all bending modes by about 20%, which moves mode 4 to roughly 26 kHz and f₁ to about 154 Hz.
3. **Section change:** Use a U- or L-channel or ribbed pocket instead of a flat bar. This greatly increases I at little mass cost and fixes C2.
4. **Damping:** Add constrained-layer viscoelastic tape (ζ → 0.02–0.05) or a b