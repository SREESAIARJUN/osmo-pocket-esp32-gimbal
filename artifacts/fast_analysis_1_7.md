# Cycle 1: Mechanical Resonance and Natural Frequency Analysis
**CNC Aluminum Roll Arm under 20 kHz Motor PWM Carrier Excitation**

---

## 1. Objective and Scope

This cycle answers two questions:

1. Do any roll-arm structural modes fall close enough to the 20 kHz PWM carrier, or its sidebands and harmonics, to be resonantly excited?
2. Is the first bending mode high enough to stay clear of the gimbal stabilization control bandwidth?

Cycle 1 uses closed-form analytical models with stated assumptions. Cycle 2 will refine these with FEA and measured data.

---

## 2. Baseline Assumptions (Cycle 1)

These values are placeholders. Replace them with your CAD and BOM values.

| Parameter | Symbol | Value |
|---|---|---|
| Material | – | Al 6061-T6 |
| Young's modulus | E | 68.9 GPa |
| Shear modulus | G | 26 GPa |
| Density | ρ | 2700 kg/m³ |
| Arm free length | L | 80 mm |
| Cross-section (width × thickness) | b × h | 20 × 6 mm |
| Tip mass (pitch motor + payload share) | m_t | 0.15 kg |
| Tip polar inertia about arm axis | J_t | 1.5×10⁻⁵ kg·m² |
| Boundary condition | – | Fixed at roll motor hub, free/loaded at tip |
| Structural damping (monolithic Al) | ζ | 0.002–0.01 (0.02 with bolted joints) |
| PWM carrier | f_c | 20 kHz (center-aligned SVPWM) |

---

## 3. Excitation Spectrum Formulation

The PWM inverter injects current ripple, and therefore torque and magnetostrictive force ripple, at:

$$
f_{exc} = m\,f_c \pm n\,f_e, \qquad f_e = \frac{p \cdot \text{RPM}}{60}
$$

For SVPWM, the dominant components are:
- **f_c ± 2f_e**
- **2f_c ± f_e**

Gimbal motors run at low speed, so f_e is typically below 10 Hz. The sidebands therefore stay tight around the carrier.

**Excitation bands of concern:**
- **Band A:** 20 kHz ± ~50 Hz (primary)
- **Band B:** 40 kHz ± ~50 Hz (secondary)

**Additional low-frequency drivers:**
- Cogging and commutation ripple at 6f_e
- The control loop (IMU rate of 1–8 kHz)

**Separation criterion (Cycle 1):**

$$
\left|\frac{f_n - f_{exc}}{f_{exc}}\right| \ge 20\% \quad \text{(target)}, \qquad \ge 10\% \quad \text{(minimum)}
$$

---

## 4. Structural Models

### 4.1 Section Properties

| Property | Formula | Value |
|---|---|---|
| Area | A = bh | 1.2×10⁻⁴ m² |
| Mass per length | μ = ρA | 0.324 kg/m |
| Beam mass | m_b = μL | 0.0259 kg |
| Weak-axis inertia (flap) | I_w = bh³/12 | 3.6×10⁻¹⁰ m⁴ → EI_w = 24.8 N·m² |
| Strong-axis inertia (in-plane) | I_s = hb³/12 | 4.0×10⁻⁹ m⁴ → EI_s = 276 N·m² |
| Torsion constant | J ≈ (bh³/3)(1 − 0.63h/b) | 1.168×10⁻⁹ m⁴ → GJ = 30.4 N·m² |
| Polar inertia | I_p = bh(b² + h²)/12 | 4.36×10⁻⁹ m⁴ |

### 4.2 Fundamental Mode with Tip Mass (Rayleigh)

$$
k = \frac{3EI}{L^3}, \qquad m_{eff} = m_t + 0.236\,m_b, \qquad f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}
$$

Weak axis:
- k = 1.453×10⁵ N/m
- m_eff = 0.156 kg
- **f₁ ≈ 154 Hz**

Strong axis:
- k scales by 11.1×
- **f₁ ≈ 512 Hz**

### 4.3 Higher Bending Modes (Euler–Bernoulli Cantilever)

$$
f_i = \frac{(\beta_i L)^2}{2\pi L^2}\sqrt{\frac{EI}{\mu}}, \qquad (\beta_i L)^2 = 3.516,\; 22.03,\; 61.70,\; 120.9
$$

The tip mass mainly depresses mode 1. Higher modes shift down only modestly, about 5–15%, so the bare-beam values below are reasonable for an initial screen.

### 4.4 Torsional Modes

With tip inertia (fundamental):

$$
f_{t1} = \frac{1}{2\pi}\sqrt{\frac{GJ/L}{J_t}}
$$

This gives k_θ = 380 N·m/rad and **f_t1 ≈ 801 Hz**.

Distributed beam (higher modes):

$$
f_{tn} = \frac{2n-1}{4L}\sqrt{\frac{GJ}{\rho I_p}}
$$

---

## 5. Results: Modal Map vs. Excitation

| Mode | f_n (Hz) | Nearest excitation | Separation | Status |
|---|---|---|---|---|
| Bending 1, weak (with tip mass) | **154** | Control loop bandwidth | – | ⚠ Check vs. loop BW |
| Bending 1, strong (