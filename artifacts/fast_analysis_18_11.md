# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm vs. 20 kHz PWM Carrier

I don't have the specifics of your "Cycle 18" (dimensions, payload, motor model, drawings). The framework below is complete, and the worked numbers use **stated placeholder values**. Swap in your real geometry and the results follow directly.

---

## 1. Problem Definition

**Objective:** Make sure no structural mode of the roll arm is excited by motor-drive sources. The sources are:

| Source | Frequency | Notes |
|---|---|---|
| PWM carrier | f_c = 20 kHz | Torque/force ripple from phase-current ripple |
| Carrier ripple (center-aligned PWM) | 2f_c = 40 kHz | Often the dominant current-ripple component |
| Sidebands | f_c ± k·f_e | f_e = electrical frequency |
| Electrical/commutation | f_e = p·RPM/60 | p = pole pairs |
| Cogging torque | N_cog·f_mech | N_cog = LCM(slots, poles) |
| Control loop | f_BW (typically 20–200 Hz) | Most common real-world resonance driver |

**Design criteria:**
1. **Frequency separation:** |f_n − f_exc| / f_exc ≥ 0.2 (±20%) for every mode–source pair.
2. **Control rule:** f_n,1 ≥ 5 × f_BW (minimum 3×).
3. **Carrier rule:** No mode within ±20% of 20 kHz or 40 kHz, or the mode is shown to be non-participating (zero modal force at the motor interface).

---

## 2. Structural Model

The arm is modeled as an **Euler–Bernoulli cantilever**: clamped at the yaw/pitch interface, with a tip mass (roll motor plus payload).

### Material: 6061-T6 (CNC billet)
| Property | Value |
|---|---|
| E | 68.9 GPa |
| G | 26 GPa |
| ρ | 2700 kg/m³ |
| ζ (structural damping) | 0.001–0.005, so Q ≈ 100–500 |

### Placeholder geometry (replace with Cycle 18 values)
- L = 80 mm, b = 20 mm, h = 6 mm (bending in the h-direction)
- Tip mass M = 0.15 kg (motor + payload)

### Section properties
- I = bh³/12 = 3.6×10⁻¹⁰ m⁴
- EI = 24.8 N·m²
- A = 1.2×10⁻⁴ m², so m = ρA = 0.324 kg/m and m_beam = 0.026 kg

---

## 3. Natural Frequency Formulation

### 3.1 Fundamental mode with tip mass (Rayleigh)

$$
k = \frac{3EI}{L^3}, \qquad m_{eff} = M + 0.2357\,m_{beam}, \qquad f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}
$$

- k = 3(24.8)/(0.08)³ ≈ 1.45×10⁵ N/m
- m_eff = 0.15 + 0.0061 = 0.156 kg
- **f₁ ≈ 154 Hz**

### 3.2 Higher bending modes (distributed beam)

$$
f_i = \frac{(\beta_i L)^2}{2\pi}\sqrt{\frac{EI}{mL^4}}, \quad \beta_i L = 1.875,\ 4.694,\ 7.855,\ 10.996
$$

With √(EI/mL⁴)/2π ≈ 217.6 Hz:

| Mode | (β_iL)² | f (bare beam) | With tip mass (approx.) |
|---|---|---|---|
| 1 | 3.516 | 765 Hz | **~154 Hz** |
| 2 | 22.03 | 4.79 kHz | ~3.5–4.5 kHz |
| 3 | 61.70 | 13.4 kHz | ~11–13 kHz |
| 4 | 120.9 | **26.3 kHz** | ~22–25 kHz ⚠ |
| 5 | 199.9 | 43.5 kHz | ~38–42 kHz ⚠ (near 2f_c) |

For the exact tip-mass solution, solve the characteristic equation:

$$
1 + \cos\beta L\cosh\beta L + \frac{M}{mL}\,\beta L\,(\cos\beta L\sinh\beta L - \sin\beta L\cosh\beta L) = 0
$$

### 3.3 Torsional mode (roll axis)

$$
f_t = \frac{1}{2\pi}\sqrt{\frac{K_t}{J_{tip}}}, \qquad K_t = \frac{G\,J_{sec}}{L}, \qquad J_{sec} \approx \beta_t\, b h^3, \quad \beta_t(b/h = 3.33) \approx 0.29
$$

This mode couples directly with motor torque ripple, so evaluate it with your actual payload inertia J_tip.

### 3.4 Local and plate modes
Thin pockets, lightening holes, and the motor-mount flange produce **plate modes in the 5–30 kHz band**. These cannot be captured by beam theory and require FEA.

---

## 4. Response at 20 kHz

The force transmissibility for a mode near the carrier:

$$
T(r) = \frac{\sqrt{1+(2\zeta r)^2}}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad r = \frac{f_c}{f_n}
$$

- Coincidence (r = 1) with ζ = 0.002 gives **T ≈ 250**. A ~10 mN ripple force becomes audible whine, fretting at the fasteners, or IMU noise.
- Mode 4 at ~24 kHz gives r ≈ 0.83, which is a **17% separation and FAILS the ±20% criterion**.
- Mode 1 at 154 Hz gives r ≈ 130 and T ≈ 6×10⁻⁵. It is fully isolated from the carrier.

**Takeaway:** the carrier does not threaten the fundamental. The risk lies with high-order bending, torsional, and local plate modes, plus IMU aliasing.

---

## 5. Verification Procedure

1. **FEA modal analysis** (0–50 kHz):
   - Use 10-node tets with at least 3 elements through the thickness.
   - Model bonded or bolted contact at the clamp, and the motor and payload as a remote mass with inertia.
   - Extract modal effective mass and participation at the motor interface.
2. **Campbell diagram:** plot f_n vs. RPM against the lines f_e, 2f_e, cogging order, f_c, 2f_c, and f_c ± f_e.
3. **Experimental modal test:**
   - Impact hammer plus accelerometer or laser vibrometer, sampled at ≥100 kHz.
   - Compare against FEA and target ≤5% error on the first 5 modes.
4. **Operational test:** run the motor and FFT the accelerometer and IMU signals. Look for peaks at 20/40 kHz and for aliases at |f_c − k·f_s,IMU|.

---

## 6. Mitigation Options (if a mode lands within ±20% of 20 or 40 kHz)

| Lever | Effect |
|---|---|
| Increase h (f ∝ h for bending) | Shifts all bending modes upward |
| Add ribs or fillets at local pockets | Raises plate modes |
| Change carrier to 24–32 kHz, or use spread-spectrum PWM | Moves or smears the excitation |
| Add an output LC/ferrite or raise phase inductance | Reduces ripple amplitude |
| Add a viscoelastic damping layer or isolated IMU mount | Increases ζ by 10–50× and cuts Q |
| Set the control-loop notch at f₁ | Protects loop stability