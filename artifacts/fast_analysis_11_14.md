# SMBJ16A Clamp Response Under Regenerative-Braking Back-EMF Spikes

I don't have your earlier cycles, so this uses explicit assumptions. Replace them with your measured values: bus voltage, wiring inductance, bus capacitance, and phase current.

**Assumed baseline**
- 4S Li-ion/LiPo bus: 14.8 V nominal, 16.8 V full charge.
- 30 V MOSFETs in the ESC.
- 470 µF low-ESR bulk bus capacitance.
- 300–500 nH of battery-lead inductance.
- 30–40 A peak phase current during hard braking.

---

## 1. SMBJ16A Device Model

| Parameter | Value |
|---|---|
| V_RWM (standoff) | 16.0 V |
| V_BR (breakdown at 1 mA) | 17.8 V min / ~18.7 V typ / 19.7 V max |
| V_C at I_PP | 26.0 V at 23.1 A (10/1000 µs pulse) |
| P_PP | 600 W (10/1000 µs pulse) |
| Steady-state dissipation | ~5 W (heavily layout-dependent) |
| Temperature coefficient of V_BR | ≈ +0.1 %/°C, about +19 mV/°C |

**Piecewise-linear clamp model**

$$
i_{TVS}(V)=\begin{cases}\approx 0 \ (\text{leakage}) & V < V_{BR}(T)\\[4pt] \dfrac{V - V_{BR}(T)}{R_d} & V \ge V_{BR}(T)\end{cases}
$$

The dynamic resistance is:

$$
R_d \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26.0 - 18.7}{23.1} \approx 0.32\ \Omega
$$

Breakdown voltage rises with junction temperature:

$$
V_{BR}(T) = V_{BR,25}\,[1 + 0.001\,(T_j - 25)]
$$

**Pulse-power derating.** For pulses other than 10/1000 µs, the peak power capability scales approximately as

$$
P_{pk}(t_p) \approx 600\,\text{W}\cdot\sqrt{1000\,\mu s / t_p}
$$

The equivalent energy capability for one 10/1000 µs pulse is about 600 W × 1.44 ms ≈ 0.85 J. Treat this as the single-shot ceiling.

---

## 2. Bus Node Equation During Regen

The DC-link node sees:

$$
C_{bus}\frac{dV_{bus}}{dt} = i_{regen}(t) - i_{batt}(t) - i_{TVS}(V_{bus}) - i_{load}
$$

The battery path behaves as:

$$
V_{bus} = V_{batt} + i_{batt}R_{batt} + L_w\frac{di_{batt}}{dt}
$$

The regen current comes from the motor:

$$
i_{regen} \approx \frac{P_{regen}}{V_{bus}}, \qquad P_{regen} \approx K_e\,\omega\, I_{ph} - I_{ph}^2 R_{ph}
$$

During switching events and deadtime, the phase current freewheels through the body diodes into the bus. This produces fast edges with high di/dt superimposed on the average regen current.

---

## 3. Case A: Inductive Spike With the Battery Connected

This is the transient the TVS is designed for. The wiring inductance discharges into the clamp against the bus voltage V_s. The clamp energy is:

$$
E_{TVS} = \tfrac12 L I^2 \cdot \frac{V_C}{V_C - V_s}, \qquad t_{clamp} = \frac{L\,I}{V_C - V_s}
$$

**Worked example:** L = 500 nH, I = 40 A, V_s = 16.8 V.

- Clamp voltage: V_C ≈ 18.7 + 0.32 × 40 ≈ 31.5 V at peak, roughly 25 V on average.
- Energy: E ≈ 0.4 mJ × 25/(25 − 16.8) ≈ **1.2 mJ**.
- Duration: t ≈ (0.5 µH × 40 A) / 8.2 V ≈ **2.4 µs**.
- Peak power: about 1.2 kW, against a capability of roughly 12 kW at a ~2–3 µs pulse width.

**Verdict: safe by a large margin.**

The bulk capacitor matters more than the TVS here. Dumping 1.2 mJ into 470 µF raises the bus by only

$$
\Delta V \approx \frac{E}{C\,V} \approx 0.15\ \text{V}
$$

So the TVS only conducts if the capacitor ESR/ESL or the PCB loop inductance allows a local overshoot. In practice, the TVS is clamping the edge spike that the capacitor's ESL cannot catch. **Place it within a few millimetres of the half-bridge, not at the battery connector.**

---

## 4. Case B: Battery Disconnected or High-Impedance During Regen

This is the dangerous case. It occurs when the BMS opens, a connector bounces, or a full battery refuses charge current. With i_batt = 0, all regen power goes into the capacitor and then into the TVS:

$$
V_{bus,ss} \approx V_{BR}(T) + R_d \frac{P_{regen}}{V_{bus}}
$$

**Worked example:** P_regen = 200 W.

- Clamp current: about 200 W / 21 V ≈ 9.5 A.
- Clamp voltage: V ≈ 18.7 + 3.0 ≈ **21.7 V**, rising as the junction heats.
- Survival time: about 0.85 J / 200 W ≈ **4 ms**, then thermal runaway and a short-circuit failure.

**Verdict: the TVS cannot absorb sustained regen.** It will fail short, typically within milliseconds. That may then blow the fuse or damage the battery path