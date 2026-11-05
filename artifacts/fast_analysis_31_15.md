# SMBJ16A Clamp Response to Motor Back-EMF Spikes During Regenerative Braking

**Note on "Cycle 31":** I don't have your test data or prior cycle context. Below is a general formulation with placeholder values. Replace the ⟨bracketed⟩ parameters with your Cycle 31 measurements, such as phase current at brake onset, bus voltage, and loop inductance.

---

## 1. Device Model (SMBJ16A, typical datasheet values)

| Parameter | Value |
|---|---|
| V_RWM (standoff) | 16.0 V |
| V_BR (@1 mA) | 17.8 V min / 19.7 V max |
| V_C @ I_PP = 23.1 A (10/1000 µs) | 26.0 V |
| P_PP (10/1000 µs) | 600 W |
| Single-pulse energy (≈ P_PP · 1.44·t_½) | ~0.6–0.9 J |

**Piecewise-linear clamp model:**

$$
V_{TVS}(i) = \begin{cases} \approx V_{bus}, & i \approx 0 \ (\text{leakage only}) \\ V_{BR} + R_{dyn}\, i, & i > 0 \end{cases}
$$

$$
R_{dyn} \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26.0 - 19.7}{23.1} \approx 0.27\ \Omega \quad (\text{worst case, } V_{BR,max})
$$

V_BR rises with temperature at about +0.1%/°C. Use the hot value for worst-case clamp voltage.

---

## 2. Two Distinct Spike Mechanisms

You must separate these. Mechanism B is the one that typically kills an SMBJ.

### A. Commutation/stray-inductance overshoot (ns–µs, every PWM edge)

The energy stored in the loop inductance L_σ (DC-link loop, FET, and cap ESL) is dumped at each turn-off:

$$
E_\sigma = \tfrac{1}{2} L_\sigma I_{ph}^2
$$

The overshoot without the clamp would be ΔV ≈ L_σ·di/dt. With the TVS, the clamp starts conducting once V_bus + ΔV > V_BR.

**Average TVS dissipation:**

$$
P_{avg,A} \approx f_{PWM}\cdot \tfrac{1}{2} L_\sigma I_{ph}^2 \cdot \frac{V_{BR}}{V_{BR}-V_{bus}}
$$

The last factor accounts for the bus supply feeding energy into the clamp during the reset interval.

*Example:* L_σ = 30 nH, I = 30 A, f = 24 kHz, V_bus = 14.8 V → E_σ = 13.5 µJ.
P_avg ≈ 0.32 W × (19.7/4.9) ≈ **1.3 W**. This is marginal for an SMB package continuously (~5 W rating only with T_L = 75 °C on heavy copper). Snubbers and layout should handle most of this energy, not the TVS.

### B. Bus pumping during regenerative braking (ms–s)

During regen, current flows back into the DC link. If the source cannot sink it, the bus capacitor charges up. Examples:
- a series reverse-protection diode
- a bench PSU
- a BMS in charge-cutoff
- a long, high-inductance battery lead

**Capacitor charging until clamp:**

$$
\frac{dV_{bus}}{dt} = \frac{I_{regen}}{C_{bus}}, \qquad I_{regen} \approx \frac{(E_{bemf} - I R_s)\, I_{ph}}{V_{bus}}\cdot \eta
$$

**Once V_bus reaches V_BR, the TVS carries the full regen current:**

$$
V_{clamp} = V_{BR} + R_{dyn} I_{regen}, \quad P_{TVS} = V_{clamp}\, I_{regen}, \quad E_{TVS} = \int_0^{t_{brake}} P_{TVS}\,dt
$$

*Example:* I_regen = 20 A, t_brake = 50 ms
→ V_clamp ≈ 19.7 + 5.4 = **25.1 V**, P ≈ 500 W, **E ≈ 25 J**.

That is about 30× the device's single-pulse capability. The SMBJ16A **will fail short**.

---

## 3. Inductive Current Decay Into the Clamp

Consider a phase winding (L_ph, R_ph, back-EMF E) freewheeling into the clamped bus:

$$
L_{ph}\frac{di}{dt} = -\big(V_{BR} + R_{dyn}i\big) + E - R_{ph} i
$$

**Solution** (with R_t = R_dyn + R_ph and τ = L_ph / R_t):

$$
i(t) = \left(I_0 + \frac{V_{BR}-E}{R_t}\right)e^{-t/\tau} - \frac{V_{BR}-E}{R_t}
$$

**Decay time to zero** (valid while E < V_BR):

$$
t_0 = \tau \ln\!\left(1 + \frac{I_0 R_t}{V_{BR}-E}\right)
$$

**Clamp energy:**

$$
E_{TVS} = \int_0^{t_0}\big(V_{BR}+R_{dyn}i\big)\, i\, dt \;\approx\; \tfrac{1}{2}L_{ph}I_0^2 \cdot \frac{V_{BR}}{V_{BR}-E} \quad (\text{for } R_t \to 0)
$$

**Critical observation:** as E approaches V_BR (high speed, hard braking), the denominator collapses. In that regime the clamp energy is dominated by sustained back-EMF, not stored inductive energy. If E > V_BR (overspeed), current never decays and the TVS sees continuous conduction.

---

## 4. Pass/Fail Checks for Cycle 31

1. **Peak pulse:** Use the measured clamp current waveform. Compute its equivalent rectangular width. Compare P_peak against the datasheet derating curve (P_PP vs. t_p) at your T_J.
2. **Single-event energy:** E_TVS < ~0.5 J. Leave margin; derate about 50% for repetitive pulses.
3. **Thermal accumulation** across cycles 1…31:
   $$T_J(n) = T_A + \sum_k E_k\, Z_{th}(t_n - t_k)$$
   Check T_J < 150 °C, and preferably < 125 °C.
4. **Downstream rating:** V_clamp,max ≈ 26–28 V. FETs should be ≥ 40 V and bus caps ≥ 35 V.
5. **Standoff margin:** A 4S LiPo at full charge is 16.8 V, which exceeds V_RWM = 16 V. Leakage rises sharply near the 17.8 V V_BR minimum. Verify standby leakage and heating at full charge, or move to SMBJ18A/20A.

---

## 5. Recommendations if Mechanism B Is Present

- Add an **active brake chopper**: comparator at ~V_bus + 2 V driving a MOSFET into a power resistor.
- Alternatively, add **firmware regen current limiting** using bus-voltage feedback, which is standard in VESC/AM32-class ESCs.
- Keep the TVS strictly as **fast-edge protection** (mechanism A).
- Increase C_bus with low-ESR parts to reduce dV/dt.
- If you keep a TVS for