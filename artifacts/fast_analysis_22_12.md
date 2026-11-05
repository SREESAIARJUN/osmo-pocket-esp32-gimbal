# Mechanical Resonance and Natural Frequency Analysis: CNC Aluminum Roll Arm Under 20 kHz PWM Excitation (Cycle 22)

> **Note:** I don't have your Cycle 22 drawings or measured data. The numbers below use a stated **reference geometry** so the method can be checked end to end. Replace the inputs in Section 1 with your actual values. The equations and acceptance criteria carry over unchanged.

---

## 1. Model Definition and Assumptions

| Parameter | Symbol | Reference value |
|---|---|---|
| Material | – | 6061‑T6 Al |
| Young's modulus | E | 68.9 GPa |
| Density | ρ | 2700 kg/m³ |
| Free length (clamp to motor axis) | L | 80 mm |
| Section (width × thickness) | b × h | 20 × 4 mm |
| Tip mass (motor + payload share) | M_t | 150 g |
| Structural damping ratio (bare Al) | ζ | 0.002–0.005 |
| PWM carrier | f_sw | 20 kHz |
| Motor pole pairs / max speed | p / n | 7 / 300 rpm (gimbal) |

**Boundary conditions:** fixed-free cantilever with tip mass, i.e. a rigid clamp at the yaw/pitch joint.

This is the stiff bound. The real joint (bearing, screw preload) is compliant, which lowers f_n by roughly 10–30%. Model it as a rotational spring k_θ if you have the data.

---

## 2. Governing Equations

### 2.1 Euler–Bernoulli Bending

```
EI ∂⁴w/∂x⁴ + ρA ∂²w/∂t² = q(x,t)
```

The section properties for each bending direction are:

```
I_flap = b·h³/12        (bending through thickness, weak axis)
I_edge = h·b³/12        (in-plane, strong axis)
```

### 2.2 Bare Cantilever Modes (Distributed Mass Only)

```
f_i = (β_iL)² / (2π) · √( EI / (ρA·L⁴) )
```

The mode constants are (β_iL)² = 3.516, 22.03, 61.70, 120.9, 199.9, and so on. For higher modes, β_iL ≈ (2i−1)π/2.

### 2.3 Fundamental with Tip Mass (Rayleigh)

```
k = 3EI/L³
m_eff = M_t + 0.2357·ρAL
f_1 = (1/2π)·√(k / m_eff)
```

### 2.4 Torsional Mode (Roll Arm Twisting About Its Own Axis)

```
J ≈ β·b·h³            (β ≈ 0.282 for b/h = 5)
k_t = G·J / L         (G = 26 GPa)
f_t = (1/2π)·√(k_t / I_tip)
```

Here I_tip is the polar mass moment of the motor/payload about the arm's long axis.

---

## 3. Worked Results (Reference Geometry)

**Section properties**
- I_flap = 106.7 mm⁴, so EI_flap = 7.35 N·m²
- I_edge = 2667 mm⁴, so EI_edge = 183.8 N·m²
- ρA = 0.216 kg/m and the arm mass is 17.3 g

**Fundamental with tip mass**
- k_flap = 3(7.35)/(0.08)³ = 43.1 kN/m
- m_eff = 0.150 + 0.2357 × 0.0173 = 0.154 kg
- **f₁,flap ≈ 84 Hz**
- **f₁,edge ≈ 84 × √25 ≈ 420 Hz**

**Bare-arm higher flap modes**

The scale factor is √(EI/ρAL⁴) = 911.5 s⁻¹, giving f_i = 145.1 × (β_iL)².

| Mode | (β_iL)² | f_i (Hz) | Ratio to 20 kHz |
|---|---|---|---|
| 1 | 3.516 | 510* | 0.026 |
| 2 | 22.03 | 3,197 | 0.16 |
| 3 | 61.70 | 8,953 | 0.45 |
| 4 | 120.9 | **17,540** | **0.88** ⚠ |
| 5 | 199.9 | **29,000** | **1.45** |

*The tip mass pulls mode 1 down to about 84 Hz. It shifts higher modes only slightly, because the tip node acts as a near-pinned point. Confirm this with FEA.

**Interpretation:** the carrier sits between modes 4 and 5. Mode 4 is within about 12% of 20 kHz. That is inside the exclusion band defined in Section 5, so it needs action.

---

## 4. Excitation Spectrum from the Motor Drive

The PWM produces current ripple and therefore Maxwell force/torque ripple at:

```
f_exc = m·f_sw ± k·f_e,   where f_e = p·n/60
```

With p = 7 and n = 300 rpm, f_e = 35 Hz. The sidebands are therefore tightly clustered.

| Source | Frequency |
|---|---|
| Carrier, SVPWM, center-aligned | 20 kHz ± 2f_e (≈ ±70 Hz) |
| 2nd carrier group (often dominant in phase ripple) | 40 kHz ± f_e, ± 3f_e |
| Commutation / cogging torque ripple | 6f_e, 12f_e → 210 / 420 Hz ⚠ (matches f₁,edge) |
| Control loop (position/speed) | 50–500 Hz bandwidth ⚠ (overlaps f₁) |

**IMU aliasing check.** This is critical even if the mechanical response is small:

```
f_alias = |f_exc − N·f_s|
```

For a gyro with f_s = 8 kHz, a 20 kHz vibration aliases to |20 000 − 16 000| = **4 kHz**. A 3.2 kHz gyro gives |20 000 − 19 200| = **800 Hz**, which lands in the band the controller sees.

---

## 5. Forced Response and Acceptance Criteria

### 5.1 Single-Mode Magnification

```
H(r) = 1 / √[ (1 − r²)² + (2ζr)² ],   r = f_exc / f_i
```

| Mode 4 at r = 1.14, ζ = 0.003 | H ≈ 3.3 |
|---|---|
| If a mode lands at r = 1 | H = 1/(2ζ) ≈ 167 |

### 5.2 Design Rules

1. **Exclusion band.** Each structural mode f_i must avoid:
   ```
   0.8·f_exc < f_i < 1.25·f_exc     (±20%)
   ```
   This applies to f_sw, 2f_sw, 6f_e, and 12f_e.
2. **Control separation.** f₁ ≥ 3–5 × the position-loop bandwidth, or use a notch filter at f₁.
3. **Carrier versus fundamental.** f_sw ≫ f₁ with r ≈ 240, so H ≈ 1/r², roughly 1.7×10⁻⁵. The fundamental is effectively isolated at the carrier.
4. **Acoustic.** At 20 kHz, any mode inside ±5% will radiate audible or ultrasonic whine. Check the panel or pocket modes of the CNC lightening cuts, not just the beam modes.

### 5.3 Reference Geometry Verdict

| Check | Status |
|---|---|
| Mode 4 (17.5 kHz) vs 20 kHz | ✗ inside the ±20% band |
| f