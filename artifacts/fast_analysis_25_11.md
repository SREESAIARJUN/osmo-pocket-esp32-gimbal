# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm under a 20 kHz PWM Carrier

I don't have your "Cycle 25" data (geometry, payload, motor specs, prior iteration results). This formulation uses representative 3-axis gimbal roll arm values, marked **[assumed]**. Substitute your numbers into the same equations.

---

## 1. System Definition

| Parameter | Symbol | Value |
|---|---|---|
| Material | – | 6061-T6 aluminum |
| Young's modulus | E | 68.9 GPa |
| Shear modulus | G | 26 GPa |
| Density | ρ | 2700 kg/m³ |
| Arm free length | L | 80 mm **[assumed]** |
| Section (rectangular) | b × h | 20 × 6 mm **[assumed]** |
| Tip mass (motor + payload share) | M | 0.15 kg **[assumed]** |
| Tip rotational inertia about arm axis | J_tip | 1.5×10⁻⁴ kg·m² **[assumed]** |
| Structural damping ratio (bolted Al) | ζ | 0.002–0.01 |
| PWM carrier | f_sw | 20 kHz |

**Model:** Euler–Bernoulli cantilever, fixed at the motor/yaw interface, with a lumped tip mass at the pitch/roll motor.

---

## 2. Section Properties

- Area: A = b·h = 120 mm² = 1.2×10⁻⁴ m²
- Mass per length: m′ = ρA = 0.324 kg/m. The beam mass is m_b = 0.0259 kg.
- Bending inertia (weak axis): I = bh³/12 = 360 mm⁴ = 3.6×10⁻¹⁰ m⁴
- Bending stiffness: EI = 24.8 N·m²
- Torsion constant: J_t ≈ β·b·h³, with β ≈ 0.29 for b/h = 3.33. This gives J_t ≈ 1.25×10⁻⁹ m⁴, so GJ_t ≈ 32.6 N·m².

---

## 3. Natural Frequencies

### 3.1 First bending mode with tip mass (dominant mode)

Static tip stiffness:

$$k = \frac{3EI}{L^3} = \frac{3(24.8)}{(0.08)^3} \approx 1.45\times10^5\ \text{N/m}$$

Effective mass, using the Rayleigh correction:

$$m_{eff} = M + 0.2235\,m_b = 0.1558\ \text{kg}$$

Natural frequency:

$$f_{n1} = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}} \approx \mathbf{154\ Hz}$$

### 3.2 Torsional mode (roll about arm axis)

Torsional stiffness:

$$k_\theta = \frac{GJ_t}{L} \approx 407\ \text{N·m/rad}$$

Natural frequency:

$$f_{\theta} = \frac{1}{2\pi}\sqrt{\frac{k_\theta}{J_{tip}}} \approx \mathbf{262\ Hz}$$

### 3.3 Bare-arm modal series (governs the high-frequency band near 20 kHz)

$$f_i = \frac{\lambda_i^2}{2\pi}\sqrt{\frac{EI}{m' L^4}}, \quad \lambda_i = 1.875,\ 4.694,\ 7.855,\ 10.996,\ \dots$$

| Mode | λᵢ² | fᵢ (bare arm) |
|---|---|---|
| 1 | 3.516 | 765 Hz |
| 2 | 22.03 | 4.79 kHz |
| 3 | 61.70 | 13.4 kHz |
| 4 | 120.9 | 26.3 kHz |
| 5 | 199.9 | ≈ 43.5 kHz |

Two cautions apply to this table:
- The tip mass and bolted-joint compliance lower these values.
- Plate modes, flange modes, and pocket-wall modes of a CNC-pocketed arm also cluster in the 10–40 kHz range.

**FEA modal analysis is therefore mandatory for modes above 5 kHz.**

---

## 4. Excitation Spectrum from the Motor Drive

| Source | Frequency | Notes |
|---|---|---|
| PWM carrier | f_sw = 20 kHz | Current-ripple torque |
| Sidebands | 20 kHz ± k·f_e | f_e = p·n/60. This is small at gimbal speeds, so the sidebands stay near 20 kHz. |
| 2nd harmonic | 40 kHz | Strongest with center-aligned SVPWM |
| Cogging / commutation | N_slots·p·n/60 | Low frequency, interacts with f_n1 |
| Control loop | f_bw (≈ 20–80 Hz typical) | Interacts with f_n1 and f_θ |

**Ripple torque amplitude at the carrier:**

$$\Delta i \approx \frac{V_{dc}\,D(1-D)}{L_{ph}\,f_{sw}}, \qquad \Delta T \approx K_t\,\Delta i$$

**Forced response near mode i:**

$$|X(f)| = \frac{F_0/k_i}{\sqrt{\left(1-r^2\right)^2 + (2\zeta r)^2}}, \quad r = f/f_i, \quad Q = \frac{1}{2\zeta}\approx 50\text{–}250$$

---

## 5. Assessment

1. **Structural integrity at 20 kHz:** low risk. The ripple force is mN-scale and the modal mass participation of high-order modes is small, so stress and fatigue are negligible.

2. **Acoustic / IMU risk:** real. If any mode lies within ±10–15% of 20 kHz (17–23 kHz) or of 40 kHz, the high Q can produce audible whine at the low edge and IMU (MEMS gyro/accelerometer) aliasing. Mode 3 at 13.4 kHz and mode 4 at 26.3 kHz bracket the carrier, so pocket and plate modes must be checked in FEA.

3. **Control stability:** the dominant constraint. The first bending mode (154 Hz) and torsional mode (262 Hz) must satisfy:

$$f_{n1} \geq (3\text{–}5)\cdot f_{bw} \;\Rightarrow\; f_{bw} \lesssim 30\text{–}50\ \text{Hz}$$

Otherwise, add a notch filter at f_n1 in the gyro or velocity loop.

---

## 6. Design Criteria

| Criterion | Requirement |
|---|---|
| Separation margin | No mode within ±15% of f_sw, 2f_sw, and their sidebands |
| Loop separation | f_n1 ≥ 3–5 × f_bw |
| IMU anti-aliasing | Gyro LPF ≪ f_sw. Sample rate placed so that f_sw does not alias into the loop band. |
| Stiffening levers | f ∝ h^{3/2}. Increasing h from 6 to 8 mm raises f_n1 by ≈ 1.5× (≈ 230 Hz). Ribs beat solid thickness. Avoid thin pocket floors under 1.5 mm. |
| Tuning off resonance | If a mode sits near 20 kHz, move f_sw to 24–32 kHz, or change the pocket geometry |
| Damping | Constrained-layer damping tape or a viscoelastic washer at the motor mount, which raises ζ |

---

## 7. Verification Procedure

1. Run FEA modal analysis (0–50 kHz) with the bolted boundary conditions and the actual motor/payload mass.
2. Do a tap test or shaker test with an accelerometer or laser vibrometer, then compare against the predicted f_n1 and f