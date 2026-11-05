# Natural Frequency and Resonance Analysis: CNC Aluminum Roll Arm Under 20 kHz PWM Excitation

I don't have the details from earlier design cycles, so this analysis uses representative assumptions. Substitute your actual geometry, motor mass and pole count into the formulas below.

---

## 1. Assumptions

| Parameter | Symbol | Value |
|---|---|---|
| Material | 6061-T6 Al | E = 68.9 GPa, ρ = 2700 kg/m³ |
| Effective arm length (root to motor axis) | L | 60 mm |
| Section (width × thickness, bending in thin axis) | b × h | 12 × 4 mm |
| Tip mass (motor + payload share) | M | 60 g |
| Loss factor (bare Al, bolted) | ζ | 0.005 – 0.02 |
| PWM carrier | f_c | 20 kHz (center-aligned) |

The arm is modelled as a cantilever: clamped at the yaw/pitch hub with a lumped mass at the roll motor.

---

## 2. Governing Model (Euler–Bernoulli Beam)

$$EI\frac{\partial^4 w}{\partial x^4} + \rho A\frac{\partial^2 w}{\partial t^2} = F(x,t)$$

**Section properties**

- I = bh³/12 = 64 mm⁴ = 6.4×10⁻¹¹ m⁴
- EI = 4.41 N·m²
- A = 48 mm², so μ = ρA = 0.1296 kg/m
- Beam mass m_b = 7.8 g

### 2a. Bare-arm modal series (no tip mass)

$$f_n = \frac{(\beta_n L)^2}{2\pi L^2}\sqrt{\frac{EI}{\rho A}}, \quad \beta_nL = 1.875,\ 4.694,\ 7.855,\ 10.996$$

| Mode | Ratio to f₁ | f_n |
|---|---|---|
| 1 | 1.00 | **907 Hz** |
| 2 | 6.27 | 5.69 kHz |
| 3 | 17.55 | **15.9 kHz** |
| 4 | 34.39 | 31.2 kHz |

### 2b. Loaded fundamental (Rayleigh, tip mass)

$$k = \frac{3EI}{L^3} = 61{,}250\ \text{N/m}, \qquad m_{eff} = M + 0.236\,m_b = 61.8\ \text{g}$$

$$f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}} \approx \mathbf{158\ Hz}$$

The tip mass also pulls the higher modes down, typically by 5–15%. Mode 3 therefore drifts from about 15.9 kHz to roughly 14–15 kHz.

### 2c. Torsional mode about the arm axis

$$f_t = \frac{1}{2\pi}\sqrt{\frac{GJ_t/L}{J_{tip}}}, \quad J_t \approx \beta\, b h^3 \ (\beta \approx 0.26 \text{ for } b/h = 3)$$

With G = 26 GPa and J_tip estimated from the motor/camera rotational inertia, this mode is typically 200–600 Hz. Compute it explicitly: it is often the lowest mode that couples into roll-axis control.

---

## 3. Excitation Spectrum from the Motor Drive

| Source | Frequency | Notes |
|---|---|---|
| Electrical fundamental | f_e = p·n (p = pole pairs, n = rev/s) | Gimbal speeds give < 50 Hz |
| Torque ripple / cogging | 6f_e, 12f_e (or LCM(slots, poles)·n) | Low Hz |
| PWM current ripple | f_c = 20 kHz | Largest magnitude for edge-aligned PWM |
| Maxwell / magnetostrictive force | **2f_c = 40 kHz** | Dominant for center-aligned SVPWM |
| PWM sidebands | k·f_c ± m·f_e | Clustered within ±100 Hz of 20/40 kHz |

---

## 4. Resonance Check

**Separation criterion:** keep each mode at least ±15–20% away from every excitation line. That means no mode inside 16–24 kHz or 32–48 kHz.

| Mode | f_n | Nearest excitation | Margin | Status |
|---|---|---|---|---|
| Bending 1 (loaded) | 158 Hz | 20 kHz | r ≈ 126 | Safe |
| Torsion 1 | ~200–600 Hz | 20 kHz | r > 30 | Safe |
| Bending 3 | ~14.5–15.9 kHz | 20 kHz | −20 to −27% | **Marginal: verify** |
| Bending 4 | ~28–31 kHz | 20/40 kHz | ~30% | OK |
| Local plate/pocket modes (thin CNC webs) | often 10–30 kHz | 20 kHz | unknown | **FEA required** |

**Transmissibility (SDOF, frequency ratio r = f/f_n):**

$$T = \frac{\sqrt{1+(2\zeta r)^2}}{\sqrt{(1-r^2)^2+(2\zeta r)^2}}$$

- Fundamental at r = 126: T ≈ 6×10⁻⁵, so it is fully isolated.
- A local mode sitting exactly at 20 kHz with ζ = 0.01: T = 1/(2ζ) = **50×** amplification. This shows up as audible whine, IMU noise aliasing, and fretting at fasteners.

---

## 5. Secondary Constraint: Control Bandwidth

$$f_1 \geq (3\text{–}5)\times f_{BW,\text{roll loop}}$$

With f₁ = 158 Hz, the roll loop bandwidth should be capped at about 30–50 Hz. Otherwise you need a notch filter at f₁, or a stiffer arm. This, not the 20 kHz carrier, is usually the binding constraint.

Also check IMU aliasing. Mode energy near f_s·k ± f_n (for example, a gyro sampled at 8 kHz) folds back into the control band.

---

## 6. Verification and Mitigation

1. **Modal FEA** (free plus bolted boundary conditions). Extract modes up to 50 kHz and flag any within 16–24 kHz or 32–48 kHz.
2. **Experimental modal test.** Use a tap test with an accelerometer or microphone and an FFT. Alternatively, sweep the PWM frequency from 16 to 32 kHz and log the IMU and acoustic response.
3. **If a mode falls in a band:**
   - Shift the carrier to 24–32 kHz, or use spread-spectrum/randomized PWM.
   - Thicken thin pockets or add ribs. Since f scales with h for plates, a 20% thickness increase gives about +20%.
   - Add constrained-layer damping or a viscoelastic washer at the motor mount (ζ rises to 0.05–0.1).
4. **Iteration rule:** Δf/f ≈ ½(Δk/k − Δm/m). Use this for quick geometry trades between cycles.

---

## Summary

The arm's global modes (~158 Hz bending, a few hundred Hz torsion) sit far below 20 kHz and are effectively immune to carrier excitation. The real risks are:

- **High-order and local web modes** in the 14–24 kHz and 32–48 kHz bands, which need FEA or tap-test confirmation.
- **The low fundamental limiting roll-loop bandwidth**, which calls for a notch filter or added stiffness.