# TVS Clamp Response: SMBJ16A Under ESC Regenerative-Braking Back-EMF (Cycle 5)

The previous cycles are not available to me, so this assumes a typical small-drone or robotics ESC: a 3S–4S LiPo bus, 20–30 A phase currents, about 20 kHz PWM, and the SMBJ16A placed across the DC bus near the bridge. Substitute your own parameters where they differ.

---

## 1. Device Model (SMBJ16A, datasheet values)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage (at 1 mA) | V_BR | 17.8 – 19.7 V (≈18.7 V nominal) |
| Max clamping voltage | V_C | 26.0 V at I_PP = 23.1 A |
| Peak pulse power | P_PP | 600 W (10/1000 µs) |
| Steady-state dissipation | P_D | ≈5 W (lead/pad temperature-limited) |
| Breakdown temperature coefficient | α_T | ≈ +0.1 %/°C |

**Piecewise-linear clamp model:**

$$
I_{TVS}(V)=\begin{cases}\approx I_R\ (\mu A) & V<V_{BR}\\[4pt] \dfrac{V-V_{BR}(T_j)}{R_d} & V\ge V_{BR}\end{cases}
$$

$$
R_d=\frac{V_C-V_{BR}}{I_{PP}}=\frac{26.0-18.7}{23.1}\approx 0.32\ \Omega
$$

$$
V_{BR}(T_j)=V_{BR,25}\,[1+\alpha_T(T_j-25)]
$$

---

## 2. Circuit Formulation During Regenerative Braking

During braking, the ESC commutates the phase current back through the high-side switches or body diodes onto the DC bus. The bus node equations are:

**Bus node:**
$$
C_{bus}\frac{dV_{bus}}{dt}=i_{regen}(t)-i_{batt}(t)-I_{TVS}(V_{bus})
$$

**Battery branch (lead inductance L_w, resistance R_b):**
$$
L_w\frac{di_{batt}}{dt}=V_{bus}-V_{batt}-R_b\,i_{batt}
$$

**Regen source (phase inductance L_ph, back-EMF e = k_e·ω):**
$$
L_{ph}\frac{di_{ph}}{dt}=e(\omega)-V_{bus}\cdot d_{eff}-R_{ph}i_{ph}
$$

The TVS conducts only when the bus voltage exceeds V_BR. This happens when:
- the battery cannot accept current fast enough because L_w blocks a fast di/dt,
- the battery is disconnected or a fuse opens during braking, or
- the bus capacitance is too small to absorb the commutation energy.

---

## 3. Single-Spike Clamp Response

Assume an inductor with initial current I_0 dumps its current into the clamp against the bus voltage. The current decays approximately linearly:

$$
t_{clamp}\approx\frac{L\,I_0}{\bar V_C-V_{bus}}
\qquad
\bar V_C\approx V_{BR}+R_d\frac{I_0}{2}
$$

$$
E_{TVS}\approx \bar V_C\cdot\frac{I_0}{2}\cdot t_{clamp}
\qquad
P_{pk}=(V_{BR}+R_dI_0)\,I_0
$$

E_TVS is larger than ½LI₀² because the bus source also supplies energy during the clamp interval.

### Case A: Battery lead inductance (the realistic TVS job)

Inputs: L_w = 1 µH, I_0 = 30 A, V_bus = 16.8 V.

- Clamp voltage: V_C,pk ≈ 18.7 + 0.32·30 ≈ **28.3 V**. This exceeds the 26 V datasheet V_C because 30 A is above I_PP.
- Clamp duration: t_clamp ≈ 1e-6·30 / (23.5 − 16.8) ≈ **4.5 µs**.
- Absorbed energy: E_TVS ≈ 23.5 · 15 · 4.5 µs ≈ **1.6 mJ**.
- Peak power: P_pk ≈ 850 W for about 4.5 µs.

This is well within the pulse capability. The surge curve scales roughly as t^(-1/2), giving about 9 kW at 5 µs.

### Case B: Phase inductance with the battery effectively absent (fault case)

Inputs: L_ph = 20 µH, I_0 = 30 A.

- Clamp duration: t_clamp ≈ 94 µs.
- Absorbed energy: E_TVS ≈ 33 mJ.
- Peak power: P_pk ≈ 850 W.

The single-pulse rating at 100 µs is roughly 1.9 kW, so one event is survivable.

---

## 4. Repetitive / Thermal Constraint (the real limit in Cycle 5)

Rapid braking produces a spike every commutation or PWM edge:

$$
P_{avg}=E_{TVS}\cdot f_{event}
\qquad
T_j=T_A+P_{avg}\,R_{\theta JA}
$$

- **Case A at 20 kHz:** 1.6 mJ × 20 kHz ≈ **32 W**. This far exceeds the ≈5 W limit, causing thermal runaway. Because V_BR rises with temperature, the clamp voltage also drifts upward until the device fails short.
- **Case B:** catastrophic within milliseconds.

**Conclusion:** the SMBJ16A can only absorb *sporadic* edge transients, well under 1 mJ at low repetition. Bulk regenerative energy must go to the battery, the bus capacitance, or an active brake chopper.

The bus capacitance needed to cap the rise at ΔV:

$$
C_{bus}\ge\frac{L_w I_0^2}{(V_{BR}-V_{bus})^2-0}\;\to\;\frac{1\mu H\cdot 900}{(18.7-16.8)^2}\approx 250\ \mu F\ \text{(low-ESR)}
$$

With this capacitance, the TVS ideally never conducts in normal braking.

---

## 5. Design Flags

1. **Voltage rating mismatch.** A fully charged 4S pack (16.8 V) exceeds V_RWM (16 V). V_BR(min) is 17.8 V, which leaves only about 1 V of margin. Expect leakage and frequent conduction during regen. For 4S, use an **SMBJ20A or SMBJ22A**, and check that V_C(max) stays below the MOSFET V_DS rating with ≥20 % margin.
2. **MOSFET protection margin.** V_C,pk of 28–30 V requires MOSFETs rated at least 40 V. 30 V parts are marginal.
3. **Layout parasitics.** Loop inductance in the TVS path adds L·di/dt overshoot. Place the device directly at the bridge with less than 5 nH of loop inductance.
4. **Firmware mitigation.** Limit braking di/dt and regen current when V_bus exceeds about 17.5 V, or enable a brake resistor or chopper.

---

## 6. Summary Response Equation

$$
V_{clamp}(t)=V_{BR,25}[1+\alpha_T(T_j(t)-25)]+R_d\,I_{TVS}(t),
\qquad
T_j(t)=T_A+Z_{\theta}(t)\ast P_{TVS}