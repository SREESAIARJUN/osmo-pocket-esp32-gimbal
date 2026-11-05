# SMBJ16A Clamp Response to Regenerative-Braking Back-EMF Transients

I don't have the operating data for "Cycle 8" (bus voltage, phase inductance, braking current, event rate). The model below is therefore parametric, with a worked example that uses assumed values. Substitute your measured numbers to get a pass/fail result.

---

## 1. Device Model (SMBJ16A, unidirectional)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage (at 1 mA) | V_BR | 17.8 V min / 18.7 V nom / 19.7 V max |
| Max clamp voltage (at I_PP) | V_C | 26.0 V |
| Peak pulse current (10/1000 µs) | I_PP | 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Breakdown temperature coefficient | α_VBR | ≈ +0.09 %/°C |

**Piecewise-linear clamp model:**

$$
V_{TVS}(I) =
\begin{cases}
\text{leakage } (\le 5\,\mu A), & V < V_{BR} \\[4pt]
V_{BR}(T_j) + R_d \cdot I, & V \ge V_{BR}
\end{cases}
$$

The dynamic resistance follows from the datasheet points:

$$
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26.0 - 18.7}{23.1} \approx 0.32\ \Omega
$$

For worst case, use V_BR,max = 19.7 V, which gives R_d ≈ 0.27 Ω but a higher clamp voltage.

Breakdown voltage rises with junction temperature:

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_{VBR}(T_j - 25)]
$$

---

## 2. Circuit Model During Regenerative Braking

During braking, the motor back-EMF E = K_e·ω drives current back through the MOSFET body diodes or synchronous FETs into the DC bus. Kirchhoff's current law at the bus node:

$$
I_{regen}(t) = C_{bus}\frac{dV_{bus}}{dt} + I_{batt}(t) + I_{TVS}(V_{bus}) + I_{load}
$$

Battery-side current is set by the wiring inductance and battery resistance:

$$
V_{bus} - V_{batt} = L_{line}\frac{dI_{batt}}{dt} + R_{batt}\, I_{batt}
$$

The TVS conducts significantly only when V_bus exceeds V_BR(T_j). Three distinct stress regimes follow.

---

## 3. Regime A: Phase-Commutation Inductive Dump (µs scale)

When a phase switches off at current I₀, the phase inductance L_ph demagnetizes into the bus. If the bus capacitor is too small or too far away (high ESL), the TVS absorbs the spike.

**Current decay (linear, since the clamp holds voltage roughly constant):**

$$
i_{TVS}(t) = I_0 - \frac{V_{clamp} - V_{bus}}{L_{ph}}\,t
$$

**Pulse duration:**

$$
t_{clamp} = \frac{L_{ph}\, I_0}{V_{clamp} - V_{bus}}
$$

**Energy absorbed.** The supply keeps pushing current during the clamp, which enlarges the energy:

$$
E_{TVS} = \tfrac{1}{2} L_{ph} I_0^2 \cdot \frac{V_{clamp}}{V_{clamp} - V_{bus}}
$$

**Peak power:**

$$
P_{pk} = I_0 \,(V_{BR} + R_d I_0)
$$

---

## 4. Regime B: Line-Inductance Kick on Regen Current Reversal (tens of µs)

When braking begins abruptly, battery current reverses. The wiring inductance opposes the change and the bus overshoots:

$$
\Delta V_{bus} \approx L_{line}\,\frac{\Delta I}{\Delta t}
\quad\text{or, with } C_{bus}:\quad
\Delta V_{pk} \approx \Delta I \sqrt{\frac{L_{line}}{C_{bus}}}
$$

The TVS conducts if V_bus + ΔV_pk > V_BR. Its energy then follows the same form as Regime A, with L_line in place of L_ph.

---

## 5. Regime C: Sustained Bus Pumping (ms to s): the critical failure mode

If the battery cannot accept charge (BMS charge FET open, full battery, bench power supply, or a disconnected connector), regen current has no sink except C_bus and the TVS.

The bus charges at:

$$
\frac{dV_{bus}}{dt} = \frac{I_{regen}}{C_{bus}}
$$

so it reaches V_BR within roughly C·ΔV / I, typically microseconds to milliseconds. After that, the TVS carries the regen current continuously:

$$
P_{TVS} = I_{regen}\,(V_{BR} + R_d I_{regen})
$$

An SMB package handles only about 1–5 W steady state. Survival time comes from its transient thermal impedance:

$$
\Delta T_j(t) = P_{TVS}\cdot Z_{th}(t) \quad\Rightarrow\quad \text{fail when } T_j > 150\text{–}175\,^\circ\text{C}
$$

**The SMBJ16A cannot be the regen energy sink.** It can only clip edges. Sustained regen must be handled by the battery, a brake chopper or resistor, or firmware that limits regen current based on bus voltage.

---

## 6. Pulse-Width Derating

The 600 W rating applies to a 10/1000 µs waveform. Peak power capability scales approximately as:

$$
P_{max}(t_p) \approx 600\,\text{W}\left(\frac{1000\,\mu s}{t_p}\right)^{0.5}
$$

Read the datasheet curve for exact values. A triangular pulse delivers about half the energy of a rectangular pulse with the same peak, which is roughly equivalent to a rectangular pulse of length t_clamp/2.

**Repetitive events.** Average dissipation must stay within the PCB's thermal limit:

$$
P_{avg} = E_{TVS}\cdot f_{events} \le P_{D,max} \ (\sim 1\text{–}3\,\text{W realistic on FR4})
$$

---

## 7. Worked Example (assumed values)

**Assumptions:** 4S Li-ion bus at 14.8 V nominal (16.8 V full charge), L_ph = 20 µH, I₀