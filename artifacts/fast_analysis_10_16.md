# SMBJ16A Clamp Response to Motor Back‑EMF Spikes During ESC Regenerative Braking (Cycle 10)

I read "Cycle 10" as **the 10th consecutive braking event**. That makes the question about cumulative junction heating, not just a single pulse. All numbers below are representative. Replace them with your measured loop inductance, phase current, and timing.

---

## 1. Device Model (SMBJ16A, unidirectional, typical datasheet values)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage @ 1 mA | V_BR | 17.8 V min / 18.7 V nom / 19.7 V max |
| Clamp voltage @ I_PP | V_C | 26.0 V @ 23.1 A (10/1000 µs) |
| Peak pulse power | P_PP | 600 W (10/1000 µs, T_J = 25 °C) |
| Thermal resistance | R_θJA / R_θJL | ≈ 100 °C/W / ≈ 20 °C/W |
| V_BR temperature coefficient | α_T | ≈ +0.1 %/°C |

**Piecewise-linear clamp model:**

$$
V_{TVS}(i)=
\begin{cases}
V_{bus}, & i \approx I_R \;(\text{leakage}) \\
V_{BR}(T_J) + R_d\, i, & i > 1\text{ mA}
\end{cases}
$$

$$
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26.0-18.7}{23.1} \approx 0.32\ \Omega
$$

$$
V_{BR}(T_J) = V_{BR,25}\,[1+\alpha_T (T_J-25)]
$$

**Pulse-power derating:**
- **Pulse width.** For a rectangular pulse of width t_p, the thermally limited capability is approximately $P_{max}(t_p) \approx 600\ \text{W}\cdot(1\ \text{ms}/t_p)^{0.5}$.
- **Temperature.** Capability falls linearly to zero at T_J = 150 °C:

$$
k_T = \frac{150-T_J}{125}
$$

---

## 2. Spike Source: Commutation of Phase Current into Stray Inductance

During regenerative braking, the ESC low-side PWM builds phase current I₀. When the switch opens, I₀ freewheels through the high-side body diodes onto the DC bus. Any loop inductance L_s that the bulk capacitor does not cover forces the bus up until the TVS conducts.

**Circuit equation once clamped** (battery or bus held at V_bat):

$$
L_s \frac{di}{dt} = -\big(V_{BR} + R_d\, i - V_{bat}\big), \qquad \Delta V \equiv V_{BR}-V_{bat}
$$

**Solution:**

$$
i(t) = \Big(I_