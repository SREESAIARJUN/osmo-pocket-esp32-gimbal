# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm under 20 kHz PWM Excitation

> **Note on "Cycle 9":** I don't have your earlier cycles' dimensions, masses, or test data. The formulation below is fully parametric. The worked numbers use **stated placeholder geometry**, so substitute your actual values. All equations and the decision criteria still apply.

---

## 1. Problem Definition

**System:** A CNC-machined aluminum roll arm carries the roll motor and payload (camera or pitch assembly). It is driven by a BLDC/PMSM motor using a 20 kHz PWM carrier.

**Goal:** Find the structural natural frequencies, then verify that none of them coincide with the excitation spectrum produced by the motor drive and control loop.

**Placeholder parameters (replace with Cycle 9 values):**

| Symbol | Meaning | Value |
|---|---|---|
| Material | 6061-T6 | E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³ |
| L | Effective cantilever length | 80 mm |
| b × h | Cross-section (width × thickness) | 20 × 4 mm |
| m_tip | Motor + payload lumped at tip | 0.150 kg |
| J_tip | Payload rotary inertia about arm axis | 1.35×10⁻⁴ kg·m² |
| ζ | Damping ratio (monolithic Al / bolted joints) | 0.002 / 0.01–0.03 |

---

## 2. Excitation Spectrum

The 20 kHz carrier is not the only frequency the arm sees. The full forcing spectrum includes:

| Source | Frequency | Notes |
|---|---|---|
| PWM carrier | f_c = 20 kHz | Magnetostriction and Maxwell forces in the stator |
| Current-ripple harmonic | 2f_c = 40 kHz | Dominant for center-aligned SVPWM |
| Sidebands | f_c ± k·f_e, 2f_c ± k·f_e | f_e = p·n_rpm/60 |
| Torque ripple | 6k·f_e | Back-EMF and current harmonics |
| Cogging | LCM(N_s, 2p)·n_rpm/60 | Slot/pole interaction |
| Control loop | f_BW, current-loop rate (often 20 kHz) | Loop gain can drive structural modes |
| IMU aliasing | \|f − k·f_s\| | 20 kHz at 8 kHz ODR folds to **4 kHz** |

Gimbal motors run slowly, so f_e is low (a few to tens of Hz). The sidebands therefore cluster tightly around 20 kHz and 40 kHz.

---

## 3. Analytical Model

### 3.1 First bending mode with tip mass (dominant mode)

Model the arm as a cantilever with tip stiffness and an equivalent mass:

$$
k = \frac{3EI}{L^3}, \qquad I = \frac{b h^3}{12}
$$

The equivalent mass uses the Rayleigh correction:

$$
m_{eq} = m_{tip} + 0.2235\, m_{beam}
$$

$$
f_{1} = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eq}}}
$$

**Numbers:**
- I = 20·4³/12 = 106.7 mm⁴ = 1.067×10⁻¹⁰ m⁴
- EI = 7.35 N·m²
- k = 3(7.35)/(0.08)³ = 43,070 N/m
- m_beam = 2700 × 80×10⁻⁶ × 0.08 = 17.3 g
- m_eq = 0.150 + 0.0039 = 0.1539 kg
- **f₁ ≈ 84 Hz** (bending about the thin axis)

### 3.2 Bare-beam higher modes (local/structural modes)

These are the modes most relevant to the kHz range. They use the Euler–Bernoulli cantilever solution:

$$
f_n = \frac{\beta_n^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}, \quad \beta_n L = 1.875,\ 4.694,\ 7.855,\ 10.996,\ 14.137,\ldots
$$

Here √(EI/ρAL⁴) = 911.5 s⁻¹.

| Mode | β_nL | f_n (Hz) | Proximity to 20 kHz |
|---|---|---|---|
| 1 | 1.875 | 510 | — |
| 2 | 4.694 | 3,196 | — |
| 3 | 7.855 | 8,950 | — |
| **4** | **10.996** | **17,540** | **−12 % ⚠** |
| 5 | 14.137 | 28,990 | +45 % |

The tip mass mainly lowers mode 1. Higher modes shift less, but still need FEA confirmation.

### 3.3 Torsional mode (roll-axis twist)

The torsion constant for a thin rectangle is:

$$
J_t \approx b h^3\left[\tfrac{1}{3} - 0.21\tfrac{h}{b}\left(1 - \tfrac{h^4}{12 b^4}\right)\right] = 3.73\times10^{-10}\ \text{m}^4
$$

$$
k_\theta = \frac{G J_t}{L} = 121\ \text{N·m/rad}, \qquad f_\theta = \frac{1}{2\pi}\sqrt{\frac{k_\theta}{J_{tip}}} \approx \mathbf{151\ Hz}
$$

### 3.4 Plate/flexural modes of webs and pockets

Thin CNC pockets behave as plates:

$$
f_{mn} = \frac{\pi}{2}\left[\left(\tfrac{m}{a}\right)^2 + \left(\tfrac{n}{b}\right)^2\right]\sqrt{\frac{D}{\rho t}}, \qquad D = \frac{E t^3}{12(1-\nu^2)}
$$

This is simply-supported; clamped edges raise f by roughly 1.8×. Thin webs of 1–2 mm commonly fall in the **10–30 kHz** band. They are the most likely 20 kHz responders.

---

## 4. Response Assessment

### 4.1 Frequency ratio and amplification

$$
r = \frac{f_{exc}}{f_n}, \