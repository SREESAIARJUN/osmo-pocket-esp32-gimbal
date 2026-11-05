# Mechanical Resonance & Natural Frequency Analysis: CNC Aluminum Roll Arm under a 20 kHz PWM Carrier

I don't have the details of your "Cycle 24" design iteration (geometry, payload, motor). The formulation below is general. The worked numbers use **stated placeholder values**, so substitute your actual dimensions and masses.

---

## 1. Problem Definition

**Goal:** Ensure no structural mode of the roll arm is excited by:
1. **PWM carrier harmonics** (20 kHz and sidebands), which cause acoustic whine and high-frequency micro-vibration.
2. **Low-frequency motor and control excitations** (electrical frequency, cogging, servo loop), which cause jitter and loop instability.

**Design criterion:**

$$
\left|\frac{f_{n,i}}{f_{exc,j}} - 1\right| \geq 0.2 \quad \text{(±20% separation)}, \qquad f_{n,1} \geq (3\text{–}5)\times f_{BW,loop}
$$

---

## 2. Excitation Spectrum

### 2.1 Carrier-related (electromagnetic force ripple)

For a PMSM/BLDC driven by SVPWM at carrier frequency $f_c = 20$ kHz, the phase current ripple contains:

$$
f_{PWM} = m f_c \pm n f_e
$$

- $m$ odd → $n$ even
- $m$ even → $n$ odd

The torque and radial-force ripple ∝ $i_{ripple}$. The dominant terms are:

| Component | Frequency |
|---|---|
| 1st carrier group | $f_c \pm 2f_e$ ≈ 20 kHz |
| 2nd carrier group | $2f_c \pm f_e$ ≈ 40 kHz (often dominant with center-aligned PWM) |

The electrical frequency is:

$$
f_e = \frac{p \cdot n_{rpm}}{60}
$$

where $p$ is the number of pole pairs. Gimbal motors run slowly, so $f_e$ is only a few Hz to tens of Hz. The sidebands therefore cluster tightly around 20 kHz and 40 kHz.

### 2.2 Low-frequency excitations

- **Cogging:**
  $$f_{cog} = \text{LCM}(N_s, 2p)\cdot \frac{n_{rpm}}{60}$$
- **Commutation / torque ripple:** $6f_e$ (and multiples)
- **Servo loop:** bandwidth $f_{BW}$ (typically 20–150 Hz for gimbals), plus the control update rate (1–8 kHz) and its aliases
- **External:** propeller blade-pass frequency, if drone-mounted: $f_{BPF} = N_{blades}\cdot \text{RPM}/60$

---

## 3. Structural Model

### 3.1 Material (6061-T6, typical CNC stock)

| Property | Value |
|---|---|
| $E$ | 68.9 GPa |
| $\rho$ | 2700 kg/m³ |
| $\nu$ | 0.33 |
| $\zeta$ (bare Al, bolted) | ≈ 0.002–0.01 (very low, so resonant peaks are sharp) |

### 3.2 Idealization

Model the arm as a cantilever. The root is clamped at the yaw/pitch motor, and the tip carries a lumped mass $M_t$ (roll motor + payload) with rotary inertia $J_t$.

**Bare-beam bending modes (Euler–Bernoulli):**

$$
f_i = \frac{(\beta_i L)^2}{2\pi}\sqrt{\frac{EI}{\mu L^4}}, \quad \beta_i L = 1.875,\ 4.694,\ 7.855,\ldots
$$

where $\mu = \rho A$ is the mass per unit length.

**First mode with tip mass (Rayleigh approximation):**

$$
k = \frac{3EI}{L^3}, \quad m_{eff} = M_t + 0.2357\,\rho A L, \quad f_{n1} = \frac{1}{2\pi}\sqrt{\frac{k}{m_{eff}}}
$$

**Torsional mode (about the arm axis):**

$$
k_\theta = \frac{GJ}{L},\quad J \approx \beta\, b\, h^3 \ \text{(rectangular section)}, \quad f_\theta = \frac{1}{2\pi}\sqrt{\frac{k_\theta}{J_t + \tfrac{1}{3}\rho I_p L}}
$$

**Local panel/pocket modes:** These come from CNC lightening pockets with thin webs. Model each web as a plate:

$$
f_{plate} = \frac{\lambda^2}{2\pi a^2}\sqrt{\frac{D}{\rho t}},\quad D = \frac{Et^3}{12(1-\nu^2)}
$$

These are the modes most likely to land in the **10–40 kHz** band.

---

## 4. Worked Example (Placeholder Geometry)

**Assumed values:** $L = 60$ mm, $b = 20$ mm, $h = 6$ mm, $M_t = 0.10$ kg.

| Quantity | Value |
|---|---|
| $I_{flap} = bh^3/12$ | 3.6×10⁻¹⁰ m⁴ |
| $EI$ | 24.8 N·m² |
| $k = 3EI/L^3$ | 3.44×10⁵ N/m |
| Beam mass | 19.4 g → $m_{eff}$ = 0.1046 kg |
| **$f_{n1}$ flapwise (with tip mass)** | **≈ 289 Hz** |
| $f_{n1}$ edgewise ($I$ ×11.1) | ≈ 963 Hz |
| Bare-beam modes 1 / 2 / 3 | 1.36 kHz / 8.5 kHz / **23.9 kHz** |

**Observations:**

1. **Carrier isolation of the fundamental mode.** The frequency ratio is $r = 20000/289 ≈ 69$. The transmissibility is:
   $$
   T \approx \frac{1}{r^2 - 1} \approx 2\times10^{-4}
   $$
   This is negligible. The global mode operates in the mass-controlled region at 20 kHz.

2. **Risk zone.** The 3rd bending mode and any pocket-web modes sit near 20–24 kHz. With $\zeta ≈ 0.005$, the amplification factor is:
   $$
   Q = \frac{1}{2\zeta} ≈ 100
   $$
   A mode within ±20% of 20 kHz or 40 kHz is therefore a concern.

3. **Control-loop check.** With $f_{BW} = 60$ Hz, the ratio $289/60 ≈ 4.8$ is acceptable but marginal. Aim for >5× with heavier payloads.

---

## 5. Forced Response Check

Treat each mode as a single-degree-of-freedom system:

$$
\frac{X_i}{F_i/k_i} = \frac{1}{\sqrt{(1-r_i^2)^2 + (2\zeta_i r_i)^2}}, \quad r_i = \frac{f_{exc}}{f_{n,i}}
$$

The force amplitude comes from the ripple:

$$
F_{ripple} \approx \frac{K_t\, \Delta i_{pp}}{r_{arm}}, \qquad \Delta i_{pp} \approx \frac{V_{dc}}{4 L_s f_c}\ \text{(worst case, 50\% duty)}
$$

**Acceptance criterion:** Tip displacement × lever arm must stay below the angular jitter budget (e.g. <1 µrad for imaging). Acoustic emission must also be acceptable, since 20 kHz is at the edge of audibility.

---

## 6. FEA Modal Procedure

1. Import the CAD of the Cycle-24 arm, including pockets, fillets, and bolt bosses.
2. Mesh with second-order tet