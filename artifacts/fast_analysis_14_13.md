# Cycle 14: Mechanical Resonance & Natural Frequency Analysis of the CNC Aluminum Roll Arm Under a 20 kHz PWM Carrier

I don't have the earlier cycles' parameters, so this uses **representative placeholder values**: a gimbal-style roll arm and a BLDC/PMSM driven by FOC with a 20 kHz carrier. Substitute your Cycle 14 geometry and masses into the equations. The structure of the analysis stays the same.

---

## 1. Objective & Acceptance Criteria

Show that no structural mode of the roll arm is excited by:
1. The PWM carrier and its harmonics/sidebands, which cause acoustic whine, fretting, and sensor noise.
2. Low-frequency drive excitations: cogging, electrical fundamental, and control loop rate.
3. The closed-loop control bandwidth, where structural coupling causes jitter or instability.

**Design rules**

| Criterion | Requirement |
|---|---|
| First mode vs. position/rate loop bandwidth f_BW | f_n1 ≥ 3–5 × f_BW |
| Any mode vs. carrier lines (f_sw, 2f_sw ± k·f_e) | Separation ≥ ±15 % |
| Any mode vs. cogging / torque-ripple lines | Separation ≥ ±20 %, or show amplitude is acceptable |
| Dynamic amplification at any excitation line | Transmissibility T < 2 (non-resonant) |

---

## 2. Model Definition

**Idealization:** a cantilever beam clamped at the motor/base interface, carrying a lumped tip mass (roll motor + payload) and a tip rotary inertia.

**Material (6061-T6):**
- E = 68.9 GPa
- G = 26 GPa
- ρ = 2700 kg/m³
- Intrinsic damping ζ ≈ 0.001–0.005. Bolted joints raise this to ζ ≈ 0.01–0.03.

**Placeholder geometry:**
- Length L = 60 mm
- Cross-section b × h = 20 × 4 mm (rectangular)
- Tip mass M = 0.15 kg
- Payload inertia about the arm axis J_p = 1.5×10⁻⁴ kg·m²

**Derived section properties:**
- A = 80 mm², so mass per length ρA = 0.216 kg/m and beam mass m_b = 12.96 g
- I = bh³/12 = 106.7 mm⁴ = 1.067×10⁻¹⁰ m⁴
- EI = 7.35 N·m²

---

## 3. Bending Modes

### 3.1 First mode with tip mass (Rayleigh, dominant for design)

$$
k = \frac{3EI}{L^3} = \frac{3(7.35)}{(0.06)^3} \approx 1.02\times10^5\ \text{N/m}
$$

$$
m_{eff} = M + 0.236\,m_b = 0.15 + 0.0031 = 0.153\ \text{kg}
$$

$$
f_{n1} = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}} = \frac{1}{2\pi}\sqrt{\frac{1.02\times10^5}{0.153}} \approx \mathbf{130\ Hz}
$$

Repeat this for the weak and strong bending axes separately, since I differs about each axis.

### 3.2 Higher modes of the beam (Euler–Bernoulli)

These modes are the ones relevant to the 20 kHz carrier.

$$
f_n = \frac{(\beta_n L)^2}{2\pi L^2}\sqrt{\frac{EI}{\rho A}}, \qquad \beta_n L = 1.875,\ 4.694,\ 7.855,\ 10.996,\ \approx \tfrac{(2n-1)\pi}{2}
$$

With $\sqrt{EI/\rho A} = 5.83$ m²/s:

| Mode | β_nL | f_n (bare beam) | Note |
|---|---|---|---|
| 1 | 1.875 | 907 Hz | Tip mass drops this to ~130 Hz |
| 2 | 4.694 | 5.68 kHz | |
| 3 | 7.855 | 15.9 kHz | Within ~20 % of the carrier |
| 4 | 10.996 | 31.2 kHz | |

The tip mass lowers these values further (it adds a pinned-like boundary condition). An FEA modal run is needed to place modes 3–4 to within ±10 %.

**Finding:** the 20 kHz carrier sits between bending modes 3 and 4, close enough that tolerances, fillets, pockets, and bolt preload can shift a mode onto it.

---

## 4. Torsional Mode (Roll Arm Twist)

Saint-Venant torsion constant for a rectangle with b/h = 5 (β ≈ 0.291):

$$
J_t = \beta b h^3 = 3.73\times10^{-10}\ \text{m}^4,\qquad k_t = \frac{GJ_t}{L} = \frac{26\times10^9 (3.73\times10^{-10})}{0.06} \approx 161\ \text{N·m/rad}
$$

$$
f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_p}} = \frac{1}{2\pi}\sqrt{\frac{161}{1.5\times10^{-4}}} \approx \mathbf{165\ Hz}
$$

Open sections are torsionally weak. If f_t is limiting, a closed or boxed section increases J_t by about 10×.

---

## 5. Excitation Spectrum from the Drive

| Source | Frequency | Typical value |
|---|---|---|
| PWM carrier (line voltage, SVPWM) | f_sw ± 2f_e, 2f_sw ± f_e, 3f_sw … | 20, 40, 60 kHz clusters |
| Phase current ripple → torque ripple | Dominant at 2f_sw (center-aligned) | ~40 kHz |
| Electrical fundamental | f_e = p · n/60 | Gimbal: < 50 Hz |
| Cogging | f_cog = LCM(slots, poles) · n/60 | e.g. 12N14P → 84 × f_mech |
| 6th-harmonic torque ripple | 6 f_e | Low Hz to 100s of Hz |
| Current loop / FOC update | f_ctrl (often = f_sw) | 20 kHz |
| Position loop bandwidth | f_BW | ~20–50 Hz |

Gimbal speeds are low, so f_e is small and the carrier sidebands cluster tightly around 20 kHz and 40 kHz. The relevant "danger bands" are therefore roughly **17–23 kHz and 34–46 kHz** (±15 %).

---

## 6. Response Assessment

**Single-DOF transmissibility for each mode–excitation pair:**

$