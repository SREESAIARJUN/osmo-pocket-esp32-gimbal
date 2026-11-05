# TVS Clamp Response: SMBJ16A Under Regenerative-Braking Back-EMF Transients

I can't see your "Cycle 28" test data, so this framework uses representative ESC values. Replace the inputs in the parameter table with your measured values. The equations still apply.

---

## 1. Device Model (SMBJ16A, unidirectional)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage at 1 mA | V_BR | 17.8 V min / 19.7 V max (≈18.7 V nominal) |
| Max clamp voltage at I_PP | V_C | 26.0 V at 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Steady-state power (SMB, T_L = 75 °C) | P_D | ≈5 W |
| V_BR temperature coefficient | α_VBR | ≈ +0.1 %/°C |

**Piecewise-linear clamp model**

$$
V_{TVS}(i) =
\begin{cases}
V_{bus}, & V < V_{BR} \quad (\text{leakage only, } I_R \le 1\ \mu A) \\
V_{BR}(T_j) + R_d \cdot i, & i > 0
\end{cases}
$$

The dynamic resistance is:

$$
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26.0 - 18.7}{23.1} \approx 0.32\ \Omega
$$

The breakdown voltage drifts with junction temperature:

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_{VBR}(T_j - 25)]
$$

Clamp voltage therefore rises as the TVS heats during repeated braking pulses.

---

## 2. Spike Source: Inductive Energy Dump

When the low-side or high-side FET turns off during regen, the current I₀ in the effective inductance L_eff must commutate. If the bus capacitors can't absorb it (because of high ESR/ESL, a long trace, or an open battery path), the TVS conducts.

**Current decay during clamp** (inductor discharged against the clamp voltage minus the source voltage):

$$
\frac{di}{dt} = -\frac{V_C - V_S}{L_{eff}}
\quad\Rightarrow\quad
i(t) = I_0 - \frac{V_C - V_S}{L_{eff}}\,t
$$

**Clamp duration:**

$$
t_{clamp} = \frac{L_{eff}\, I_0}{V_C - V_S}
$$

**Energy absorbed by the TVS:**

$$
E_{TVS} = \tfrac{1}{2} L_{eff} I_0^2 \cdot \frac{V_C}{V_C - V_S}
$$

The factor V_C/(V_C − V_S) matters. With only about 10 V of headroom above the bus, the TVS absorbs 2 to 3 times the stored inductive energy, because the source keeps feeding current while the clamp is active.

**Peak power:**

$$
P_{pk} = I_0 \cdot (V_{BR} + R_d I_0)
$$

---

## 3. Worked Example (Representative Values)

Assumptions: 3S pack, V_S = 12.6 V; I₀ = 30 A phase current at braking turn-off.

### Case A: Worst case, phase inductance dumps into the TVS (L_eff = 20 µH)

This applies when the battery is disconnected or the caps are inadequate.

- V_C,peak = 18.7 + 0.32 × 30 ≈ **28.3 V** (above the 26 V datasheet point, since I₀ > I_PP)
- Average clamp ≈ 23.5 V, so V_C − V_S ≈ 10.9 V
- t_clamp = (20 µ × 30) / 10.9 ≈ **55 µs**
- E_L = ½ × 20 µ × 30² = 9 mJ, and E_TVS ≈ 9 × 23.5/10.9 ≈ **19 to 24 mJ**
- P_pk ≈ **850 W**, triangular waveform

**Single-pulse check.** The power-versus-pulse-width curve scales roughly as P(t) ≈ 600·(1 ms / t)^0.5. That gives about 2.5 kW allowable at around 55 µs, so a single pulse passes with about 3× margin.

**Repetitive check.** At 20 kHz PWM, the dissipation would be about 24 mJ × 20 kHz ≈ 480 W. This would destroy the part within milliseconds.

### Case B: Realistic case, only stray loop inductance (L_eff ≈ 30 nH)

This applies when the body diodes and bulk caps are working.

- E ≈ ½ × 30 n × 900 × 2.2 ≈ 30 µJ per edge
- t_clamp is in the tens of ns, so the TVS response (<1 ps intrinsic, but package and lead inductance dominate) is adequate if the TVS is placed tight to the FET bridge.
- With two edges per period at 20 kHz: about 1.2 W average. This is sustainable only if the spike actually exceeds V_BR every cycle. Ideally the ringing stays below about 17.8 V and the TVS never conducts in normal operation.

---

## 4. Sustained Regen Pump-Up: The Real Hazard

If the battery cannot accept charge (full pack, BMS opens, connector bounce), the regen current charges the bus capacitance:

$$
\frac{dV_{bus}}{dt} = \frac{I_{regen}}{C_{bus}}
$$

For example, 15 A into 1000 µF gives 15 V/ms, so the bus reaches V_BR in under 1 ms. The TVS then becomes the only load:

$$
P_{TVS} = V_C \cdot I_{regen} \approx 22\ \text{V} \times 15\ \text{A} = 330\ \text{W, continuous}
$$

This is about 66× the 5 W steady-state rating. The SMBJ16A will fail short within milliseconds to tens of ms. A shorted TVS is a safer failure than an open one, but it still kills the ESC.

**Conclusion:** the TVS handles fast edge transients only. Sustained regen overvoltage needs:
- firmware bus-voltage limiting (reduce braking torque