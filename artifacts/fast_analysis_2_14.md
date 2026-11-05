# SMBJ16A Clamp Response to Back-EMF Spikes During Regenerative Braking

"Cycle 2" isn't defined here, so I've assumed it means one braking event with sustained regeneration, as opposed to a single isolated turn-off spike. The analysis is parametric. Substitute your own L, I₀, E, and repetition rate.

---

## 1. Device parameters (SMBJ16A, unidirectional, 600 W SMB)

| Parameter | Value |
|---|---|
| V_RWM (standoff) | 16.0 V |
| V_BR (breakdown) @ 1 mA | 17.8 V min / ~18.7 V typ / 19.7 V max |
| V_C (clamp) @ I_PP | 26.0 V @ 23.1 A (10/1000 µs) |
| P_PP | 600 W (10/1000 µs) |
| Steady-state P_D | ~5 W (lead temperature dependent) |

**Linearised clamp model:**

$$V_{TVS}(i) \approx V_{BR} + R_d\, i,\qquad R_d \approx \frac{V_C - V_{BR}}{I_{PP}} \approx \frac{26 - 18.7}{23.1} \approx 0.32\ \Omega$$

At worst case (V_BR max), R_d is about 0.27 Ω and V_C stays at roughly 26 V at rated current.

> ⚠️ **Rail compatibility check:** a 4S LiPo at full charge is 16.8 V, which is above V_RWM. At that point leakage rises and margin to V_BR(min) is only about 1 V. SMBJ16A is appropriate for 3S (12.6 V) or a regulated ~12–15 V bus. For 4S, use SMBJ18A/20A.

---

## 2. Circuit model during regenerative braking

When the ESC turns off its low-side (or all) FETs, the phase current I₀ freewheels through the body diodes into the DC bus:

```
 Motor phase (L_ph, R_ph, back-EMF E) ──► body diodes (2·V_f) ──► DC bus
                                                              ├── C_bus (ESR)
                                                              ├── Battery (via L_wire)
                                                              └── TVS (SMBJ16A)
```

**Governing equation for phase current decay while the TVS conducts:**

$$L\frac{di}{dt} = -\big[V_{BR} + R_d\, i + 2V_f + R_{ph}\, i - E\big]$$

The key regen nuance is that the back-EMF E *opposes* the decay. Current only decays if V_clamp + 2V_f > E.

---

## 3. Response phases

**Phase A: bus charge-up (TVS off)**

C_bus absorbs the current until V_bus reaches V_BR:

$$t_A \approx \frac{C_{bus}(V_{BR} - V_0)}{I_0}$$

If C_bus is large and the battery is connected, the spike may never reach V_BR, and the TVS stays idle. This is the desired case.

**Phase B: clamp conduction**

Solving the decay equation (neglecting R_ph):

$$i(t) = \left(I_0 + \frac{V_{BR}+2V_f-E}{R_d}\right)e^{-t/\tau} - \frac{V_{BR}+2V_f-E}{R_d},\qquad \tau = \frac{L}{R_d}$$

Since τ is much larger than the clamp time, the decay is nearly linear:

$$t_c \approx \frac{L\, I_0}{V_{C,avg} + 2V_f - E}$$

**Energy absorbed by the TVS**, including the energy supplied by the back-EMF during decay:

$$E_{TVS} \approx \tfrac{1}{2} L I_0^2 \cdot \frac{V_{C,avg}}{V_{C,avg} + 2V_f - E}$$

**Peak power:**

$$P_{pk} = (V_{BR} + R_d I_0)\, I_0$$

**Layout overshoot** from parasitic inductance in series with the TVS:

$$V_{bus,pk} \approx V_{BR} + R_d I_0 + L_{trace}\frac{di}{dt}$$

The silicon responds in under 1 ns, but 5 nH of trace at 1 A/ns adds about 5 V. Place the TVS directly across the FET bridge bulk capacitor.

---

## 4. Worked example

**Assumptions:** L = 20 µH (phase-phase), I₀ = 30 A, E = 10 V, V_f = 0.7 V, V_BR = 18.7 V.

| Quantity | Result |
|---|---|
| V_clamp at peak | 18.7 + 0.32×30 ≈ **28.3 V** (above datasheet V_C because I₀ > I_PP) |
| V_C,avg | ≈ 23.5 V |
| t_c | 600 µJ·s/A ÷ (23.5 + 1.4 − 10) ≈ **40 µs** |
| E_TVS | ½·20µ·900 × 23.5/14.9 ≈ 9 mJ × 1.58 ≈ **14 mJ** |
| P_pk | ≈ **850 W** (triangular pulse, ~20 µs rectangular equivalent) |

**Single-pulse verdict:** From the P_PP vs. t_p derating curve, the SMB rating at about 20 µs is roughly 3–4 kW. A single event is therefore **within rating**, with about a 4× margin.

---

## 5. Repetitive regen: the real failure mode

During braking, the event repeats every PWM or commutation cycle:

$$P_{avg} = E_{TVS}\cdot f_{rep}$$

| f_rep | P_avg | Status vs. ~5 W |
|---|---|---|
| 100 Hz | 1.4 W | OK |
| 1 kHz | 14 W | ❌ thermal runaway |
| 20 kHz (PWM-synchronous) | 280 W | ❌ instant failure |

**Worst case: battery disconnected or BMS open.** Regen current has nowhere to go but the TVS:

$$P_{TVS} \approx V_C \cdot I_{regen} \approx 26\text{ V} \times 10\text{ A} = 260\text{ W (continuous)}$$

The SMBJ fails short within milliseconds. That is fail-safe for overvoltage, but it blows a fuse or the FETs.

---

## 6. Conclusions and design rules

1. **The TVS is a transient clamp, not a regen sink.** Bus capacitance plus the battery must absorb steady regen energy. Design so V_bus stays below V_BR(min) = 17.8 V in normal braking:
   $$C_{bus} \ge \frac{L I_0^2}{V_{BR,min}^2 - V_0^2}$$
2. **Limit ESC regen current** in firmware (brake current limit, bus-voltage foldback starting at about V_RWM).
3. **For disconnected-battery cases**, add an active brake chopper (FET plus power resistor triggered at ~17 V) or an OVP disconnect. Alternatively, upsize the clamp (SMCJ/5.0SMDJ, or paralleled devices with matched V_BR).
4. **Check the thermal budget:** E_TVS · f_rep < ~1–2 W with derating at elevated T_A.
5. **Verify on hardware:** scope V_bus and i_TVS (current probe) during a full-thrott