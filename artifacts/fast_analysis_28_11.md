# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm under 20 kHz PWM Excitation

> **Note on "Cycle 28":** I don't have prior cycles or your design data. The formulation below is complete and general. The numbers are a **worked example with stated assumptions**. Replace them with your actual geometry, payload, and drive parameters.

---

## 1. Problem Definition

**Objective:** Make sure no structural mode of the roll arm is excited by the motor drive's electromagnetic forcing, and that none produces audible noise or IMU/gyro corruption.

**Excitation sources:**

| Source | Frequency | Notes |
|---|---|---|
| PWM carrier | f_c = 20 kHz | Fundamental of the current ripple |
| Center-aligned PWM ripple | 2f_c = 40 kHz | Often dominant in phase-current ripple |
| Carrier sidebands | f_c ± k·f_e (k = 1, 2, …) | f_e is the electrical frequency |
| Electrical frequency | f_e = p·n/60 | p = pole pairs, n = rpm. Low for gimbals, so sidebands cluster tightly around f_c |
| Torque ripple / cogging | 6f_e, LCM(slots, poles)·n/60 | Low-frequency, high-energy |
| Control loop | f_FOC, f_pos | Typically 1–20 kHz |

The 20 kHz carrier delivers very little force. The real risks are (a) a high-order mode coinciding with f_c and producing ultrasonic or audible whine, and (b) vibration **aliasing into the IMU**.

---

## 2. Structural Model

The roll arm is modeled as a **cantilever beam** (fixed at the roll motor/yaw interface) carrying a **tip mass**: the pitch motor plus payload.

### 2.1 Example Assumptions

| Parameter | Value |
|---|---|
| Material | 6061-T6: E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³ |
| Section | Rectangular, b = 20 mm, h = 5 mm |
| Length | L = 80 mm |
| Tip mass | M = 0.15 kg |
| Payload roll inertia | J_p = 1.0×10⁻⁴ kg·m² |

### 2.2 Section Properties

- Second moment of area: I = bh³/12 = 2.083×10⁻¹⁰ m⁴
- Bending stiffness: **EI = 14.35 N·m²**
- Mass per unit length: μ = ρA = 0.27 kg/m
- Beam mass: m_b = 21.6 g

---

## 3. Natural Frequency Formulation

### 3.1 Bending: Euler–Bernoulli Cantilever (Bare Arm, Distributed Mass)

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\mu L^4}}, \qquad \beta_n L = 1.875,\ 4.694,\ 7.855,\ 10.996
$$

| Mode | β_nL | f_n (bare arm) |
|---|---|---|
| 1 | 1.875 | 637 Hz |
| 2 | 4.694 | 3,992 Hz |
| 3 | 7.855 | 11,180 Hz |
| **4** | **10.996** | **≈ 21,900 Hz ⚠** |

### 3.2 First Mode with Tip Mass (Rayleigh)

$$
k = \frac{3EI}{L^3} = 84{,}080\ \text{N/m}, \qquad m_{\text{eff}} = M + 0.2235\,m_b = 0.1548\ \text{kg}
$$

$$
f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{\text{eff}}}} \approx \mathbf{117\ Hz}
$$

The tip mass also shifts the higher modes. These should be confirmed by FEA.

### 3.3 Torsional Mode (Arm Twisting with Payload Inertia)

$$
J_t = \beta\, b h^3 \quad (\beta \approx 0.281 \text{ for } b/h = 4) = 7.03\times10^{-10}\ \text{m}^4
$$

$$
k_t = \frac{G J_t}{L} = 228\ \text{N·m/rad}, \qquad f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_p}} \approx \mathbf{240\ Hz}
$$

### 3.4 Local Plate/Web Modes (Pocketed CNC Features)

For a thin pocket floor of thickness t and span a:

$$
f_{\text{plate}} \approx \frac{\lambda^2}{2\pi a^2}\sqrt{\frac{D}{\rho t}}, \qquad D = \frac{E t^3}{12(1-\nu^2)}
$$

Thin floors of 1–1.5 mm frequently land in the **10–30 kHz band**. These local modes are the most likely 20 kHz offenders and must be checked by FEA.

---

## 4. Resonance Proximity & Response

### 4.1 Frequency Separation Criterion

$$
\text{SM} = \frac{|f_n - f_{\text{exc}}|}{f_{\text{exc}}} \geq 15\% \quad (\text{minimum } 10\%)
$$

Apply this to every f_exc ∈ {f_c, 2f_c, f_c ± k·f_e}.

For the example arm, mode 4 gives SM = |21.9 − 20|/20 = **9.5% → FAIL**.

### 4.2 Transmissibility (SDOF Approximation)

$$
T(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad r = \frac{f_{\text{exc}}}{f_n}, \qquad Q = \frac{1}{2\zeta}
$$

Typical damping:
- Monolithic aluminum: ζ ≈ 0.001–0.005
- Bolted joints: ζ ≈ 0.01–0.02

For the example (r = 0.913, ζ = 0.005): **T ≈ 6×**. If the frequencies coincide exactly: T = Q ≈ 100×.

### 4.3 Forcing Amplitude Estimate

$$
F_{\text{ripple}} \approx \frac{K_t\, \Delta i_{pp}}{r_{\text{arm}}}, \qquad \Delta i_{pp} \approx \frac{V_{dc}}{4 L_s f_c} \quad (\text{worst case at 50\% duty})
$$

The resulting displacement is x ≈ T·F/k_modal. It is typically nanometers to micrometers. That is structurally negligible but can matter acoustically and for IMU readings.