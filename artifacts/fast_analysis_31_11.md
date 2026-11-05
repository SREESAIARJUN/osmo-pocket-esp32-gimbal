# Natural Frequency and Resonance Analysis: CNC Aluminum Roll Arm vs. 20 kHz PWM Carrier (Cycle 31)

I don't have the Cycle 31 drawings or test data from your earlier work. The numbers below use a **representative baseline geometry**. Replace the values in §1 with your actual dimensions and the method applies unchanged.

---

## 1. Model Definition and Assumptions

| Parameter | Symbol | Baseline value |
|---|---|---|
| Material | – | 6061-T6 Al |
| Young's modulus | E | 68.9 GPa |
| Shear modulus | G | 26 GPa |
| Density | ρ | 2700 kg/m³ |
| Free length (clamp → tip) | L | 60 mm |
| Section width | b | 12 mm |
| Section thickness | h | 4 mm |
| Tip payload (motor/camera) | M_tip | 50 g |
| Structural damping | ζ | 0.001–0.005 (Q ≈ 100–500) |

**Boundary condition:** fixed-free cantilever, with the root bolted to the motor hub and the tip carrying the payload.

---

## 2. Governing Equations

### 2.1 Euler–Bernoulli bending (distributed mass, no tip mass)

$$
f_n = \frac{(\beta_n L)^2}{2\pi}\sqrt{\frac{EI}{\rho A L^4}}
$$

The cantilever eigenvalues are βₙL = 1.875, 4.694, 7.855, 10.996.

Section properties for the out-of-plane case (bending through the thin dimension):

- I = bh³/12 = 64 mm⁴ = 6.4×10⁻¹¹ m⁴
- A = 48 mm²
- ρA = 0.1296 kg/m
- EI = 4.41 N·m²

### 2.2 Fundamental mode with tip mass (Rayleigh approximation)

$$
k = \frac{3EI}{L^3}, \qquad m_{eff} = M_{tip} + 0.2357\, m_{beam}, \qquad f_1 = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}
$$

### 2.3 Torsion (fixed-free)

$$
f_{t,n} = \frac{2n-1}{4L}\sqrt{\frac{G J}{\rho I_p}}, \qquad J \approx \beta\, b h^3,\ \ \beta \approx 0.263 \text{ for } b/h = 3
$$

---

## 3. Baseline Results

| Mode | Formula basis | f_n |
|---|---|---|
| Out-of-plane bending 1, with 50 g tip | Rayleigh, k = 61.3 kN/m | **≈ 173 Hz** |
| Out-of-plane bending 1, bare | β₁L = 1.875 | 906 Hz |
| Out-of-plane bending 2 | ×6.27 | 5.68 kHz |
| Out-of-plane bending 3 | ×17.55 | **15.9 kHz** |
| Out-of-plane bending 4 | ×34.39 | 31.2 kHz |
| In-plane bending 1 (I = 576 mm⁴, ×3) | – | 2.72 kHz |
| In-plane bending 2 | – | **17.0 kHz** |
| In-plane bending 3 | – | 47.7 kHz |
| Torsion 1 | §2.3 | compute with your J; typically 5–15 kHz for this size |

The tip mass mainly lowers mode 1. Higher modes are only weakly affected because the tip becomes a near-node for them.

---

## 4. Excitation Spectrum from the Motor Drive

The roll arm is excited by electromagnetic forces from the current ripple:

| Source | Frequency |
|---|---|
| PWM carrier ripple | f_c = 20 kHz |
| Carrier sidebands | f_c ± k·f_e, where f_e = p·RPM/60 |
| Maxwell / radial force (∝ i²) | 2f_c = 40 kHz, plus sidebands |
| Center-aligned SVPWM dominant ripple | often at 2f_c in the phase voltage |
| Low-frequency drivers | cogging (N_slots·p-related), FOC loop bandwidth, IMU/gyro loop |

Gimbal motors run slowly (f_e is tens of Hz), so the sidebands sit within about ±0.5 kHz of 20 kHz and 40 kHz.

---

## 5. Resonance Margin Criterion

Separation margin:

$$
\text{SM} = \frac{|f_n - f_{exc}|}{f_{exc}}
$$

Required: SM ≥ 15–20%. This covers machining tolerance (±0.05 mm on h gives about ±1.3% in f, because f ∝ h), bolt preload variation, and the scatter in E.

Dynamic amplification near resonance:

$$
H(r) = \frac{1}{\sqrt{(1-r^2)^2 + (2\zeta r)^2}}, \qquad r = f_{exc}/f_n
$$

| Mode | f_n | Nearest excitation | SM | Verdict |
|---|---|---|---|---|
| OOP-3 | 15.9 kHz | 20 kHz | 20.5% | Marginal pass (H ≈ 1.7) |
| IP-2 | 17.0 kHz | 20 kHz | 15.0% | **Borderline**: clamp stiffness changes could close the gap |
| OOP-4 | 31.2 kHz | 40 kHz | 22% | Pass |
| IP-3 | 47.7 kHz | 40 kHz | 19% | Pass |

Aluminum has a Q of roughly 300. An exact coincidence would therefore amplify the response about 300×, which shows up as audible whine, IMU noise aliasing, and fastener fretting.

---

## 6. Verification Steps

1. **FEA modal analysis** (e.g., SolidWorks Simulation or ANSYS):
   - Model the bolted root as a bonded contact, then repeat with a frictional contact to bracket the result.
   - Include the payload as a remote mass with its full inertia.
   - Extract modes up to 50 kHz.
2. **Experimental modal test:**
   - Use an impact hammer with a miniature accelerometer (under 1 g, to avoid mass loading), or a laser Doppler vibrometer.
   - Measure the FRF to 50 kHz.
3. **Operational check:**
   - Run the motor at its holding torque.
   - Take an FFT of the accelerometer or microphone signal.
   - Look for peaks at 20 kHz, 40 kHz, and their ±k·f_e sidebands.
4. **Correlation:** FEA and test should agree within ±5%. If not, update the root stiffness.

---

## 7. Mitigation Options (if a mode falls within ±15%)

| Option | Effect |
|---|---|
| Change h (f ∝ h for bending) | +0.5 mm on 4 mm raises bending modes by about 12.5% |
| Add ribs or pockets | Tune the in-plane and out-of-plane modes independently |
| Shift f_c (e.g., to 24 or 32 kHz) | Usually the easiest fix; also moves the excitation further from audible range |
| Spread-spectrum / randomized PWM | Flattens the carrier peak and reduces H at resonance |
| Constrained-layer damping tape | Raises ζ toward 0.02–0.05, so H at resonance drops from about 300 to about 10–25 |
| Hard anodize | Negligible effect on f_n; don't rely on it |

---

**Summary:** With the baseline geometry, the in-plane 2nd bending mode (about 17 kHz) and the out-of-plane 3rd bending mode (about 16 kHz) are the critical modes relative to the 20 kHz carrier.