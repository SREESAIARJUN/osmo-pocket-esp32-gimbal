# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm under 20 kHz PWM Carrier

I don't have the geometry or results from earlier design cycles, so this analysis uses stated assumptions. Replace the values marked **[A]** with your Cycle 20 CAD numbers.

---

## 1. Problem Definition

**Structure:** The roll arm is modeled as a cantilever. It is clamped at the yaw/pitch interface and carries a lumped tip mass (roll motor + payload).

**Excitation sources to check:**

| Source | Frequency |
|---|---|
| PWM carrier (fundamental) | f_c = 20 kHz |
| SVPWM sidebands | f_c ± 2f_e, 2f_c ± f_e (≈ 40 kHz cluster) |
| Motor electrical frequency | f_e = p·n/60 (low for a gimbal, typically < 50 Hz) |
| Cogging / torque ripple | 6f_e, LCM(slots, poles)·n/60 |
| Control loop / IMU | loop BW ~50–200 Hz; IMU sample rate f_s |

The force path runs from current ripple to Maxwell stress / magnetostriction in the stator. This produces a high-frequency force on the motor mount, which drives the arm tip.

---

## 2. Governing Formulation

### 2.1 Bending: Euler–Bernoulli cantilever (distributed modes)

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}
$$

| n | β_nL | (β_nL)² |
|---|---|---|
| 1 | 1.8751 | 3.516 |
| 2 | 4.6941 | 22.03 |
| 3 | 7.8548 | 61.70 |
| 4 | 10.9955 | 120.9 |
| 5 | 14.1372 | 199.9 |

### 2.2 Fundamental with tip mass (Rayleigh)

$$
k = \frac{3EI}{L^3},\qquad m_{eff} = M_{tip} + \tfrac{33}{140}m_{arm},\qquad
f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}
$$

### 2.3 Torsion about the arm axis

$$
k_t = \frac{GJ}{L},\qquad f_t = \frac{1}{2\pi}\sqrt{\frac{k_t}{J_{tip} + \tfrac{1}{3}J_{arm}}},\qquad J_{rect}\approx \beta\, b\,t^3
$$

### 2.4 Local plate / web modes

These matter most at 20 kHz. For pockets and thin webs:

$$
f_{plate} = \frac{\lambda^2}{2\pi a^2}\sqrt{\frac{D}{\rho t}},\qquad D=\frac{Et^3}{12(1-\nu^2)}
$$

### 2.5 Forced response (single-mode, base or force excitation)

$$
r = \frac{f}{f_n},\qquad
T(r)=\sqrt{\frac{1+(2\zeta r)^2}{(1-r^2)^2+(2\zeta r)^2}},\qquad Q\approx\frac{1}{2\zeta}
$$

Bare machined 6061 has ζ ≈ 0.001–0.005, which gives **Q ≈ 100–500**. A coincident mode is therefore severe.

---

## 3. Worked Example (6061-T6)

**Material:** E = 68.9 GPa, G = 26 GPa, ρ = 2700 kg/m³, ν = 0.33

**Assumed geometry [A]:**
- L = 80 mm
- Section 20 × 4 mm (b × h)
- M_tip = 0.15 kg

### Section properties
- Weak axis: I = bh³/12 = 106.7 mm⁴, so EI = 7.35 N·m²
- Strong axis: I = 2667 mm⁴ (25× stiffer)
- A = 80 mm², ρA = 0.216 kg/m, m_arm = 17.3 g

### Fundamental with tip mass (weak axis)
- k = 3(7.35)/(0.08)³ = 43.1 kN/m
- m_eff = 0.150 + 0.236(0.0173) = 0.154 kg
- **f₁ ≈ 84 Hz** (weak axis)
- **f₁ ≈ 420 Hz** (strong axis)

### Distributed modes, bare arm (weak axis)
√(EI/ρAL⁴) = 911.5 s⁻¹, so f_n = 145.1·(β_nL)²:

| Mode | f_n (bare) | Margin to 20 kHz |
|---|---|---|
| 1 | 510 Hz | — |
| 2 | 3.20 kHz | — |
| 3 | 8.95 kHz | — |
| **4** | **17.5 kHz** | **−12.5 % ⚠** |
| **5** | **29.0 kHz** | +45 % |

The tip mass mainly lowers mode 1. Higher modes trend toward clamped–pinned values, so modes 4 and 5 can shift by several percent. **This must be confirmed with FEA.**

---

## 4. Acceptance Criteria

1. **Carrier separation:** no mode within **±20 %** of f_c (16–24 kHz) or 2f_c (32–48 kHz), including sideband spread.
2. **Control separation:** f₁ ≥ **3–5 ×** the closed-loop bandwidth. For example, with a 100 Hz bandwidth you need f₁ > 300–500 Hz. The 84 Hz weak-axis mode **fails** this check.
3. **IMU aliasing:** the alias frequency is f_alias = |f − N·f_s|.
   - At f_s = 8 kHz, 20 kHz aliases to **4 kHz** (the Nyquist edge) and leaks into the gyro data.
   - Choose f_s so that aliases of 20 kHz and 40 kHz land in the filter stopband, and confirm the IMU's anti-alias filter.
4. **Audible check:** 20 kHz sits at the edge of human hearing. A resonant arm can radiate audible whine through subharmonics and intermodulation.

---

## 5. Verification Plan

1. **FEA modal analysis** (fixed at the clamp, motor and payload as point masses with rotary inertia): extract modes up to 50 kHz and include local pocket and web modes.
2. **Harmonic analysis:** apply a force spectrum at the motor face from the measured phase-current rip