# Natural Frequency and Resonance Analysis: CNC Aluminum Roll Arm with 20 kHz PWM Excitation

I don't have the history behind "Cycle 27," so this analysis uses assumed baseline values. Replace them with your actual drawing dimensions and the method still applies.

---

## 1. Problem Definition

**Assumed system**
- **Arm:** 6061-T6 aluminum, CNC machined, modeled as a cantilever. The root is fixed at the yaw/pitch interface and the tip carries the roll motor plus payload.
- **Excitation:** a BLDC/PMSM driven by FOC/SVPWM at a carrier frequency f_c = 20 kHz.

**Baseline parameters**

| Parameter | Symbol | Value |
|---|---|---|
| Young's modulus | E | 68.9 GPa |
| Shear modulus | G | 26 GPa |
| Density | ρ | 2700 kg/m³ |
| Free length | L | 80 mm |
| Section (width × thickness) | b × h | 20 × 5 mm |
| Tip mass (motor + payload) | M | 0.150 kg |
| Payload roll inertia | J_p | 1.0×10⁻⁴ kg·m² |

**Excitation spectrum to check**

| Source | Frequency |
|---|---|
| PWM carrier | f_c = 20 kHz |
| Center-aligned SVPWM current ripple (dominant) | 2f_c = 40 kHz |
| Carrier sidebands | k·f_c ± n·f_e (f_e = electrical frequency) |
| Cogging/torque ripple | 6·f_e, 12·f_e (low-frequency) |
| Control loop / IMU | Loop bandwidth (~50–300 Hz), sample rate (1–8 kHz) |

---

## 2. Section Properties

- Area: A = b·h = 1.0×10⁻⁴ m²
- Mass per unit length: μ = ρA = 0.270 kg/m
- Beam mass: m_b = μL = 0.0216 kg
- Second moment of area (weak axis): I = bh³/12 = 2.083×10⁻¹⁰ m⁴
- Bending stiffness: EI = 14.35 N·m²
- Torsion constant: J_t ≈ bh³[1/3 − 0.21(h/b)(1 − h⁴/12b⁴)] ≈ 7.02×10⁻¹⁰ m⁴
- Torsional stiffness: GJ_t = 18.25 N·m²

---

## 3. Fundamental Bending Mode (Rayleigh, Lumped Tip Mass)

$$
k = \frac{3EI}{L^3} = \frac{3(14.35)}{(0.08)^3} = 84{,}080\ \text{N/m}
$$

$$
m_{eff} = M + 0.2357\,m_b = 0.1551\ \text{kg}
$$

$$
f_{n1} = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}} = \frac{736\ \text{rad/s}}{2\pi} \approx \mathbf{117\ Hz}
$$

**Finding:** This mode falls inside or near a typical gimbal control loop bandwidth. **This mode is the main risk, not the 20 kHz carrier.**

---

## 4. Torsional Mode (Arm Twist About Its Long Axis)

$$
k_t = \frac{GJ_t}{L} = 228\ \text{N·m/rad}, \qquad
f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_p}} \approx \mathbf{240\ Hz}
$$

---

## 5. Higher Bending Modes (Euler–Bernoulli Continuous Beam)

$$
f_i = \frac{(\beta_i L)^2}{2\pi L^2}\sqrt{\frac{EI}{\mu}} = 181.3\,(\beta_i L)^2\ \text{Hz}
$$

With a heavy tip mass (M/m_b ≈ 7), the higher modes tend toward clamped–pinned eigenvalues:

| Mode | βL (bare cantilever) | f (bare) | βL (heavy tip, clamped–pinned) | f (with tip mass) |
|---|---|---|---|---|
| 1 | 1.875 | 637 Hz | — (Rayleigh) | **117 Hz** |
| 2 | 4.694 | 4.0 kHz | 3.927 | 2.80 kHz |
| 3 | 7.855 | 11.2 kHz | 7.069 | 9.06 kHz |
| 4 | 10.996 | 21.9 kHz | 10.21 | **18.9 kHz** ⚠ |
| 5 | 14.137 | 36.2 kHz | 13.35 | 32.3 kHz |

---

## 6. Resonance Criteria

**Frequency ratio**

$$
r = \frac{f_{exc}}{f_n}, \qquad \text{Avoid band: } 0.85 < r < 1.15
$$

**Dynamic amplification**

$$
H(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}
$$

Here ζ ≈ 0.002–0.01 for monolithic machined aluminum, which gives Q ≈ 50–250.

**Evaluation against the excitation sources**

| Excitation | Nearest mode | r | Status |
|---|---|---|---|
| 20 kHz carrier | Mode 4 (18.9 kHz) | 1.06 | **Inside avoid band.** Expect audible whine and fretting at joints. Low structural fatigue risk because the PWM force amplitude is small. |
| 40 kHz (2f_c ripple) | Mode 5 (32.3 kHz) | 1.24 | Marginal OK |
| Control bandwidth (~100–200 Hz) | Mode 1 (117 Hz) | ~1 | **Critical.** Causes oscillation, jitter, and reduced stabilization gain. |
| Torque ripple 6f_e at slow pans | Modes 1 / torsion | Varies | Sweep check required |

**Design rule:** f_n1 ≥ 3–5 × the control loop bandwidth, and every mode must sit ±15% away from k·f_c (k = 1, 2).

---

## 7. Remediation Options

1. **Raise the first mode.** Since f ∝ h^{3/2} for bending, increasing thickness from 5 to 8 mm raises f_n1 to about 230 Hz. Alternatively, use an I-, U-, or closed box section, which raises I much faster than mass. The goal is f_n1 > 300–400 Hz.
2. **Reduce tip mass.** Move the motor mass toward the root, or lighten the motor housing.
3. **Shift the carrier.** Raise f_c to 24–25 kHz so that r drops out of the avoid band for mode 4. It also helps to keep 2f_c away from mode 5.
4. **Add damping.** Options include constrained-layer damping tape, a viscoelastic washer at the joint, or carbon/Al hybrid construction.
5. **Use control-side measures.** Add a notch filter at the measured f_n1 and limit loop bandwidth to f_n1/3.
6. **Address electrical sources.** Use symmetric S