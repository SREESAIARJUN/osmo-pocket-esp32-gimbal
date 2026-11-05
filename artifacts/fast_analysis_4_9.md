# Mechanical Resonance and Natural Frequency Analysis: CNC Aluminum Roll Arm Under 20 kHz PWM Excitation (Cycle 4)

I don't have your Cycle 1–3 geometry or results, so this uses **stated assumptions** you can replace. The formulas are general. The numbers show the method and where the risks are.

---

## 1. Assumed System Definition

| Parameter | Symbol | Value (assumed) |
|---|---|---|
| Material | 6061-T6 Al | E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³ |
| Arm free length | L | 80 mm |
| Cross-section (rectangular) | b × h | 20 mm × 5 mm (bending about the thin axis) |
| Tip mass (motor + payload) | M | 150 g |
| Tip rotary inertia (torsion axis) | J_tip | 1.0 × 10⁻⁴ kg·m² |
| PWM carrier | f_pwm | 20 kHz |
| Boundary condition | – | Clamped at hub (cantilever) |

**Derived section properties:**
- Area: A = 1.0 × 10⁻⁴ m²
- Mass per unit length: μ = ρA = 0.27 kg/m
- Beam mass: m_b = μL = 21.6 g
- Second moment of area: I = bh³/12 = 2.083 × 10⁻¹⁰ m⁴
- Bending stiffness: EI = 14.35 N·m²

---

## 2. Governing Equations

### 2.1 Euler–Bernoulli bending

$$EI\frac{\partial^4 w}{\partial x^4} + \mu\frac{\partial^2 w}{\partial t^2} = q(x,t)$$

For a clamped-free beam, the characteristic equation is $1 + \cos\beta L\cosh\beta L = 0$. Its roots give the natural frequencies:

$$f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\mu L^4}}$$

with (β_nL)² = 3.516, 22.03, 61.70, 120.9, 199.9, …

### 2.2 Cantilever with tip mass (dominant mode)

Use the Rayleigh approximation with an effective beam mass:

$$k = \frac{3EI}{L^3},\qquad M_{eff} = M + 0.2357\,m_b,\qquad f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{M_{eff}}}$$

### 2.3 Torsion (roll-axis twist)

$$J_t \approx \beta\, b\, h^3 \;(\beta = 0.281 \text{ for } b/h = 4),\qquad k_t = \frac{GJ_t}{L},\qquad f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_{tip}}}$$

### 2.4 Forced response (single mode, base or torque excitation)

$$|H(r)| = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}},\qquad r = \frac{f_{exc}}{f_n},\qquad Q = \frac{1}{2\zeta}$$

Machined monolithic aluminum is lightly damped: ζ ≈ 0.001–0.005, so Q ≈ 100–500.

---

## 3. Results

### 3.1 Bare-beam bending modes (local arm modes)

$$\sqrt{EI/(\mu L^4)} = 1139\ \text{s}^{-1} \;\Rightarrow\; f_1^{bare} = 0.5596 \times 1139 \approx 637\ \text{Hz}$$

| Mode | (βL)² | f_n | Proximity to 20 kHz |
|---|---|---|---|
| 1 | 3.516 | 637 Hz | — |
| 2 | 22.03 | 3.99 kHz | — |
| 3 | 61.70 | 11.2 kHz | — |
| **4** | **120.9** | **≈ 21.9 kHz** | **+9.5% → inside the ±15% danger band** |
| 5 | 199.9 | 36.2 kHz | Near 2·f_pwm (40 kHz), −9.5% |

With a tip mass, the higher modes shift slightly, but the local high-order modes stay in this region. Verify them with FEA.

### 3.2 System fundamental (with payload)

- k = 3(14.35)/(0.08)³ = 84,080 N/m
- M_eff = 0.150 + 0.2357(0.0216) = 0.1551 kg
- **f₁ ≈ 117 Hz** (governs the control loop)

### 3.3 Torsional mode

- J_t = 7.03 × 10⁻¹⁰ m⁴
- GJ_t = 18.3 N·m²
- k_t = 228 N·m/rad
- **f_t ≈ 240 Hz**

### 3.4 PWM excitation spectrum

Torque and force ripple from the inverter appears at:

$$f_{exc} = m\,f_{pwm} \pm k\,f_e,\qquad f_e = \frac{p\cdot \text{RPM}}{60}$$

- Center-aligned SVPWM puts dominant phase-current ripple at 2f_pwm (40 kHz). Some ripple remains at f_pwm (20 kHz) through deadtime and asymmetry.
- Gimbal motors run slowly, so f_e is only a few hertz to a few hundred hertz. The sidebands are therefore narrow: about 20 kHz ± 0.5 kHz.

### 3.5 Response assessment

| Mode | r = 20k/f_n | \|H\| | Verdict |
|---|---|---|---|
| f₁ = 117 Hz | 171 | 3.4 × 10⁻⁵ | Negligible: strong mass-line isolation |
| f_t = 240 Hz | 83 | 1.4 × 10⁻⁴ | Negligible |
| Mode 4 ≈ 21.9 kHz | 0.913 | ≈ 6 (ζ = 0.003) | **Risk: audible whine, fretting, IMU aliasing** |
| Mode 5 ≈ 36 kHz vs. 40 kHz | 1.10 | ≈ 4.7 | Moderate risk |

**Key insight:** 20 kHz PWM does **not** threaten the structural fundamental. The real hazards are:

1. **High-order local and plate modes** coinciding with f_pwm or 2f_pwm. These cause acoustic noise, fastener loosening, and accelerometer saturation.
2. **IMU aliasing.** If the gyro or accelerometer samples at f_s, a 20–22 kHz vibration folds down to |f_vib − n·f_s|. This can inject false low-frequency signals into the stabilization loop.

---

## 4. Design Criteria for Cycle 4

1. **Separation margin:** |f_n − m·f_pwm ± k·f_e| / f_exc ≥ 15–20% for m = 1, 2.
2. **Control bandwidth:** f_BW ≤ f₁/3 to f₁/5, i.e. about 25–40 Hz for f₁ = 117 Hz. Add a notch filter at f₁ (and f_t if it is observable).
3. **IMU:** Enable the sensor's anti-alias LPF so it is well below f_s/2. Mount the IMU on the st