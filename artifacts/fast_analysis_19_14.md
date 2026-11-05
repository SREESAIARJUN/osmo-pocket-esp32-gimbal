# SMBJ16A Clamp Response to Motor Back-EMF Spikes During ESC Regenerative Braking

"Cycle 19" isn't a standard term, so I've treated it as the 19th event in a sequence of repeated braking events. That makes thermal accumulation part of the problem. The motor, bus, and wiring parameters below are placeholders. Replace them with your values.

---

## 1. Device Model (SMBJ16A, Unidirectional)

**Datasheet parameters (typical, Littelfuse/Vishay)**

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage at 1 mA | V_BR | 17.8 V min, ~18.7 V nominal, 19.7 V max |
| Clamping voltage at I_PP | V_C | 26.0 V |
| Peak pulse current (10/1000 µs) | I_PP | 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Breakdown temperature coefficient | α_T | ≈ +0.1 %/°C (≈ +19 mV/°C) |

**Piecewise-linear I–V model**

$$
I_{TVS}(V)=
\begin{cases}
I_R \;(\le 1\,\mu A) & V < V_{BR}\\[4pt]
\dfrac{V - V_{BR}(T_j)}{R_d} & V \ge V_{BR}
\end{cases}
$$

The dynamic resistance follows from the datasheet clamp point:

$$
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26.0-18.7}{23.1}\approx 0.32\ \Omega
$$

Breakdown voltage rises with junction temperature:

$$
V_{BR}(T_j)=V_{BR,25}\,[1+\alpha_T (T_j-25)]
$$

**Compatibility warning.** A 4S LiPo charges to 16.8 V, which exceeds V_RWM = 16 V. Leakage will rise, though it is still below V_BR(min). On 4S the SMBJ16A is marginal. An SMBJ18A or SMBJ20A is the safer choice. On 3S (12.6 V max) it is fine.

---

## 2. Circuit Model During Regenerative Braking

```
 Battery ──R_w──L_w──┬──────┬────────┬──── ESC 3φ bridge ── Motor (L_ph, R_ph, E=K_e·ω)
                     │      │        │
                     C_bus  TVS     (I_regen injected by bridge)
                     │      │        │
                    GND ────┴────────┘
```

**Bus-node equation**

$$
C_{bus}\frac{dV_{bus}}{dt}= I_{regen}(t) - I_{TVS}(V_{bus}) - I_{batt}(t)
$$

$$
L_w\frac{dI_{batt}}{dt}= V_{bus} - V_{batt} - R_w I_{batt}
$$

**Regenerative current source.** Under active braking with complementary PWM at duty D:

$$
I_{regen}\approx \frac{K_e\,\omega - D\,V_{bus}}{R_{ph}}\cdot D \quad(\text{averaged, during boost-mode braking})
$$

---

## 3. Two Distinct Transient Classes

### Class A: Commutation / interruption spike (short, energy-limited)

This occurs when phase current is abruptly forced into the bus. Typical causes are dead-time, fault shutdown, or desync. The phase inductance dumps its stored energy:

$$
E_L=\tfrac12 L_{ph} I_{ph}^2
$$

The bus capacitor absorbs this energy first. Its peak bus rise, ignoring the TVS, is:

$$
\Delta V_{bus}\approx I_{ph}\sqrt{\frac{L_{ph}}{C_{bus}}}
$$

The TVS conducts only the portion of the rise that exceeds V_BR. The energy diverted to it is approximately:

$$
E_{TVS}\approx \tfrac12 L_{ph}I_{ph}^2 - \tfrac12 C_{bus}\left(V_{BR}^2 - V_{bus,0}^2\right)
$$

This holds when the result is positive. Otherwise the capacitor handles the event alone.

When the TVS dominates (small C_bus), its clamp duration is:

$$
t_{clamp}\approx \frac{L_{ph}\,I_{ph}}{V_{C}-V_{bus,0}}
$$

The peak clamp voltage is:

$$
V_{clamp,pk}=V_{BR}(T_j)+R_d\,I_{TVS,pk}
$$

### Class B: Sustained regen overvoltage (long, power-limited)

This occurs when the battery cannot sink current, for example when the BMS opens, a connector bounces, the battery is full, or the leads are long. In that case I_batt ≈ 0, and the TVS becomes the only sink:

$$
V_{clamp}=V_{BR}+R_d\,I_{regen},\qquad P_{TVS}=V_{clamp}\,I_{regen}
$$

---

## 4. Worked Example (Assumed Values)

Assumptions: L_ph = 20 µH, I_ph = 40 A, C_bus = 470 µF, V_bus,0 = 16.8 V (4S full), and a regen current of 10 A in the sustained case.

**Class A result**

- Stored energy: E_L = ½ · 20 µH · 40² = **16 mJ**
- Capacitor-only rise: ΔV = 40 · √(20/470) ≈ **8.2 V**, which would reach ≈ 25 V
- Capacitor energy to reach V_BR = 18.7 V: ½ · 470 µF · (18.7² − 16.8²) ≈ **15.8 mJ**
- Excess energy to the TVS: **≈ 0.2 mJ**

This is trivial for the TVS. The bus capacitor does the real work. A failure would be capacitor-driven, for example from high ESR or degraded electrolytics.

**Removing the capacitor** (e.g., a cracked cap or 10 µF ceramics only) changes the picture. Nearly all 16 mJ goes to the TVS, and I_TVS,pk can approach 40 A. That is 1.7× the rated I_PP, giving V_clamp ≈ 18.7 + 0.32 · 40 ≈ **31.5 V**. The clamp lasts about 20 µH · 40 / (31.5 − 16.8) ≈ 54 µs.

To check survival, scale the power rating to the actual pulse width. For an SMB package, roughly P_PP(t) ≈ 600 W · (1 ms / t)^0.5:

- At 54 µs, the rating is ≈ 2.6 kW.
- The actual peak is ≈ 31.5 · 40 ≈ 1.26 kW, with a triangular profile.

The device survives a single event. However, the 31.5 V clamp may exceed your MOSFET or gate-driver V_DS(max) if they are 30 V parts.

**Class B result (10 A sustained)**

- V_clamp ≈ 18.7 + 3.2 = **21.9 V**
- P ≈ **219 W, continuous**