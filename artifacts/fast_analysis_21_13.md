# SMBJ16A Clamp Response to Back-EMF Spikes During ESC Regenerative Braking

"Cycle 21" is treated here as one braking event in a repeated test sequence. All numerical inputs below are **assumed example values**. Replace them with your measured L, I, C and timing.

---

## 1. Device Model (SMBJ16A, unidirectional, 25 °C)

| Parameter | Symbol | Value |
|---|---|---|
| Stand-off voltage | V_RWM | 16.0 V |
| Breakdown @ 1 mA | V_BR | 17.8 – 19.7 V (≈18.7 V typ) |
| Clamp @ I_PP | V_C | 26.0 V @ 23.1 A (10/1000 µs) |
| Peak pulse power | P_PPM | 600 W (10/1000 µs) |
| Steady-state dissipation | P_D | ~5 W (lead/pad-limited) |

**Piecewise-linear clamp model:**

$$
V_{TVS}(i) =
\begin{cases}
\approx 0 \text{ (leakage only)}, & V < V_{BR} \\
V_{BR}(T_j) + R_d\, i, & i > 0
\end{cases}
$$

$$
R_d \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26.0 - 18.7}{23.1} \approx 0.32\ \Omega
$$

**Thermal drift of breakdown voltage:**

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_T (T_j - 25)], \qquad \alpha_T \approx 9\times10^{-4}\ /^\circ C \text{ (approximate; check datasheet)}
$$

**Applicability check:** V_RWM = 16 V suits 3S Li-ion (12.6 V max) or a 12 V bus. It is **not valid for 4S (16.8 V)**, because the diode would conduct continuously.

---

## 2. System Model During Regenerative Braking

```
 Motor (L_ph, R_ph, e = k_e·ω) → Inverter (body diodes / synchronous rectification)
       → DC bus: C_bus ∥ TVS ∥ [L_lead + R_bat → Battery]
```

Two distinct stress regimes must be evaluated separately.

### Regime A: Fast inductive spike (ns – µs)

This occurs when current in the parasitic loop L_σ (battery leads plus PCB) is interrupted, e.g., at a PWM edge or when the battery/BMS opens.

Once the TVS conducts:

$$
L_\sigma \frac{di}{dt} = -\big(V_{BR} - V_s + R_d\, i\big), \quad a \equiv V_{BR} - V_s
$$

The current decays as:

$$
i(t) = \left(I_0 + \frac{a}{R_d}\right)e^{-t/\tau} - \frac{a}{R_d}, \qquad \tau = \frac{L_\sigma}{R_d}
$$

The resulting peak voltage, clamp duration and absorbed energy are:

$$
V_{peak} = V_{BR} + R_d I_0
$$

$$
t_{clamp} = \tau \ln\!\left(1 + \frac{I_0 R_d}{a}\right)
$$

$$
E_{TVS} \approx \tfrac12 L_\sigma I_0^2 \cdot \frac{V_C}{V_C - V_s}
$$

### Regime B: Bus pump-up (ms – s)

This occurs when regenerative current I_reg = P_brake / V_bus cannot return to the battery (BMS open, battery full, or high R_bat).

1. **Charging phase.** C_bus charges until the TVS conducts:
   $$
   t_{rise} = \frac{C_{bus}(V_{BR} - V_s)}{I_{reg}}
   $$
2. **Clamping phase.** The TVS then carries essentially the entire regenerative current for the braking time t_b:
   $$
   P_{TVS} = (V_{BR} + R_d I_{reg})\, I_{reg}, \qquad E_{TVS} = P_{TVS}\, t_b
   $$
3. **Junction temperature.**
   $$
   T_j(t) = T_A + P_{TVS}\, Z_{th}(t)
   $$
   As T_j rises, V_BR rises with it, which acts as positive feedback on dissipation.

**Pass criterion:** P_TVS must not exceed P_PPM(t_b), read from the datasheet "peak pulse power vs. pulse width" curve. That curve falls roughly as t^(−0.4…−0.5): about 600 W at 1 ms, and only on the order of 100 W at 100 ms.

---

## 3. Worked Example, Cycle 21 (assumed values)

Inputs: V_s = 12.6 V (3S, full), so a = 6.1 V.

### Regime A: spike at battery-lead current interruption

Inputs: L_σ = 200 nH, I_0 = 30 A.

| Quantity | Calculation | Result |
|---|---|---|
| τ | 200 nH / 0.32 Ω | 625 ns |
| t_clamp | 625 ns · ln(1 + 9.6/6.1) | ≈ 0.59 µs |
| V_peak | 18.7 + 0.32·30 | ≈ 28.3 V |
| E_TVS | | ≈ 0.1 mJ |

**Result: pass.** Energy and pulse width are far below the rating.

**Caution:** 28.3 V must remain below the MOSFET V_DS rating with margin. 30 V FETs are marginal; 40 V FETs are preferred. Package and trace inductance add further overshoot of L_trace·di/dt on