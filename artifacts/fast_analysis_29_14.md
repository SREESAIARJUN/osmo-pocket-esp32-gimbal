# SMBJ16A Clamp Response Under Regenerative-Braking Back-EMF Transients

**About "Cycle 29":** I don't have your test log or cycle data. Below is a general formulation with placeholder values. If you share the measured bus voltage, current, and timing for Cycle 29, these equations can be evaluated directly.

---

## 1. Device Parameters (SMBJ16A, unidirectional, typical datasheet)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage at 1 mA | V_BR | 17.8 V min, ~18.75 V nominal, 19.7 V max |
| Max clamping voltage at I_PP | V_C | 26.0 V |
| Peak pulse current (10/1000 µs) | I_PP | 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Breakdown temperature coefficient | α_VBR | ≈ +0.1 %/°C |
| Junction capacitance | C_j | ~1–2 nF at 0 V bias |

Check exact values against your vendor's datasheet (Littelfuse, Vishay, Bourns, etc.).

---

## 2. Clamp I–V Model

Use a piecewise-linear model:

$$
V_{TVS}(I) =
\begin{cases}
\text{leakage region}, & V < V_{BR} \\[4pt]
V_{BR}(T_j) + R_d \cdot I, & I \ge 1\,\text{mA}
\end{cases}
$$

**Dynamic resistance:**

$$
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26.0 - 18.75}{23.1} \approx 0.31\,\Omega
$$

For worst-case clamp analysis, use V_BR,max = 19.7 V with R_d ≈ 0.27 Ω, which gives the highest V_C at any given current.

**Temperature correction:**

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + 0.001\,(T_j - 25)]
$$

At T_j = 100 °C, V_BR rises about 1.4 V. Because the TVS heats while it conducts, the clamp level creeps upward during a long regen event.

---

## 3. Transient Source Model During Regen Braking

The bus sees three superimposed mechanisms.

### (a) Regenerative current into a stiff-less bus

This is the case where the battery is disconnected, the BMS has opened, the connector bounces, or source impedance is high:

$$
\frac{dV_{bus}}{dt} = \frac{I_{regen} - I_{batt} - I_{TVS}}{C_{bus}}
$$

The time to reach clamp from the operating voltage is:

$$
t_{clamp} \approx \frac{C_{bus}\,(V_{BR} - V_{bus,0})}{I_{regen}}
$$

### (b) Inductive kick when current is interrupted

This includes phase inductance during commutation or PWM turn-off, and battery-lead inductance:

$$
E_L = \tfrac{1}{2} L\, I^2, \qquad V_{spike} = L\,\frac{di}{dt}
$$

### (c) Mechanical energy reservoir

This is the dominant term when the battery cannot accept charge:

$$
E_{mech} = \tfrac{1}{2} J\,(\omega_1^2 - \omega_2^2)\cdot\eta_{regen}
$$

---

## 4. TVS Response During Clamping

Once V_bus ≥ V_BR, the TVS current follows from the node equation:

$$
I_{TVS}(t) = I_{regen}(t) - I_{batt}(t) - C_{bus}\frac{dV_{bus}}{dt},
\qquad V_{bus} = V_{BR}(T_j) + R_d\, I_{TVS}
$$

**Clamped voltage:**

$$
V_{C,actual} = V_{BR}(T_j) + R_d\, I_{TVS,pk}
$$

**Instantaneous power and absorbed energy:**

$$
P_{TVS}(t) = V_{TVS}(t)\, I_{TVS}(t), \qquad E_{TVS} = \int P_{TVS}\,dt
$$

**Energy capability vs. pulse width.** For the 10/1000 µs waveform (τ ≈ 1.44 ms):

$$
E_{rated} \approx P_{PP}\,\tau \approx 600 \times 1.44\times10^{-3} \approx 0.8\text{–}0.9\,\text{J}
$$

For other pulse widths, scale with the datasheet P_PP-vs-t_p curve, which goes roughly as P ∝ t^(-0.5):

$$
P_{max}(t_p) \approx 600\,\text{W}\cdot\sqrt{\frac{1\,\text{ms}}{t_p}}
$$

**Survival criterion:**

$$
\bar{P}_{TVS}(t_p) < P_{max}(t_p) \quad\text{and}\quad T_j < 150\,°\text{C}
$$

---

## 5. Voltage Seen by the MOSFETs

The TVS does not clamp the FET directly. The loop parasitics add to the clamp voltage:

$$
V_{DS,pk} = V_{C,actual} + (L_{trace} + L_{TVS,lead})\,\frac{di_{TVS}}{dt}
$$

Example: with 10 nH of loop inductance and a 20 A rise in 20 ns, this adds 10 V. A 28 V clamp then becomes 38 V at the switch, which would fail a 30 V FET and come close on a 40 V FET.

---

## 6. Worked Example (Assumed Values: Substitute Your Cycle 29 Data)

**Assumptions:**
- 4S Li-ion bus at 16.8 V
- C_bus = 470 µF
- I_regen = 30 A
- Battery disconnects at the start of braking
- Phase inductance L_ph = 20 µH
- Lead inductance 300 nH
- Rotor stored energy 5 J

| Step | Result |
|---|---|
| Time to reach V_BR (18.75 V) | 470 µF × 1.95 V / 30 A ≈ **31 µs** |
| Clamp voltage at 30 A | 18.75 + 0.31 × 30 ≈ **28.0 V**, which exceeds the 26 V spec because I > I_PP |
| TVS power | 28 × 30 ≈ **840 W** |
| Phase-L kick energy | ½ × 20 µH × 30² = 9 mJ (easily absorbed) |
| Lead-L kick energy | ½ × 300 nH × 30² = 0.14 mJ (negligible energy, but a fast spike) |
| Rotor energy dumped into TVS | **~5 J**, about 6× the rating |
| Allowed duration at 840 W | ≈ (600/840)² × 1 ms ≈ **0.5 ms**, while braking lasts tens to hundreds of ms |

**Conclusion for this scenario:** the SMBJ16A handles the inductive spikes but **fails thermally (short-circuit mode)** under sustained regen with no battery sink.

---

## 7. Key Findings and Design Flags

1. **Standoff margin problem on 4S.** A full 4S pack (16.8 V) exceeds V_RWM (16 V). Regen overshoot of only about 1 V reaches V_BR,min (17.8 V). The TVS will conduct during normal regen, not just during faults. Consider an SMBJ20A–24A (check the FET rating) or a 3S bus.
2. **A TVS is a spike suppressor, not a brake resistor.**