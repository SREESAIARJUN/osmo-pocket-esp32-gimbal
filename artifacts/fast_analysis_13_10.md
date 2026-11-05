# Natural Frequency and Resonance Analysis: CNC Aluminum Roll Arm with a 20 kHz PWM Carrier

I don't have your Cycle 13 drawings or mass data, so the method below is general and the numbers come from an assumed example geometry. Substitute your real dimensions into the same equations.

---

## 1. Problem Definition

**Structure:** The roll arm is modeled as a cantilever. The root is clamped at the yaw or pitch motor interface. The tip carries the roll motor and payload as a lumped mass and rotational inertia.

**Excitation sources to check:**

| Source | Frequency | Typical relevance |
|---|---|---|
| PWM carrier | f_c = 20 kHz (plus 40 kHz, 60 kHz harmonics) | Ultrasonic. Causes torque/current ripple and magnetostrictive forcing. |
| Electrical commutation | f_e = (p/2)·(RPM/60) | Low for gimbals (< 100 Hz) |
| Cogging | N_slots·p-related × f_mech | Low frequency, often a few hundred Hz |
| Control loop | Bandwidth f_bw and loop rate f_loop | Must not couple to the structure |
| IMU sampling | f_s (1–8 kHz) | Aliasing of 20 kHz → \|20k − n·f_s\| |

---

## 2. Material Properties (6061-T6)

- E = 68.9 GPa
- G = 26 GPa
- ρ = 2700 kg/m³
- ζ ≈ 0.001–0.005 for bare machined aluminum. This is very lightly damped, with Q ≈ 100–500.

---

## 3. Governing Equations

### 3.1 Distributed Euler–Bernoulli Bending (Bare Arm)

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}
$$

The mode constants are βₙL = 1.875, 4.694, 7.855, 10.996, …, approaching (2n−1)π/2.

### 3.2 Lumped Tip-Mass Model (Dominant Mode in Practice)

$$
k_b = \frac{3EI}{L^3}, \qquad m_{eff} = M_{tip} + 0.236\,m_{arm}, \qquad f_1 = \frac{1}{2\pi}\sqrt{\frac{k_b}{m_{eff}}}
$$

### 3.3 Torsional Mode (Critical for a Roll Arm)

$$
k_t = \frac{G J_t}{L}, \quad J_t \approx \beta\, b h^3 \;(\text{rectangle}), \quad f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_{payload}}}
$$

### 3.4 Forced Response (Single Degree of Freedom per Mode)

$$
r = \frac{f_{exc}}{f_n}, \qquad |H| = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}
$$

---

## 4. Worked Example (Assumed Geometry)

**Assumed inputs:**
- Arm length L = 80 mm, width b = 20 mm, thickness h = 6 mm
- Tip mass M = 150 g
- Payload inertia J = 2×10⁻⁴ kg·m²

**Section properties:**
- I = bh³/12 = 3.6×10⁻¹⁰ m⁴
- EI = 24.8 N·m²
- A = 1.2×10⁻⁴ m², giving ρA = 0.324 kg/m
- Arm mass m_arm = 25.9 g

**Results:**

| Mode | Calculation | f_n |
|---|---|---|
| Bare bending, mode 1 | 0.5596 · √(24.8 / 1.327×10⁻⁵) | **765 Hz** |
| Bare bending, mode 2 | × 6.27 | 4.8 kHz |
| Bare bending, mode 3 | × 17.55 | **13.4 kHz** |
| Bare bending, mode 4 | × 34.39 | **26.3 kHz** |
| Loaded bending, mode 1 | k_b = 1.45×10⁵ N/m, m_eff = 0.156 kg | **≈154 Hz** |
| Loaded torsion, mode 1 | J_t ≈ 0.28·b·h³ = 1.21×10⁻⁹ m⁴, k_t = 393 N·m/rad | **≈223 Hz** |

**Observation:** The 20 kHz carrier falls between bare bending modes 3 and 4.
- r = 20/13.4 = 1.49 against mode 3
- r = 20/26.3 = 0.76 against mode 4

Both ratios are inside a typical ±30–40% exclusion band. Local features (pockets, thin flanges, fastener tabs) can also create plate modes anywhere in the 10–30 kHz range.

---

## 5. Design Criteria

1. **Carrier separation.** Every mode within the 10–60 kHz range should satisfy f_n < 0.7·f_c or f_n > 1.4·f_c, for f_c and its 2f_c and 3f_c harmonics.
2. **Control separation.** The first structural mode (bending or torsion) should satisfy f₁ ≥ 3–5 × f_bw. A notch filter is needed if it does not.
3. **Aliasing.** Check that |f_c − n·f_s| does not land inside the control bandwidth or on the first structural mode.
4. **Acoustic.** Modes near 20 kHz radiate audible whine through subharmonics and fastener rattle.

---

## 6. Mitigation Options

- **Stiffen the arm.**
  - Bending frequency scales as f ∝ h·√(E/ρ)/L². Thickness and length matter most.
  - A closed or box section raises J_t dramatically. This is the best fix for the torsional mode.
- **Shift the carrier.** Moving to 24–32 kHz may clear the exclusion band, at the cost of more switching loss in the driver.
- **Add damping.**
  - Constrained-layer damping tape or a viscoelastic washer at the root can raise ζ to about 0.02–0.05.
  - Higher damping cuts peak |H| roughly tenfold.
- **Firmware.** Use a notch filter or biquad at f₁ and an anti-alias low-pass on the IMU.

---

## 7. Verification Plan

1. **FEA modal analysis** of the actual CAD model up to 60 kHz. Use the real clamp boundary conditions and the motor modeled as a lumped mass. Record mode shapes and effective modal masses.
2. **Tap test.** Use an impact hammer and accelerometer, or a microphone FFT for modes above about 5 kHz.
3. **Operational test.**
   - Sweep the PWM carrier from 16 to 32 kHz.
   - Log IMU gyro noise PSD and acoustic SPL, then identify peaks.
4. **Compare** measured and predicted frequencies. Tune the boundary-condition stiffness in the FEA until predictions match within 5–10%.

If you share the Cycle 13 dimensions, masses, IMU rate and loop bandwidth, I can rerun the numbers for your actual part.