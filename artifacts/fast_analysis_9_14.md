# SMBJ16A Clamp Response Under ESC Regenerative-Braking Back-EMF

> **Note on "Cycle 9":** I don't have your prior cycle data (bus voltage, currents, layout parasitics, or results from cycles 1–8). The model below is fully parameterized. The example numbers assume a **4S LiPo bus (16.8 V full charge)**, a 24 kHz PWM ESC, and ~40 A phase current. Replace them with your measured values.

---

## 1. Device Parameters (SMBJ16A, unidirectional)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage @ 1 mA | V_BR | 17.8 V min / 18.75 V nom / 19.7 V max |
| Clamping voltage @ I_PP | V_C | 26.0 V @ 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Temperature coefficient of V_BR | α_V | ≈ +0.1 %/°C (~19 mV/°C) |

**Piecewise-linear clamp model:**

$$
I_{TVS}(V)=\begin{cases} I_R \;(\mu A) & V < V_{BR} \\[4pt] \dfrac{V - V_{BR}(T_j)}{R_d} & V \ge V_{BR}\end{cases}
\qquad
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26 - 18.75}{23.1} \approx 0.31\ \Omega
$$

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_V (T_j - 25)]
$$

**⚠ First flag:** A full 4S pack is 16.8 V, which exceeds V_RWM = 16 V. The bus stays below V_BR(min), so the diode won't conduct heavily. However, leakage rises sharply in this region, and there is almost no margin before regen pumping starts the clamp. In effect the TVS clamps on nearly every braking event.

---

## 2. Spike Sources During Regenerative Braking

During braking, the ESC commutates phase current back toward the DC bus. Two different mechanisms reach the TVS, and they behave very differently.

### (A) Commutation spike: loop inductance at FET turn-off

Each switching edge interrupts current through the stray loop inductance L_σ (FETs, bus caps, traces):

$$
V_{spike} = V_{bus} + L_\sigma \frac{di}{dt}
$$

Once V reaches V_BR, the TVS takes the current, and the inductor discharges against the clamp voltage. Neglecting the small R_d drop during decay:

$$
t_{clamp} = \frac{L_\sigma I_0}{V_{CL} - V_{bus}}, \qquad
E_{TVS} = \tfrac12 L_\sigma I_0^2 \cdot \frac{V_{CL}}{V_{CL} - V_{bus}}
$$

The second factor accounts for the bus continuing to drive current through the clamp while the inductor discharges.

**Peak clamp voltage, including the TVS's own lead and trace inductance L_TVS:**

$$
V_{peak} \approx V_{BR}(T_j) + I_0 R_d + L_{TVS}\frac{di}{dt}
$$

**Example:** L_σ = 20 nH, I₀ = 40 A, V_bus = 16.8 V
- V_peak ≈ 18.75 + 40·0.31 ≈ **31 V**, before adding L_TVS·di/dt
- t_clamp ≈ 20 nH·40 A / (31 − 16.8) V ≈ 56 ns
- E_TVS per event ≈ 16 µJ · 2.2 ≈ **35 µJ**

A single event is trivial for the part. The repetition rate is the problem:

$$
P_{avg} = E_{TVS}\cdot f_{edges} \approx 35\,\mu J \times 24\,kHz \approx \mathbf{0.85\ W}\ (\text{per edge type})
$$

An SMB package with realistic PCB copper handles roughly 0.5–1 W continuously. **The diode will thermally run away if it clamps every PWM edge.**

**⚠ Second flag:** A 31 V+ clamp exceeds the rating of the 30 V MOSFETs commonly used on 4S ESCs. The TVS does not protect them in this regime.

### (B) Regen bus pumping: energy returned with no sink

If the battery cannot absorb the regen current, the returned power charges C_bus until the TVS conducts. This happens with BMS cutoff, connector bounce, a fuse opening, or a high-ESR or cold pack.

$$
C_{bus}\frac{dV}{dt} = I_{regen}(t) - I_{TVS}(V) - I_{batt}(V),\qquad I_{regen} \approx \frac{\eta\,T_{brake}\,\omega}{V_{bus}}
$$

At steady clamp:

$$
P_{TVS} \approx V_{CL}\, I_{regen}
$$

**Example:** 10 A regen for 200 ms with the battery disconnected
- V_CL ≈ 18.75 + 10·0.31 ≈ 22 V → P ≈ **220 W** for 200 ms, so **E ≈ 44 J**

The device's pulse rating derates with duration, roughly P_PP(t) ≈ 600 W·(1 ms/t)^0.5 ≈ **42 W at 200 ms**. That is 5× overstress, and **the device fails (usually shorted).**

With the battery connected, the bus rise is only I_regen·ESR (e.g., 10 A × 50 mΩ = 0.5 V), and the TVS stays off. Case (B) is therefore a fault-mode design case, but it must be survived.

---

## 3. Thermal Coupling (critical for repeated cycles)

$$
T_j(t) = T_a + \sum_k P_k\, Z_{th,JA}(t - t_k)
$$

As T_j rises, V_BR rises (≈ +19 mV/°C). For example, at +100 °C, V_BR ≈ 20.6 V, so the clamp voltage drifts upward over consecutive braking cycles.

- This feedback is self-limiting for the diode, because a higher clamp means less current at a given bus.
- It works against the MOSFETs, whose margin shrinks every cycle.

If Cycle 9 is the ninth repeated braking event in a test sequence, compare the measured V_peak trend against this drift. A monotonic rise in clamp voltage across cycles is the signature of junction heating.

---

## 4. Simulation Formulation (SPICE / ODE)

```
State: V_bus, i_Lσ, T_j
dV_bus/dt = [