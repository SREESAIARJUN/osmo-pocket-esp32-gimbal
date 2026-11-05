# SMBJ16A Clamp Response to Motor Back-EMF Spikes During Regenerative Braking

**About "Cycle 27":** I don't have your test log, waveform captures, or the parameters for that cycle. Below is a general formulation with an example calculation. If you send the Cycle 27 data (bus voltage, braking current, inductance, repetition rate, ambient temperature), the numbers can be replaced directly.

---

## 1. Device Parameters (SMBJ16A, typical datasheet values)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage | V_BR | 17.8 V min, ~18.7 V nominal, 19.7 V max (at I_T = 1 mA) |
| Max clamping voltage | V_C | 26.0 V at I_PP = 23.1 A |
| Peak pulse power | P_PP | 600 W (10/1000 µs waveform) |
| Steady-state dissipation | P_D | ~5 W (ideal heatsinking), realistically 1–2 W on a PCB |
| V_BR temperature coefficient | α_T | ≈ +0.1 %/°C (check your vendor's datasheet) |

**Voltage compatibility check:** A 3S Li-ion pack (12.6 V maximum) is safe. A 4S pack (16.8 V maximum) exceeds V_RWM. Leakage will then rise sharply, and the diode can partially conduct during normal operation. In that case, choose an SMBJ20A or higher.

---

## 2. Clamp Model

Model the diode as a piecewise-linear device with temperature dependence:

$$
V_{TVS}(I, T_j) = V_{BR}\,[1 + \alpha_T (T_j - 25^\circ C)] + R_d \cdot I
$$

The dynamic resistance R_d follows from the datasheet clamping point:

$$
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26.0 - 18.7}{23.1} \approx 0.32\ \Omega
$$

---

## 3. Inductive Energy Dump into the Clamp

Consider an inductance L carrying current I₀ that is suddenly interrupted, for example at MOSFET turn-off or when the regen current path opens. The inductor then forces current into the TVS against the source voltage V_s (the bus or battery). Using the average clamp voltage V_cl, the current ramps down linearly:

$$
i(t) = I_0 - \frac{V_{cl} - V_s}{L}\,t
$$

The clamp conduction time is:

$$
t_{cl} = \frac{L\,I_0}{V_{cl} - V_s}
$$

The energy absorbed by the TVS is:

$$
E_{TVS} = \tfrac{1}{2} L I_0^2 \cdot \frac{V_{cl}}{V_{cl} - V_s}
$$

The factor V_cl / (V_cl − V_s) is important. It means the TVS absorbs **more** than the stored inductive energy, because the source keeps pushing current through it during the clamp interval. With low clamping headroom, this multiplier becomes large.

The peak power at the start of the clamp interval is:

$$
P_{pk} = V_{TVS}(I_0)\cdot I_0
$$

---

## 4. Pulse Capability Check

**Peak power.** The 600 W rating applies to a 10/1000 µs pulse. For shorter pulses, allowable peak power rises roughly as P ∝ t^(−1/2) (Wunsch–Bell behaviour):

$$
P_{allow}(t_{cl}) \approx 600\ \text{W}\cdot\sqrt{\frac{1000\ \mu s}{t_{eq}}}
$$

**Single-pulse energy.** The rated 10/1000 µs pulse corresponds to roughly 0.85 J (600 W × τ, where τ ≈ 1.44 ms). Single events should stay well below this.

**Repetitive events.** Each braking cycle adds heat, so the junction temperature must be tracked:

$$
P_{avg} = E_{TVS}\cdot f_{rep}, \qquad
T_j = T_a + P_{avg}\,R_{\theta JA} + P_{pk}\,Z_{\theta}(t_{cl})
$$

For an SMB package, R_θJA is about 75–100 °C/W on minimum pads. Rising T_j raises V_BR, which raises V_cl. That increases headroom slightly but also stresses downstream parts.

---

## 5. Worked Example (illustrative values, not Cycle 27 data)

**Case A: commutation spike from wiring/stray inductance.**
Assumptions: L = 200 nH, I₀ = 30 A, V_s = 12.6 V.

- Clamp voltage: V_cl ≈ 18.7 + 0.32 × 30 ≈ **28.3 V** at peak. This is above the 26 V datasheet figure because I₀ > I_PP.
- Clamp time: t_cl = (200 nH × 30 A) / (28.3 − 12.6) ≈ **0.38 µs**
- Absorbed energy: E_TVS ≈ 90 µJ × (28.3 / 15.7) ≈ **160 µJ**
- Peak power: P_pk ≈ **850 W** for under 1 µs, comfortably within short-pulse capability.

Even at a 20 kHz PWM edge rate, P_avg ≈ 3.2 W. That is marginal on an SMB. So if every switching edge clamps, the root cause is gate drive or layout (snubber, decoupling capacitance), not TVS sizing.

**Case B: regen bus pumping with no absorbing path** (battery disconnected, BMS open, or high-ESR wiring).
Assumptions: 40 A braking current, bus near 12.6 V.

The regenerated power is ≈ V × I ≈ **500 W or more, continuously, for milliseconds to seconds**. This greatly exceeds the SMBJ16A's steady-state rating, so the device will fail short (its typical failure mode). That can collapse the bus or damage the ESC.

---

## 6. Interpretation and Design Guidance

1. **The SMBJ16A handles fast commutation spikes well** (nanosecond- to microsecond-scale, microjoule-scale events). Place it close to the bus capacitors with minimal loop inductance.
2. **It cannot absorb sustained regenerative energy.** For braking energy, use:
   - a guaranteed battery return path, with the BMS allowing charge current;
   - sufficient low-ESR bulk capacitance;
   - a brake chopper (resistor plus MOSFET triggered around 15 V); or
   - firmware limits on regen current tied to measured bus voltage.
3. **Verify the clamp level against your MOSFETs' V_DS rating.** A 28–30 V clamp is fine for 40 V FETs but gives too little margin for 