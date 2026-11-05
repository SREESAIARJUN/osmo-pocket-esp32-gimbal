# TVS Clamp Response: SMBJ16A Under Regenerative-Braking Back-EMF

"Cycle 15" isn't a standard term I can resolve. I've treated it as one braking event in a test sequence and used representative parameters you can replace with your own.

---

## 1. Device Model (SMBJ16A, datasheet typical)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage (@ 1 mA) | V_BR | 17.8 V min / 19.7 V max (~18.75 V nominal) |
| Max clamping voltage | V_C | 26.0 V @ I_PP = 23.1 A |
| Peak pulse power | P_PP | 600 W (10/1000 µs, 25 °C) |
| Leakage @ V_RWM | I_R | ≤ 1 µA (typical) |

**Piecewise-linear clamp model:**

$$
I_{TVS}(V) =
\begin{cases}
\approx 0 & V < V_{BR} \\[4pt]
\dfrac{V - V_{BR}}{R_d} & V \ge V_{BR}
\end{cases}
\qquad
R_d \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26 - 18.75}{23.1} \approx 0.31\ \Omega
$$

**Pulse energy capability.** A 10/1000 µs waveform has a decay time constant of τ = 1000 µs / ln 2 ≈ 1.44 ms, so:

$$
E_{max} \approx P_{PP}\,\tau \approx 600 \times 1.44\times10^{-3} \approx 0.87\ \text{J} \quad (\text{at } 25^\circ\text{C})
$$

Derate this linearly toward 0 at about 150–175 °C junction temperature (see the datasheet derating curve). Steady-state dissipation for an SMB package is only about **5 W**.

---

## 2. DC-Bus Node Equation During Regen

$$
C_{bus}\frac{dV_{bus}}{dt} = I_{regen}(t) - I_{batt}(t) - I_{TVS}(V_{bus}) - I_{load}(t)
$$

where:
- I_regen is the motor phase current rectified through the MOSFET body diodes or synchronous FETs onto the bus.
- I_batt = (V_bus − V_batt)/R_batt, with series lead inductance L_w.

There are two distinct stress mechanisms:

**(a) Commutation spike.** This is inductive energy dumped when the bridge turns off:
$$
E_L = \tfrac{1}{2} L_{ph} I_{ph}^2, \qquad V_{spike,local} \approx L_{stray}\frac{di}{dt}
$$

**(b) Sustained regen.** This is a continuous current that must be absorbed somewhere:
$$
P_{regen} \approx k_t\,\omega\,I_{brake}\;\eta
$$

---

## 3. Worked Example: Cycle 15 (assumed parameters)

**Assumptions:**
- 3S Li-ion bus: V₀ = 12.6 V
- C_bus = 470 µF (low-ESR)
- L_ph = 50 µH, I_ph = 20 A at turn-off
- Battery lead L_w = 1 µH, R_batt = 30 mΩ
- Braking current 10 A

### Case A: Battery connected, commutation spike

$$
E_L = \tfrac{1}{2}(50\,\mu H)(20)^2 = 10\ \text{mJ}
$$

If this energy goes into the bus capacitor:

$$
V_{peak} = \sqrt{V_0^2 + \frac{2E_L}{C}} = \sqrt{158.8 + 42.6} \approx 14.2\ \text{V}
$$

**Result:** The peak stays below V_BR, so the TVS does not conduct. The capacitor and battery absorb the event.

The local stray inductance spike (e.g., 20 nH × 20 A / 50 ns ≈ 8 V) can briefly reach the clamp region at the FET drain. That is a nanosecond, µJ-level event, well within the TVS rating. Note that TVS lead and trace inductance limits its effectiveness at this timescale.

### Case B: Battery disconnected (BMS cutoff or connector bounce) during braking

Bus voltage ramp:
$$
\frac{dV}{dt} = \frac{I_{regen}}{C} = \frac{10}{470\,\mu F} \approx 21\ \text{V/ms}
$$

Time to reach breakdown:
$$
t_{BR} = \frac{(17.8 - 12.6)\,C}{I} \approx 0.25\ \text{ms}
$$

Once V_BR is reached, the TVS carries the full regen current:
$$
V_{clamp} = V_{BR} + I R_d \approx 18.75 + 10(0.31) \approx 21.9\ \text{V}
$$
$$
P_{TVS} \approx 21.9 \times 10 \approx 220\ \text{W (continuous)}
$$

Time to exhaust the pulse energy budget:
$$
t_{fail} \approx \frac{E_{max}}{P} \approx \frac{0.87}{220} \approx 4\ \text{ms}
$$

**Result:** The TVS fails within milliseconds, usually as a short circuit. A short then collapses the bus and can destroy the ESC FETs or drive them into a fault.

---

## 4. Key Conclusions

1. **The SMBJ16A clamps transients, not regen energy.** It handles µJ–mJ commutation spikes well. It cannot absorb sustained regenerative power beyond a few milliseconds.

2. **Check the standoff margin against your battery.**
   - 3S (12.6 V max): adequate margin.
   - 4S (16.8 V max): exceeds the 16 V V_RWM. Leakage rises sharply, and the part may partially conduct continuously and heat up. Use an SMBJ20A or SMBJ22A instead, then re-verify that the clamp voltage stays below the FET V_DS rating.

3. **Clamp coordination requirement:**
$$
V_{batt,max} < V_{RWM} \quad\text{and}\quad V_{C}(I_{peak}) < 0.8\,V_{DS(max)}
$$

4. **Mitigations for sustained regen:**
   - Active brake chopper (resistor and FET) triggered around 17 V.
   - ESC firmware that limits regen current when V_bus exceeds a threshold.
   - Increased C_bus.
   - Ensuring the BMS cannot open the circuit during regen.
   - Placing the TVS directly at the bridge with minimal loop inductance.

---

## 5. Simulation Suggestion

In SPICE, model the TVS as an ideal diode in series with V_BR and R_d, or use the vendor model (Littelfuse/Vishay). Drive the bus with a current source representing I_regen(t) and a switched battery branch. Log ∫I_TVS·V dt and compare it against the derated E_max for each cycle.

If you share your real parameters (cell count, C_bus, motor L, braking current, FET V_DS), I can redo the Cycle 15 numbers.