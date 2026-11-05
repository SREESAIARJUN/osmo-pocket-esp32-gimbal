# SMBJ16A TVS Clamp Response to Motor Back-EMF Spikes During ESC Regenerative Braking

**A note on "Cycle 23":** I don't have context for what this refers to. It may be a test iteration, a drive-cycle index, or a design-review step. I've treated it as one event in a **repetitive braking sequence**, so the analysis covers both single-pulse and cumulative thermal behavior. Substitute your own values where marked.

---

## 1. Device Parameters (SMBJ16A, unidirectional, typical datasheet)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage (@1 mA) | V_BR | 17.8 V min / 18.75 V nom / 19.7 V max |
| Max clamping voltage | V_C | 26.0 V @ I_PP = 23.1 A |
| Peak pulse power | P_PP | 600 W (10/1000 µs waveform, 25 °C) |
| Leakage @ V_RWM | I_R | ≤ 1 µA |
| Junction capacitance | C_J | ~1–3 nF (bias dependent) |

---

## 2. Clamp Model (Piecewise-Linear)

$$
I_{TVS}(V) \approx
\begin{cases}
I_R, & V < V_{BR} \\[4pt]
\dfrac{V - V_{BR}}{R_{dyn}}, & V \ge V_{BR}
\end{cases}
\qquad\Rightarrow\qquad
V_{clamp}(I) = V_{BR} + R_{dyn}\, I
$$

Dynamic resistance, extracted from the datasheet points:

$$
R_{dyn} \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26.0 - 18.75}{23.1} \approx 0.31\ \Omega
\quad(\text{worst case with } V_{BR,min}: \approx 0.36\ \Omega)
$$

Add the temperature coefficient of V_BR (≈ +0.09 %/°C) for hot-junction behavior:

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_T (T_j - 25)]
$$

---

## 3. Spike Source Model

During rapid regen braking, the ESC switches phase current back into the DC bus. Two distinct overvoltage mechanisms exist. Keep them separate, because the TVS can handle only one of them.

### (a) Commutation spike: inductive kick from stray/loop inductance
When a MOSFET turns off, the current I₀ in loop inductance L_σ (PCB traces plus battery leads) must decay through the TVS:

$$
L_\sigma \frac{di}{dt} = -(V_{clamp} - V_{bus})
$$

Clamp conduction time:
$$
t_{clamp} = \frac{L_\sigma I_0}{V_{clamp} - V_{bus}}
$$

Energy absorbed by the TVS, including energy fed from the bus during the decay:
$$
E_{TVS} = \tfrac{1}{2} L_\sigma I_0^2 \cdot \frac{V_{clamp}}{V_{clamp} - V_{bus}}
$$

Average pulse power:
$$
P_{avg} \approx \frac{E_{TVS}}{t_{clamp}} = \tfrac{1}{2} I_0 V_{clamp}
$$

### (b) Bus pumping: sustained regen energy
If the battery cannot absorb the regen current (BMS open, high internal resistance, long leads, cold cell), the bus capacitor charges up:

$$
\frac{dV_{bus}}{dt} = \frac{I_{regen} - I_{batt}}{C_{bus}}, \qquad
I_{regen} \approx \frac{k_e\,\omega\, \cdot\, \text{duty}}{\ldots} \;\;(\text{ESC/motor dependent})
$$

The energy source here is the **kinetic energy of the rotor and load**, ½Jω², which is typically joules to hundreds of joules. **The TVS cannot absorb this.**

---

## 4. Worked Example (Mechanism a)

Assumptions (replace with your values): V_bus = 14.8 V (4S Li-ion nominal), L_σ = 100 nH, I₀ = 40 A at turn-off.

1. **Clamp voltage:** Since the current decays linearly from 40 A, the peak is
   $$V_{clamp,pk} = 18.75 + 0.31 \times 40 \approx 31.2\ \text{V}$$
   (Exceeding 26 V at 40 A is expected, because the datasheet V_C is specified at 23.1 A.)
2. **Clamp time:** Using the average clamp voltage ≈ 25 V,
   $$t_{clamp} = \frac{100\text{n}\cdot 40}{25 - 14.8} \approx 0.39\ \mu\text{s}$$
3. **Energy:**
   $$E_{TVS} = 0.5 \cdot 100\text{n} \cdot 40^2 \cdot \frac{25}{10.2} \approx 196\ \mu\text{J}$$
4. **Peak power:** ≈ 31 V × 40 A ≈ 1.25 kW for well under 1 µs.

**Rating check.** Pulse power capability scales roughly as P_PP ∝ t^(−0.5) relative to 1 ms:

$$
P_{allow}(0.4\,\mu s) \approx 600 \sqrt{\frac{1000}{0.4}} \approx 30\ \text{kW (curve-limited, typically capped ~10–20 kW on datasheet chart)}
$$

The single-pulse energy rating is about 600 W × ~1.4 ms equivalent ≈ 0.4–0.6 J. A 196 µJ pulse is about 0.05 % of that. **This is a single-pulse PASS.**

**MOSFET check.** The FETs see V_clamp,pk + ringing ≈ 31–35 V. You need FETs rated ≥ 40 V, preferably 60 V.

---

## 5. Repetitive / Cumulative Thermal Behavior ("Cycle 23" Context)

The ESC switches at f_sw (for example 24 kHz). If every turn-off edge clamps:

$$
P_{TVS,avg} = E_{TVS} \cdot f_{sw} = 196\ \mu\text{J} \times 24\ \text{kHz} \approx 4.7\ \text{W}
$$

The SMB package handles only ~0.5–1 W continuous (R_θJA ≈ 100 °C/W):

$$
\Delta T_j \approx 4.7 \times 100 = 470\ ^\circ\text{C} \;\Rightarrow\; \textbf{FAIL}
$$

Junction temperature across n braking cycles, with each braking event lasting t_brake and cooling between events:

$$
T_j[n] = T_a + P_{avg} Z_{th}(t_{brake}) + \sum_{k<n} \text{residual heating}
$$

By cycle 23, cumulative heating has raised V_BR. This increases the clamp voltage, which raises the energy per pulse. The result is thermal runaway toward short-circuit failure, which is the typical TVS failure mode.

The datasheet duty-cycle limit (typically 0.01 % for rated P_PP) confirms that the TVS **must not clamp every switching edge**.

---

## 6. Critical Design Issue: Voltage Coordination

| Bus | Max voltage | vs V_RWM = 16 V | vs V_BR,min = 17.8 V |
|---|---|---|---|
| 3S Li-ion | 12.6 V | OK | OK |
| 4S Li-ion | **16.8 V** | **Exceeds** | Margin only 1 V |
| 4S + regen rise (~10 %) | ~18.5 V | Exceeds | **TVS conducts continuously → destruction** |

**Conclusion:** The SMBJ16A is appropriate for 3S (≈12 V) systems. For 4S, use an **SMBJ20A–SMBJ22A** (and check FET rating against the new V_C ≈ 32–35 V).

---

## 7. Recommendations

1. **Use the TVS only for fast edges (mechanism a).** Minimize L_σ with tight bulk/ceramic capacitance at the half-bridges, so most commutation energy goes into C_bus rather than the TVS:
   $$\Delta V_{cap} = I_0 \sqrt{L_\sigma / C_{local}}$$
   The TVS should then conduct only on abnormal events.
2. **Handle bus pumping (mechanism b) actively:**
   - A brake chopper or resistor dump triggered at roughly V_bus,max + 5 %
   - ESC firmware that limits regen current when V_bus rises (current-limited braking)
   - Battery path impedance checks; never permit regen with the BMS open
3. **Upsize or parallel if needed.** Use an SMCJ (1500 W) or SMDJ (3000 W) part for higher margin, or parallel devices with matched V_BR.
4. **Validate by measurement.** Capture TVS current (current probe) and V_bus during a braking cycle. Integrate ∫V·I dt per event and compare against the derated P_PP curve at the measured T_j.

---

### Summary Formula Set

$$
V_{clamp} = V_{BR