# TVS Clamp Response: SMBJ16A Under ESC Regenerative-Braking Back-EMF

**Scope and assumption.** "Cycle 4" is not a standard term, so I treat it as the **4th consecutive hard-braking event in a repetitive test profile**. That reading matters because the thermal state carries over from earlier cycles. If Cycle 4 means something else in your test plan, the electrical formulation still holds and only the thermal initial conditions change.

---

## 1. Device Model (SMBJ16A, unidirectional)

| Parameter | Value |
|---|---|
| V_RWM (standoff) | 16.0 V |
| V_BR at 1 mA | 17.8 V min / 19.7 V max |
| V_C at I_PP | 26.0 V at 23.1 A (10/1000 µs) |
| P_PP | 600 W (10/1000 µs, T_J0 = 25 °C) |
| V_BR tempco | ≈ +0.1 %/°C |

**Piecewise-linear clamp model.** Using worst-case V_BR = 19.7 V:

$$
I_{TVS}(V) =
\begin{cases}
I_R \;(\le 1\,\mu A) & V < V_{BR} \\
\dfrac{V - V_{BR}(T_J)}{R_d} & V \ge V_{BR}
\end{cases}
\qquad
R_d \approx \frac{26.0 - 19.7}{23.1} \approx 0.27\,\Omega
$$

The breakdown voltage rises with junction temperature:

$$
V_{BR}(T_J) = V_{BR,25}\,[1 + 0.001\,(T_J - 25)]
$$

---

## 2. Spike Sources During Regenerative Braking

There are three mechanisms, each with very different energy levels.

**(a) Switching-node / stray-inductance spike** (at every MOSFET turn-off)

$$
V_{spike} = L_{stray}\frac{di}{dt}, \qquad E_a = \tfrac12 L_{stray} I_{ph}^2
$$

Example: L_stray = 20 nH, a 30 A edge in 50 ns.
- V_spike ≈ 12 V
- E_a ≈ 9 µJ per edge

**(b) Battery-lead inductance spike** (regen current reverses into the pack through its wiring)

Example: L_lead ≈ 0.5 µH, I = 20 A.
- E_b ≈ 100 µJ

**(c) Regen dump with the battery unable to accept charge** (BMS open, connector bounce, or full pack)

The motor keeps pumping current into the bus capacitor C, then into the TVS. This is the dangerous case.

---

## 3. Governing Equations

**Bus node:**

$$
C\,\frac{dV_{bus}}{dt} = I_{regen}(t) - I_{TVS}(V_{bus}) - I_{batt}(t)
$$

The regen current comes from motor back-EMF minus winding drop, set by the braking duty:

$$
I_{regen} \approx \frac{k_e\,\omega - I_{ph}R_{ph}}{V_{bus}}\, I_{ph}
$$

**Phase 1: charging to breakdown** (worst case, I_batt = 0)

$$
t_1 = \frac{C\,(V_{BR} - V_0)}{I_{regen}}
$$

**Phase 2: clamp**

$$
V_{bus}(t) = V_{BR} + R_d I_{regen}\left(1 - e^{-t/R_dC}\right), \qquad \tau = R_d C
$$

$$
V_{bus,ss} = V_{BR} + R_d\, I_{regen}
$$

**Energy from an inductive dump against a source V_s:**

$$
E_{TVS} = \tfrac12 L I_0^2 \cdot \frac{V_C}{V_C - V_s}
$$

**Sustained dump power:**

$$
P_{TVS} = V_{bus,ss}\, I_{regen}
$$

---

## 4. Thermal Formulation (multi-cycle, Cycle 4)

Junction temperature after the n-th pulse, by superposition with transient thermal impedance Z_th(t):

$$
T_J(n) = T_A + \sum_{k=1}^{n} P_k\left[Z_{th}(t_n - t_k + t_p) - Z_{th}(t_n - t_k)\right]
$$

**Pulse-length derating** (roughly √t, typical for silicon TVS):

$$
P_{PP}(t_p) \approx 600\,\text{W}\cdot\sqrt{\frac{1\,\text{ms}}{t_p}}
$$

**Temperature derating** (linear to zero at 150 °C):

$$
P_{PP}(T_{J0}) = P_{PP,25}\cdot\frac{150 - T_{J0}}{125}
$$

**Cycle 4 rating:** use T_J0 equal to the junction temperature left over from cycles 1–3. Both P_PP and V_BR shift accordingly.

---

## 5. Worked Example

**Inputs:**
- C = 470 µF
- V_0 = 16.8 V (4S pack, full charge)
- I_regen = 20 A
- BMS opens during Cycle 4

**Phase 1 (charging):**
- t_1 = 470µ·(19.7 − 16.8)/20 ≈ 68 µs

**Phase 2 (clamp):**
- τ = 0.27 × 470µ ≈ 127 µs
- V_ss ≈ 19.7 + 5.4 ≈ 25.1 V
- P ≈ 25.1 × 20 ≈ 500 W, continuous

**Survival check:**
- Allowable at 10 ms: ≈ 600·√(1/10) ≈ 190 W, before any thermal derating.
- If cycles 1–3 left T_J0 ≈ 60 °C, multiply by 0.72, giving ≈ 135 W.
- **Result: failure (typically a short) within about 1–2 ms** of a sustained dump.

**Spikes (a) and (b):** these are µJ-level events. Peak current is only a few amps and V_C stays at about 20–21 V. These pass easily, even when repeated at PWM rates, provided the average power stays under about 1–3 W (P_avg = E·f_sw).

---

## 6. Conclusions and Design Flags

1. **Standoff margin issue.** A 4S pack at 16.8 V exceeds V_RWM = 16 V and approaches V_BR,min = 17.8 V.
   - Leakage and heating rise, especially hot, though V_BR does increase with temperature.
   - Use **SMBJ18A–20A** for 4S, or confirm the system is 3S (12.6 V max).
2. **The SMBJ16A is a spike clamp, not a regen sink.**
   - It handles switching and lead-inductance transients well (V_C ≈ 20–22 V).
   - It cannot absorb a sustained regen dump.
3. **Check MOSFET headroom.** Clamp voltage (up to ≈ 25–26 V) plus layout overshoot must stay below the MOSFET V_DS rating. 30 V parts are marginal; prefer 40 V.
4. **Handle the dump case with other mechanisms:**
   - Firmware bus-overvoltage regen limiting (fold back braking torque above about 18 V).
   - A brake chopper / active clamp (MOSFET plus power resistor) set below V_BR.
   - Bulk capacitance sized so that ½C(V_max² − V_0²) ≥ regen energy over the firmware response time.
5. **Validate Cycle 4 specifically.**
   - Measure V_bus and TVS case temperature across consecutive brake events.
   - Use T_J0 from cycle 3 in the derating; repetitive braking is where marginal designs fail.

If you share the motor k_e, peak RPM, phase current, bus capacitance, battery configuration, and the actual Cycle 4 profile timing, I can turn this into specific numbers and a pass/fail margin.