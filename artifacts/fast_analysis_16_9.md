# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm vs. 20 kHz PWM Carrier

*I don't have the history of your earlier cycles, so this is a self-contained formulation. The numbers are a representative worked example. Substitute your actual arm geometry, payload, and drive parameters.*

---

## 1. Problem Definition

The roll-axis BLDC/PMSM is driven by a 20 kHz PWM inverter. The carrier injects high-frequency current ripple, which becomes torque/force ripple at the stator–rotor interface. That ripple is transmitted into the roll arm.

**Objective:** Verify that no structural mode of the arm is coincident with any significant excitation frequency. A secondary goal is to keep the fundamental mode well above the servo bandwidth.

**Excitation spectrum to check:**

| Source | Frequency |
|---|---|
| PWM carrier | f_sw = 20 kHz |
| Carrier harmonics | 2f_sw = 40 kHz, 3f_sw … (40 kHz dominant for center-aligned SVPWM ripple) |
| Sidebands | f_sw ± k·f_e, where f_e = (p/2)·(RPM/60) |
| Low-frequency sources | cogging (N_slots·LCM-related × f_mech), control-loop bandwidth f_bw |

---

## 2. Material Properties (6061-T6, typical CNC stock)

| Property | Value |
|---|---|
| E | 68.9 GPa |
| G | 26 GPa |
| ρ | 2700 kg/m³ |
| ν | 0.33 |
| Intrinsic damping ζ | ≈ 0.001–0.003 |
| Assembled damping ζ (bolted joints) | ≈ 0.01–0.02 |

**Note on 7075-T6:** E is roughly equal (71.7 GPa) and ρ = 2810 kg/m³. The specific stiffness E/ρ is nearly identical, so an alloy swap does not move resonances meaningfully. Geometry is the lever.

---

## 3. Analytical Models

### 3.1 Bending: Euler–Bernoulli Cantilever (motor end fixed)

Bare-beam modes:

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}
$$

with β_nL = 1.875, 4.694, 7.855, 10.996, 14.137, and ≈ (2n−1)π/2 for higher modes.

Fundamental with tip payload (Rayleigh approximation):

$$
k_b = \frac{3EI}{L^3}, \qquad f_1 \approx \frac{1}{2\pi}\sqrt{\frac{k_b}{m_{tip} + 0.2357\, m_{beam}}}
$$

### 3.2 Torsion about the Arm Axis

$$
k_t = \frac{G J_t}{L}, \qquad f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_{payload} + \tfrac{1}{3}J_{arm}}}
$$

For a rectangular b × h section (b ≥ h):

$$
J_t \approx b h^3\left[\tfrac13 - 0.21\tfrac{h}{b}\left(1-\tfrac{h^4}{12b^4}\right)\right]
$$

### 3.3 Forced Response (per mode, SDOF)

$$
\frac{|X|}{F/k} = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad r = \frac{f_{exc}}{f_n}, \qquad Q = \frac{1}{2\zeta}
$$

At ζ = 0.005, Q = 100. A coincident mode amplifies ripple by about 100×.

### 3.4 PWM Excitation Amplitude

$$
\Delta I_{pp} \approx \frac{V_{bus}\, D(1-D)}{L_{ph}\, f_{sw}}, \qquad \Delta T \approx K_t \cdot \Delta I_{pp}/2, \qquad F_{arm} \approx \Delta T / r_{mount}
$$

---

## 4. Worked Example

**Assumed geometry and drive:**
- Arm: L = 80 mm, b = 20 mm, h = 4 mm, 6061-T6
- Payload at the tip: 150 g

**Section properties:**
- I = bh³/12 = 1.067×10⁻¹⁰ m⁴
- EI = 7.35 N·m²
- ρA = 0.216 kg/m
- m_beam = 17.3 g

**Fundamental with payload:**
- k_b = 3EI/L³ = 43.1 kN/m
- m_eff = 0.150 + 0.2357(0.0173) = 0.154 kg
- **f₁ ≈ 84 Hz**

**Bare-arm modal ladder** (payload-free sub-structure modes, which govern the high-frequency behavior):

f₁,bare = 0.5596 × √(EI/(ρA·L⁴)) ≈ 510 Hz, and fₙ = 510 × (βₙL / 1.875)²:

| Mode | βₙL | fₙ | Ratio to 20 kHz |
|---|---|---|---|
| 1 | 1.875 | 0.51 kHz | — |
| 2 | 4.694 | 3.2 kHz | — |
| 3 | 7.855 | 8.9 kHz | 0.45 |
| 4 | 10.996 | **17.5 kHz** | **0.88 ⚠** |
| 5 | 14.137 | 29.0 kHz | 1.45 |
| 6 | 17.279 | 43.3 kHz | **2f_sw: 0.92 ⚠** |

**Finding:** Mode 4 sits within about 12% of the carrier, and Mode 6 sits within about 8% of 2f_sw. Both fail a ±20% separation criterion. Expect audible or near-ultrasonic whine and possible IMU contamination if the IMU is arm-mounted.

---

## 5. Acceptance Criteria

1. **Carrier separation:** |f_n − f_exc| / f_exc ≥ 0.20 for f_exc ∈ {f_sw, 2f_sw, f_sw ± 2f_e,max}.
2. **Servo separation:** f₁ (with payload) ≥ 3–5 × f_bw. For example, an 84 Hz fundamental supports f_bw ≲ 17–28 Hz.
3. **Response limit:** the forced tip acceleration at the IMU must stay below the IMU's anti-alias and vibration-rectification threshold.
4. **Mounting:** the IMU should not sit at an antinode of any mode within 15–45 kHz.

---

## 6. Mitigation Options (ranked)

1. **Move the carrier.** Shift f_sw to about 24–25 kHz, which puts 2f_sw near 48–50 kHz. This is often the cheapest fix. Alternatively, use spread-spectrum or randomized PWM (±1–2 kHz) to smear the peak energy.
2. **Retune the geometry.** Changing h scales all fₙ linearly (f ∝ h). For example, h = 4 → 4.5 mm raises Mode 4 to about 19.7 kHz, which is worse. Going to h ≈ 5 mm moves Mode 4 to 21.9 kHz, which is also too close. Either step further, or add