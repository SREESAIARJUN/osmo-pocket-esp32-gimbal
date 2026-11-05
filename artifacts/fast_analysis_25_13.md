# SMBJ16A Clamp Response to Back-EMF Spikes During ESC Regenerative Braking

I've read "Cycle 25" as the 25th braking event in a repetitive braking test. If it refers to a specific standard or internal test spec, its pulse definition should replace the placeholders below.

---

## 1. Device Model (SMBJ16A, unidirectional, 600 W)

| Parameter | Symbol | Value |
|---|---|---|
| Standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage (at 1 mA) | V_BR | 17.8 V min / 18.75 V typ / 19.7 V max |
| Max clamp voltage (at I_PP) | V_C | 26.0 V at 23.1 A (10/1000 µs) |
| Peak pulse power | P_PP | 600 W (10/1000 µs, T_J0 = 25 °C) |
| Breakdown temperature coefficient | α_VBR | ≈ +0.1 %/°C (≈ +18 mV/°C) |
| Thermal resistance | R_θJL / R_θJA | ≈ 20 °C/W / ≈ 80–100 °C/W (PCB dependent) |

**Piecewise-linear clamp model:**

$$
I_{TVS}(V)=
\begin{cases}
I_R \;(\le 1\,\mu A) & V < V_{BR}(T_J)\\[4pt]
\dfrac{V - V_{BR}(T_J)}{R_d} & V \ge V_{BR}(T_J)
\end{cases}
$$

The dynamic resistance, worst case, is:

$$
R_d \approx \frac{V_C - V_{BR,min}}{I_{PP}} = \frac{26.0-17.8}{23.1}\approx 0.36\ \Omega
$$

The breakdown voltage drifts with junction temperature:

$$
V_{BR}(T_J)=V_{BR,25}\,[1+\alpha_{VBR}(T_J-25)]
$$

> **Flag:** A 4S Li-ion/LiPo pack charges to 16.8 V, which is above V_RWM. In that case the TVS conducts leakage, and possibly onset current, at full charge. SMBJ16A suits 12 V or 3S systems. For 4S, use SMBJ18A/20A, or verify leakage at 16.8 V.

---

## 2. Spike Source Mechanisms During Regen Braking

### (a) Commutation spike (fast, low energy)
When a MOSFET turns off, the commutation-loop stray inductance L_σ (bus caps → FETs) produces a spike:

$$
V_{spike} = V_{bus} + L_\sigma \frac{di}{dt},\qquad E_\sigma=\tfrac12 L_\sigma I_{ph}^2
$$

Example: L_σ = 30 nH, I_ph = 40 A gives E_σ = 24 µJ. This is negligible for the TVS. The bus capacitors and FET avalanche dominate here.

### (b) Bus pump-up (slow, high energy, the real threat)
During braking, the back-EMF E = K_e·ω drives current into the DC link through the body diodes or synchronous FETs. If the battery cannot absorb it (BMS open, connector bounce, long leads, or full charge), the bus node equation is:

$$
C_{bus}\frac{dV_{bus}}{dt} = I_{regen}(t) - I_{batt}(t) - I_{TVS}(V_{bus})
$$

The regen current is:

$$
I_{regen}\approx \frac{P_{regen}}{V_{bus}} = \frac{\eta\,(K_e\omega\, I_{ph,brake} - I_{ph}^2 R_{ph})}{V_{bus}}
$$

The battery path has its own impedance:

$$
L_{batt}\frac{dI_{batt}}{dt} = V_{bus} - V_{oc} - I_{batt}R_{batt}
$$

If the battery is disconnected (I_batt = 0), the bus voltage rises until it reaches V_BR. After that the TVS carries the regen current:

$$
V_{clamp}(t) = V_{BR}(T_J) + R_d\, I_{regen}(t)
$$

Energy dumped into the TVS per braking event:

$$
E_{TVS}=\int_{t_1}^{t_2} V_{clamp}(t)\,I_{TVS}(t)\,dt \;\approx\; \tfrac12 J(\omega_1^2-\omega_2^2)\eta - \text{(losses, cap charge)}
$$

The bound is the motor plus load kinetic energy, not the motor inductance. The phase-inductance term ½·L_ph·I_ph² is only the residual when the ESC stops switching:

$$
E_{L}=\tfrac12 L_{ph} I_{ph}^2 \quad(\text{e.g. } 20\,\mu H,\ 30\,A \Rightarrow 9\text{ mJ})
$$

---

## 3. Pulse Capability Check

The rated power scales approximately with pulse width:

$$
P_{PP}(t_p) \approx 600\,\text{W}\cdot\left(\frac{1\,\text{ms}}{t_p}\right)^{0.5}
$$

The energy rating also derates with starting temperature:

$$
E_{max}(T_{J0}) \approx E_{max,25}\cdot\frac{T_{J,max}-T_{J0}}{150-25},\qquad T_{J,max}=150\,°C
$$

**Example:** I_regen = 20 A for t_p = 5 ms with the battery disconnected.
- V_clamp ≈ 18.75 + 0.36·20 ≈ **26 V**
- P ≈ 520 W, so E ≈ **2.6 J**
- Allowed power at 5 ms ≈ 600/√5 ≈ **270 W**, so the device **fails**.

**Commutation residual only:** E ≈ 10 mJ, well within the rating, so the device **survives**.

---

## 4. Repetitive Stress: Cycle n = 25

Junction temperature after the n-th pulse, by superposition of the transient thermal impedance Z_th(t):

$$
T_J(n) = T_A + \sum_{k=1}^{n} P_k\left[Z_{th}\big((n-k)T + t_p\big) - Z_{th}\big((n-k)T\big)\right]
$$

For identical pulses at period T, the steady-state approximation is:

$$
T_J(25)\approx T_A + \bar P\,R_{\theta JA} + P_{pk}\,Z_{th}(t_p)\Big|_{\text{peak}},\qquad \bar P = E_{TVS}/T
$$

The clamp voltage then rises with the heated junction:

$$
V_{clamp}(25)=V_{BR,25}\big[1+\alpha_{VBR}(T_J(25)-25)\big]+R_d I_{regen}
$$

**Example:** E = 10 mJ per event, T = 2 s, T_A = 50 °C, R_θJA = 90 °C/W.
- Average power = 5 mW, so ΔT_avg ≈ 0.5 °C
- Per-pulse spike ≈ 600 W × ~0.02 °C/W ≈ 12 °C
- T_J(25) ≈ **63 °C**
- V_BR shifts by +0.24 V, so V_clamp ≈ **19.2 V + R_d·I**

The datasheet duty-cycle limit is ≤ 0.01 % (t_p/T). Exceeding it requires the cumulative T_J analysis above, not the single-pulse rating.

---

## 5. Design Conclusions

1. **The TVS is a spike clamp, not a regen dump.** It survives commutation and phase-L residuals (µJ–mJ). It cannot absorb sustained regen energy (J-level) when the battery path opens.
2. **Downstream parts must withstand about 26 V worst case,**