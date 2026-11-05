# Mechanical Resonance and Natural Frequency Analysis: CNC Aluminum Roll Arm under a 20 kHz PWM Carrier (Cycle 17)

I don't have the earlier cycles, so I don't have your arm drawing or mass data. Below is the complete method with a worked example using stated **assumed values**. Replace them with your CAD numbers and the framework stays the same.

---

## 1. Problem Definition

**Objective:** Make sure no structural mode of the roll arm falls within the excitation bands created by the 20 kHz motor carrier. If one does, quantify the amplification.

**Excitation sources from the PWM-driven BLDC/PMSM:**

| Source | Frequency content |
|---|---|
| Phase-voltage PWM harmonics (SVPWM) | f_c ± 2f_e, f_c ± 4f_e, then 2f_c ± f_e, 2f_c ± 5f_e |
| Radial Maxwell / magnetostrictive force | ≈ f_c, 2f_c (dominant), with sidebands |
| Current-ripple torque | f_c, 2f_c |

The electrical frequency is

$$f_e = \frac{p \cdot n_{rpm}}{60}$$

Gimbal-type roll motors (p ≈ 7–12, slow rotation) give f_e of only a few Hz to tens of Hz.

**Critical bands:**
- **Band A:** 20 kHz ± ~0.5 kHz
- **Band B:** 40 kHz ± ~0.5 kHz

---

## 2. Assumed Model (replace with your data)

| Parameter | Value |
|---|---|
| Material | Al 6061-T6: E = 68.9 GPa, ρ = 2700 kg/m³, ν = 0.33 |
| Arm idealization | Cantilever clamped at roll motor hub |
| Length L | 80 mm |
| Cross-section | b = 20 mm × h = 4 mm (rectangular) |
| Tip mass M (payload/next motor) | 0.30 kg |
| Structural damping ζ | 0.01–0.02 (bolted aluminum) |

**Section properties:**
- Out-of-plane (weak axis): I_w = bh³/12 = 106.7 mm⁴ = 1.067×10⁻¹⁰ m⁴
- In-plane (strong axis): I_s = hb³/12 = 2667 mm⁴ = 2.667×10⁻⁹ m⁴
- A = 80 mm², ρA = 0.216 kg/m, beam mass m_b = 17.3 g

---

## 3. Governing Equations

### 3.1 Euler–Bernoulli beam (bending modes)

$$EI\frac{\partial^4 w}{\partial x^4} + \rho A\frac{\partial^2 w}{\partial t^2} = 0$$

For a clamped-free beam:

$$f_n = \frac{(\beta_n L)^2}{2\pi L^2}\sqrt{\frac{EI}{\rho A}}$$

The eigenvalues solve 1 + cos(βL)·cosh(βL) = 0:

| Mode n | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| β_nL | 1.875 | 4.694 | 7.855 | 10.996 | 14.137 |

For n > 2, β_nL ≈ (2n−1)π/2.

### 3.2 Fundamental mode with tip mass (Rayleigh)

$$f_1 = \frac{1}{2\pi}\sqrt{\frac{3EI/L^3}{M + 0.2357\,m_b}}$$

### 3.3 Torsional modes (if the payload is offset from the arm axis)

$$f_{t,n} = \frac{(2n-1)}{4L}\sqrt{\frac{GJ}{\rho I_p}}, \qquad J \approx \beta\, b h^3 \;(\text{St. Venant})$$

With a tip inertia J_tip, the fundamental is:

$$f_{t1} = \frac{1}{2\pi}\sqrt{\frac{GJ/L}{J_{tip}}}$$

### 3.4 Forced response (amplification near a mode)

With frequency ratio r = f_exc / f_n:

$$H(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad H_{max} = Q = \frac{1}{2\zeta}$$

For ζ = 0.01–0.02, Q is roughly 25–50.

---

## 4. Worked Results

### 4.1 Fundamental (tip-loaded, out-of-plane)

- EI_w = 7.35 N·m²
- k = 3EI/L³ = 43,070 N/m
- M_eff = 0.304 kg

$$f_1 = \frac{1}{2\pi}\sqrt{\frac{43{,}070}{0.304}} \approx \mathbf{60\ Hz}$$

This mode matters for the control loop (gimbal bandwidth and notch filter), not for the PWM carrier.

### 4.2 Higher bending modes (bare arm)

Base frequency: f₁,bare = 87.4 × √(34.0) ≈ **510 Hz**.

| Mode | Out-of-plane (weak) | In-plane (strong, ×5.0) |
|---|---|---|
| 1 | 510 Hz | 2.55 kHz |
| 2 | 3.20 kHz | **15.98 kHz** ⚠ |
| 3 | 8.95 kHz | 44.8 kHz |
| 4 | **17.54 kHz** ⚠ | — |
| 5 | **28.99 kHz** | — |
| 6 | ~43.3 kHz ⚠ (near 2f_c) | — |

### 4.3 Tip-mass correction for higher modes

A heavy tip mass pushes the higher modes toward clamped-pinned eigenvalues (βL = 3.927, 7.069, 10.21, 13.35, 16.49). That shifts:
- Mode 4 from 17.5 kHz to about **15.1 kHz**
- Mode 5 from 29 kHz to about **25.9 kHz**

The true values lie between the two boundary cases, so **FEA is required** to pin them down.

---

## 5. Separation Margin Assessment

**Criterion:** No mode within ±15% of any excitation line (±20% preferred for bolted assemblies, which have joint-stiffness uncertainty).

| Mode | f_n | r = 20k / f_n | Margin | H (ζ = 0.015) | Status |
|---|---|---|---|---|---|
| OOP-4 (bare)