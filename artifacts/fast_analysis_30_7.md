# Mechanical Resonance and Natural Frequency Analysis: CNC Aluminum Roll Arm with a 20 kHz PWM Carrier

I don't have the "Cycle 30" design data, so the numbers below use a representative roll arm geometry. Every step is parametric, so you can substitute your actual dimensions and masses.

---

## 1. System Definition and Assumptions

| Parameter | Symbol | Value (baseline) |
|---|---|---|
| Material | – | 6061-T6 Al |
| Young's modulus | E | 68.9 GPa |
| Shear modulus | G | 26 GPa |
| Density | ρ | 2700 kg/m³ |
| Arm free length | L | 80 mm |
| Section (width × thickness) | b × h | 20 × 6 mm |
| Tip mass (motor + payload share) | M | 150 g |
| Tip rotational inertia (about arm axis) | J_tip | 1.5×10⁻⁴ kg·m² |
| Structural damping ratio | ζ | 0.002–0.005 (Q ≈ 100–250) |
| PWM carrier | f_c | 20 kHz |

**Boundary condition:** The arm is modelled as clamped at the yaw/pitch hub and free at the roll motor, i.e. a cantilever. Real bolted joints are softer than this, so expect measured f_n to be 10–25% lower.

---

## 2. Section Properties

- Area: A = b·h = 1.2×10⁻⁴ m²
- Mass per unit length: μ = ρA = 0.324 kg/m
- Arm mass: m_b = μL = 25.9 g
- Weak-axis inertia: I_w = bh³/12 = 3.6×10⁻¹⁰ m⁴, giving EI_w = 24.8 N·m²
- Strong-axis inertia: I_s = hb³/12 = 4.0×10⁻⁹ m⁴, giving EI_s = 276 N·m²
- Torsion constant: J_t ≈ β·b·h³ with β ≈ 0.27 (for b/h = 3.33), giving J_t = 1.17×10⁻⁹ m⁴ and GJ_t = 30.3 N·m²

---

## 3. Low-Frequency Modes (Loaded Arm, Control-Relevant)

### 3a. First bending mode, weak axis (Rayleigh, tip mass)

$$k_b = \frac{3EI}{L^3} = \frac{3(24.8)}{(0.08)^3} = 1.45\times10^5\ \text{N/m}$$

$$m_{eff} = M + 0.236\,m_b = 0.156\ \text{kg}$$

$$f_{n1} = \frac{1}{2\pi}\sqrt{\frac{k_b}{m_{eff}}} \approx \mathbf{154\ Hz}$$

### 3b. First bending mode, strong axis

Strong-axis stiffness is EI_s/EI_w ≈ 11.1 times higher, so the frequency scales by √11.1:

f_n ≈ 154 × √11.1 ≈ **512 Hz**

### 3c. First torsional mode (roll-axis twist)

$$K_t = \frac{GJ_t}{L} = 379\ \text{N·m/rad}, \qquad f_t = \frac{1}{2\pi}\sqrt{\frac{K_t}{J_{tip}}} \approx \mathbf{253\ Hz}$$

---

## 4. High-Frequency Modes (Bare-Arm Euler–Bernoulli Series)

These modes matter for the carrier, because tip mass has little influence on higher-order local modes.

$$f_i = \frac{(\beta_i L)^2}{2\pi}\sqrt{\frac{EI}{\mu L^4}}, \qquad \sqrt{\frac{EI_w}{\mu L^4}} = 1367\ \text{s}^{-1}$$

| Mode | β_iL | f_i (weak axis) |
|---|---|---|
| 1 | 1.875 | 765 Hz (unloaded) |
| 2 | 4.694 | 4.79 kHz |
| 3 | 7.855 | **13.4 kHz** |
| 4 | 10.996 | **26.3 kHz** |
| 5 | 14.137 | 43.5 kHz |

For higher modes, use β_iL ≈ (2i−1)π/2.

Plate and pocket modes are also likely in the 10–40 kHz band. These come from thin webs and lightening pockets and must be found by FEA.

---

## 5. Excitation Spectrum from the Drive

For center-aligned SVPWM at carrier f_c with electrical frequency f_e:

- Primary sidebands: f_c ± 2f_e and f_c ± 4f_e, clustered near **20 kHz**.
- Second group: 2f_c ± f_e, clustered near **40 kHz**.
- Current ripple at these frequencies produces Maxwell and magnetostrictive forces in the stator. These forces are transmitted into the arm through the motor mount.
- Gimbal motors run at low speed, so f_e is only a few Hz to tens of Hz. The sidebands therefore sit at approximately 20 kHz ± <0.2 kHz.

Lower-frequency exciters to check alongside the carrier:

- Current/FOC loop rate (typically 10–20 kHz, often synchronous with PWM)
- IMU/gyro sample rate and attitude loop rate (1–8 kHz)
- Cogging: f_cog = N_cog · f_mech
- Aliasing of structural modes into the gyro

---

## 6. Separation and Transmissibility Criteria

The frequency ratio is r = f_exc / f_n, and the response amplification is:

$$T(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}$$

Two design rules apply:

1. **Avoidance band:** keep 0.7 < r < 1.4 empty of modes, with an additional ±10–15% allowance for machining tolerance and joint stiffness. Aluminum is lightly damped: Q ≈ 100–250, so a direct hit amplifies the response 100–250 times.
2. **Control rule:** the first structural mode should be at least 5–10 times the position/rate loop bandwidth.

### Evaluation against 20 kHz

| Mode | f_n | r = 20k/f_n | T | Status |
|---|---|---|---|---|
| Bending 3 | 13.4 kHz | 1.49 | 0.82 | Marginal: just outside the band; a 10% drop puts it at r ≈ 1.66 (safer), but a stiffer as-built arm moves it closer |
| Bending 4 | 26.3 kHz | 0.76 | 2.