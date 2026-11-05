# SMBJ16A Clamp Response Under ESC Regenerative-Braking Back-EMF Transients

**A note on "Cycle 22":** I don't know what your test cycle specifies. Below, the duty profile is left as parameters (I₀, ω, braking duration, battery state). Substitute your Cycle 22 values. The worked numbers are illustrative only.

---

## 1. Device Model (SMBJ16A, unidirectional)

| Parameter | Symbol | Value (datasheet typ.) |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage @ 1 mA | V_BR | 17.8 – 19.7 V (≈18.75 V nominal) |
| Max clamp voltage @ I_PP | V_C | 26.0 V @ 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| V_BR temperature coefficient | α_T | ≈ +0.09 %/°C |

**Piecewise-linear I–V model:**

$$
i_{TVS}(V,T_j)=
\begin{cases}
I_R \;(\lesssim 1\,\mu A), & V < V_{BR}(T_j)\\[4pt]
\dfrac{V - V_{BR}(T_j)}{R_d}, & V \ge V_{BR}(T_j)
\end{cases}
$$

with

$$
R_d \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26.0 - 18.75}{23.1} \approx 0.31\ \Omega
$$

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_T (T_j - 25^\circ C)]
$$

---

## 2. System Model During Regenerative Braking

**State variables:**
- Phase current i_ph
- Bus voltage V_bus
- Battery current i_b

**Parameters:**
- L_ph: line-line inductance
- e = K_e·ω: back-EMF
- C_bus: bus capacitance
- L_w, R_b: battery wiring inductance and battery resistance
- V_oc: battery open-circuit voltage

**Bus node:**

$$
C_{bus}\frac{dV_{bus}}{dt} = i_{regen}(t) - i_b - i_{TVS}(V_{bus}) - i_{load}
$$

**Battery branch:**

$$
L_w \frac{di_b}{dt} = V_{bus} - V_{oc} - R_b\, i_b
$$

**Worst case:** the battery is disconnected (BMS open, connector bounce, fuse). Then i_b = 0, and all regenerated charge goes into C_bus and the TVS.

There are two distinct transient classes. Treat them separately because their timescales differ by about 10³–10⁶.

---

## 3. Case A: Inductive Spike at Switch-Off (µs scale)

This occurs during braking when the ESC turns all FETs off (desync, fault trip, or a PWM edge with dead time). The phase current then freewheels through the body diodes into the bus:

$$
L_{ph}\frac{di_{ph}}{dt} = -(V_C - e), \qquad V_C \approx V_{BR} + R_d\, i_{ph}
$$

**Decay time:**

$$
t_d \approx \frac{L_{ph} I_0}{V_C - e}
$$

**Energy absorbed by the TVS** (assuming C_bus is small or already charged):

$$
E_{TVS} \approx \tfrac{1}{2} L_{ph} I_0^2 \cdot \frac{V_C}{V_C - e}
$$

The factor V_C/(V_C − e) matters. As back-EMF approaches the clamp voltage, the energy dumped into the TVS diverges, because the motor keeps supplying energy while the inductance discharges.

**Peak power:**

$$
P_{pk} = V_C(I_0)\cdot I_0
$$

**Illustrative example:** L_ph = 20 µH, I₀ = 30 A, e = 10 V.

| Step | Result |
|---|---|
| Clamp voltage | V_C = 18.75 + 0.31·30 ≈ 28.1 V |
| Decay time | t_d = (20 µ·30)/(18.1) ≈ 33 µs |
| Energy | E ≈ 9 mJ × 1.55 ≈ 14 mJ |
| Peak power | P_pk ≈ 843 W |

**Rating check:** for short pulses, allowable peak power scales roughly as t^(−1/2) (adiabatic):

$$
P_{allow}(33\,\mu s) \approx 600\sqrt{1000/33} \approx 3.3\ \text{kW}
$$

Result: **survives with about 4× margin.** Verify against the datasheet derating curve rather than relying on the rule of thumb.

---

## 4. Case B: Sustained Regenerative Pumping (ms to s scale)

With the battery disconnected, regenerated power is:

$$
P_{regen} = \eta\, T_{brake}\,\omega
$$

1. **Capacitor charging phase.** C_bus charges until V_bus reaches V_BR. The time to reach clamp is:

$$
t_{clamp} = \frac{C_{bus}\,(V_{BR}^2 - V_0^2)}{2P_{regen}}
$$

2. **Clamping phase.** Once V_bus reaches V_BR, the TVS absorbs essentially all of P_regen for the rest of the braking event:

$$
E_{TVS} \approx P_{regen}\,(t_{brake} - t_{clamp})
$$

**Example:** 50 W regen for 100 ms gives **5 J**. This is about 6× the energy of a 600 W 10/1000 µs pulse (≈0.87 J).

**Result: the SMBJ16A fails, typically short-circuit.** A TVS is a transient suppressor, not a regen energy dump.

---

## 5. Thermal Coupling (Self-