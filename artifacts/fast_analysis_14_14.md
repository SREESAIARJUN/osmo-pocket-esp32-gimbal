# SMBJ16A Clamp Response Under Regenerative-Braking Back-EMF Transients

I don't have your "Cycle 14" test data (I0, inductance, bus voltage, capture waveforms). The formulation below is fully parametric. A worked example uses representative values, which you should replace with your measurements.

---

## 1. Device Model (SMBJ16A, unidirectional)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage @ 1 mA | V_BR | 17.8 V min / ~18.7 V typ / 19.7 V max |
| Max clamp voltage @ I_PP | V_C | 26.0 V @ 23.1 A |
| Peak pulse power | P_PPM | 600 W (10/1000 µs) |
| Steady-state dissipation | P_D | ~5 W (heavily layout-dependent) |

Use a piecewise-linear clamp model:

$$
V_{TVS}(i) = V_{BR} + R_d\, i, \qquad R_d \approx \frac{V_C - V_{BR}}{I_{PP}} \approx \frac{26.0-18.7}{23.1} \approx 0.30\ \Omega
$$

Add the parasitic inductive overshoot from the TVS loop:

$$
V_{peak,node} = V_{TVS}(i) + L_{loop}\frac{di}{dt}
$$

Here L_loop is typically 1–5 nH (pads, vias, traces). This term often dominates on fast edges.

**Note on 4S packs.** A 4S lithium pack at full charge is 16.8 V, which exceeds V_RWM. Leakage will rise, and near V_BR(min) the diode may partially conduct during normal operation. SMBJ16A is comfortable for 3S (12.6 V) and marginal for 4S.

---

## 2. Circuit Formulation of the Braking Spike

During aggressive regen, current I0 commutates through the MOSFET body diodes onto the DC bus. A spike appears when the bus cannot accept that current fast enough. Typical causes are battery lead inductance, a BMS open, a connector bounce, or the low-side PWM switching off.

The equivalent circuit is an inductance L (battery leads plus effective motor phase inductance) carrying I0, discharging into C_bus in parallel with the TVS, against source voltage V_s.

### Stage A: Bulk capacitor absorbs first

The TVS stays off unless:

$$
\tfrac12 L I_0^2 > \tfrac12 C_{bus}\left(V_{BR}^2 - V_s^2\right)
$$

Enough bulk capacitance can make the TVS irrelevant for single spikes. Check this first.

### Stage B: TVS conduction

$$
L\frac{di}{dt} = -\left(V_{BR} + R_d i - V_s\right)
$$

Define:

$$
\tau = \frac{L}{R_d}, \qquad I_x = \frac{V_{BR}-V_s}{R_d}
$$

The current, clamp duration, and peak clamp voltage are:

$$
i(t) = (I_0 + I_x)\,e^{-t/\tau} - I_x
$$

$$
t_c = \tau \ln\!\left(\frac{I_0+I_x}{I_x}\right)
$$

$$
V_{clamp,pk} = V_{BR} + R_d I_0
$$

Energy absorbed by the TVS is the inductor energy plus the energy the source pushes through during clamping:

$$
E_{TVS} = \tfrac12 L I_0^2 + V_s\!\int_0^{t_c}\! i\,dt
$$

$$
\int_0^{t_c} i\,dt = (I_0+I_x)\,\tau\left(1-e^{-t_c/\tau}\right) - I_x t_c
$$

A useful approximation for a constant clamp voltage is:

$$
E_{TVS} \approx \tfrac12 L I_0^2 \cdot \frac{V_c}{V_c - V_s}
$$

As V_c approaches V_s, this blows up. That is why a clamp too close to the bus voltage overheats.

---

## 3. Pulse-Rating Check

The 600 W rating applies to a 10/1000 µs pulse, equivalent to roughly 0.85 J. For other pulse widths, use the adiabatic scaling (or the datasheet P_PPM vs t_p curve):

$$
E_{max}(t_p) \approx 0.85\,\text{J}\cdot\sqrt{\frac{t_p}{1.44\ \text{ms}}}
\qquad
P_{max}(t_p) \approx 600\,\text{W}\cdot\sqrt{\frac{1\ \text{ms}}{t_p}}
$$

For repetitive spikes (every PWM or commutation event), check average power. It must stay well below the ~5 W steady-state rating, derated for ambient temperature and copper area:

$$
P_{avg} = E_{TVS}\cdot f_{rep} \ll P_D
$$

---

## 4. Worked Example (placeholder values; replace with Cycle 14 data)

Assumed: V_s = 12.6 V (3S), I0 = 30 A, L = 10 µH, V_BR = 18.7 V, R_d = 0.30 Ω.

| Quantity | Result |
|---|---|
| Inductor energy, ½LI0² | 4.5 mJ |
| I_x | (18.7 − 12.6)/0.30 = 20.3 A |
| τ | 33 µs |
| t_c | 33 µs × ln(50.3/20.3) ≈ 30 µs |
| V_clamp,pk | 18.7 + 0.30·30 ≈ 27.7 V (above the 26 V datasheet V_C because I0 > I_PP) |
| ∫i dt | ≈ 381 µC, so source contributes ≈ 4.8 mJ |
| E_TVS | ≈ 9.3 mJ |
| P_peak | ≈ 830 W |
| Allowed at 30 µs | ≈ 3.4 kW / ≈ 0.12 J |

**Single-event margin is about 13× on energy, so it passes.**

Add the loop overshoot. With 3 nH at di/dt ≈ 1 A/ns, you get +3 V, so the node reaches about 30.7 V. That leaves almost no margin on 30 V MOSFETs. Use FETs rated 40 V or higher, or reduce the loop.

At a 20 kH