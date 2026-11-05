# Mechanical Resonance and Natural Frequency Analysis: CNC Aluminum Roll Arm under a 20 kHz PWM Carrier (Cycle 26)

I don't have your Cycle 26 drawing or parameters, so this uses a **parametric baseline**. Substitute your real geometry and masses into the equations. The method is: (1) define the excitation spectrum, (2) compute the structural modes, (3) check the separation margins, and (4) verify with FEA and test.

---

## 1. Baseline Assumptions

| Parameter | Symbol | Value (replace with actual) |
|---|---|---|
| Material | – | Al 6061-T6 |
| Young's modulus | E | 68.9 GPa |
| Density | ρ | 2700 kg/m³ |
| Shear modulus | G | 26 GPa |
| Free arm length | L | 60 mm |
| Section (width × thickness) | b × h | 12 × 4 mm |
| Boundary condition | – | Clamped at the motor hub, free or tip-loaded at the far end |
| Tip payload (motor/camera) | m_tip | 50 g |
| Structural damping | ζ | 0.002–0.005 (bare Al; Q ≈ 100–250) |

---

## 2. Excitation Spectrum (Forcing Function)

The PWM carrier itself isn't the only line. You must map the full spectrum.

**Electrical frequency:**

$$f_e = \frac{p \cdot n_{rpm}}{60}, \quad p = \text{pole pairs}$$

**Carrier-band harmonics (SVPWM).** The current ripple, and therefore the Lorentz/Maxwell force ripple, appears at:

$$f_{exc} = m f_c \pm k f_e$$

- With f_c = 20 kHz, the dominant lines are 20 kHz ± 2f_e and 40 kHz ± f_e.
- Maxwell radial force scales with B², so it can produce force components at the sum and difference of the flux harmonics. Include the 2× products.
- Center-aligned PWM pushes much of the ripple energy to **2f_c = 40 kHz**.

**Low-frequency torque lines.** These are what excite the fundamental modes:

- Cogging: $f_{cog} = \text{LCM}(Q_s, 2p) \cdot n/60$
- Torque ripple: $6f_e$, $12f_e$
- Control-loop bandwidth: the gimbal PID typically runs at 100–500 Hz.

**Bands to protect:**
- **Band A (PWM):** 20 kHz ± (sideband span + 15%) → roughly **16.5–23.5 kHz**
- **Band B:** 40 kHz ± 15%
- **Band C (control/torque):** 0 to about 3× the loop bandwidth

---

## 3. Natural Frequency Formulation

### 3.1 Bending: Euler–Bernoulli cantilever

$$f_i = \frac{(\beta_i L)^2}{2\pi L^2}\sqrt{\frac{EI}{\rho A}}$$

with β₁L = 1.875, β₂L = 4.694, β₃L = 7.855, β₄L = 10.996, and βᵢL ≈ (2i−1)π/2 for higher modes.

**Flap direction (bending about the thin axis):**
- $I = bh^3/12 = 64\ \text{mm}^4$
- $A = 48\ \text{mm}^2$
- $\sqrt{EI/\rho A} = \sqrt{4.41/0.1296} = 5.83\ \text{m}^2/\text{s}$

**In-plane direction (bending about the wide axis):**
- $I = hb^3/12 = 576\ \text{mm}^4$, which raises every frequency by a factor of 3.

| Mode | Flap (thin axis) | In-plane (wide axis) |
|---|---|---|
| 1 | 907 Hz | 2.72 kHz |
| 2 | 5.68 kHz | **17.0 kHz ⚠** |
| 3 | 15.9 kHz | 47.7 kHz |
| 4 | **31.2 kHz** | – |

### 3.2 Torsion: rectangular section, clamped–free

$$f_{t,i} = \frac{(2i-1)}{4L}\sqrt{\frac{GJ}{\rho I_p}}, \quad J \approx \beta b h^3,\ \beta(b/h=3)\approx 0.263$$

- $J \approx 202\ \text{mm}^4$
- $I_p = bh(b^2+h^2)/12 = 640\ \text{mm}^4$
- $\sqrt{GJ/\rho I_p} \approx 55.2\ \text{m/s}$

This gives:
- f_t1 ≈ 230 Hz (bare arm, before tip inertia)
- f_t,i up to roughly 0.23 × 87 ≈ 20 kHz near mode 44

Torsional mode density is high near 20 kHz. Rely on FEA here, not the 1-D formula.

### 3.3 Tip-mass correction (Rayleigh, fundamental)

$$k = \frac{3EI}{L^3}, \quad m_{eff} = m_{tip} + 0.236\,m_{beam}, \quad f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}$$

- Flap: k = 61.3 kN/m and m_eff = 51.8 g, so **f₁ ≈ 173 Hz**.

This fundamental falls inside or near Band C, which matters for gimbal control. The tip mass mostly drops the low modes. The high modes near 20 kHz are local and depend mainly on the arm's own stiffness and mass, so the bare-beam values above stay representative.

### 3.4 Bending wavelength at 20 kHz (sets the FEA mesh size)

$$k_b = \left(\frac{\omega^2 \rho A}{EI}\right)^{1/4} = 147\ \text{m}^{-1} \Rightarrow \lambda_b = 42.8\ \text{mm}$$

The arm is longer than one wavelength, so expect **local panel, pocket, and fillet modes**, not just beam modes.

---

## 4. Resonance Assessment

**Frequency ratio and dynamic amplification:**

$$r = \frac{f_{exc}}{f_n}, \qquad H(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}$$

At r = 1 with ζ = 0.003, H ≈ 167. At ±15% separation, H ≈ 3.

**Separation criterion:**

$$\left| \frac{f_n - f_{exc}}{f_{exc}} \right| \geq 15\% \quad \text{(use 20\% if sidebands are wide or } f_c \text{ is adaptive)}$$

| Mode | f_n | Nearest excitation | Margin | Status |
|---|---|---|---|