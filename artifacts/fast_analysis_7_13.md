# SMBJ16A Clamp Response Under Regenerative Back-EMF Transients

I don't have the specific data for your "Cycle 7" test (bus voltage, phase current, stray inductance, switching frequency). Below is a general model with placeholder values marked as **assumed**. Substitute your measured numbers.

---

## 1. Device Model (SMBJ16A, from datasheet)

| Parameter | Value |
|---|---|
| Standoff voltage V_RWM | 16.0 V |
| Breakdown V_BR @ 1 mA | 17.8 V min, ~18.7 V typ, 19.7 V max |
| Clamp V_C @ I_PP | 26.0 V @ 23.1 A (10/1000 µs) |
| Peak pulse power | 600 W (10/1000 µs) |
| Temperature coefficient α_T | ≈ +0.09 %/°C on V_BR |

**Piecewise-linear clamp model:**

$$
V_{TVS}(i,T_j)=
\begin{cases}
i \cdot R_{leak} & V < V_{BR}\\[4pt]
V_{BR}(T_j) + R_d\, i & V \ge V_{BR}
\end{cases}
$$

$$
R_d \approx \frac{V_C - V_{BR,max}}{I_{PP}} = \frac{26 - 19.7}{23.1} \approx 0.27\ \Omega
$$

$$
V_{BR}(T_j) = V_{BR,25}\,[1+\alpha_T (T_j - 25)]
$$

For example, at T_j = 85 °C, V_BR ≈ 18.7 × 1.054 ≈ 19.7 V.

---

## 2. Circuit Formulation During Regen

When the ESC low-side or high-side switches turn off during braking, phase current commutates through the body diodes onto the DC bus. The TVS sees the energy the bulk capacitors and battery cannot absorb.

**Governing equation (clamp-active interval):**

$$
L_{eq}\frac{di}{dt} = e_{bemf}(t) - V_{BR} - (R_d + R_s)\,i
$$

**Solution, assuming constant e_bemf over the short interval:**

$$
i(t) = I_\infty + (I_0 - I_\infty)\,e^{-t/\tau}, \qquad 
\tau = \frac{L_{eq}}{R_d+R_s}, \qquad
I_\infty = \frac{e_{bemf}-V_{BR}}{R_d+R_s}
$$

The sign of I_∞ separates two regimes:

- **e_bemf < V_BR (normal operation):** I_∞ < 0, so current decays to zero at a finite t_off. The result is a self-terminating spike.
- **e_bemf > V_BR (overspeed, or bus lost):** I_∞ > 0, so the TVS conducts continuously. This leads to thermal runaway.

**Energy absorbed per event** (approximately linear decay, bus at V_bus):

$$
E_{TVS} \approx \tfrac12 L I_0^2 \cdot \frac{\bar V_C}{\bar V_C - V_{bus}}, 
\qquad 
t_{clamp} \approx \frac{L I_0}{\bar V_C - V_{bus}}
$$

---

## 3. Worked Example (Assumed Cycle 7 Conditions)

**Assumed:** 3S bus (12.6 V max), peak phase current 40 A, stray loop inductance 80 nH, PWM 24 kHz, T_j = 25 °C.

### Case A: Switching-edge spikes (battery connected)

| Quantity | Result |
|---|---|
| Peak clamp voltage | 18.7 + 0.27 × 40 ≈ **29.5 V** |
| Average clamp voltage | ≈ 24 V |
| Clamp duration | 80 nH × 40 / (24 − 12.6) ≈ **0.28 µs** |
| Peak power | ≈ 1.2 kW (acceptable for sub-µs pulses) |
| Energy per event | 64 µJ × 24/11.4 ≈ **135 µJ** |
| Average power | 135 µJ × 24 kHz ≈ **3.2 W** ⚠️ |

**Interpretation:** No single pulse is a problem. However, the *repetitive* dissipation exceeds what an SMB package can sustain on typical PCB copper (~1–1.5 W). In practice, low-ESR ceramics placed tightly at the bridge should absorb most of this energy. If the TVS is actually carrying it, your capacitor placement or layout is inadequate.

### Case B: Battery disconnect during regen (BMS trip or connector bounce)

Assume regen current I_reg = 15 A:

$$
P_{TVS} = 15 \times (18.7 + 0.27\times15) \approx 340\ \text{W (continuous)}
$$

This falls inside the 600 W / 1 ms envelope for only about **1–2 ms** before T_j exceeds its limit. The TVS then typically fails short, crowbarring the bus. **A TVS cannot handle sustained regen energy.** A brake chopper, active regen limiting in firmware, or a bus-OV-triggered short-brake is required.

---

## 4. Design Flags

1. **Bus voltage compatibility:** If this is a **4S system (16.8 V full charge)**, the SMBJ16A's 16 V standoff is exceeded. You will see rising leakage, and at hot T_j near-conduction at full charge. Use an SMBJ18A or SMBJ20A instead.
2. **Downstream ratings:** Verify that 29–30 V peaks are within MOSFET V_DS and gate-driver ratings with margin.
3. **Thermal check:** Use the transient thermal impedance:

$$
\Delta T_j = P_{pk}\, Z_{th}(t_p) + P_{avg}\,R_{\theta JA}
$$

4. **Validation:** Measure TVS current directly (current probe on the TVS lead) during Cycle 7. Compare the measured E_TVS per edge against the model above to back-calculate the actual