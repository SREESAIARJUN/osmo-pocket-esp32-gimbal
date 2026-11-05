# SMBJ16A Clamp Response to Regenerative-Braking Back-EMF Spikes

"Cycle 30" is read here as the 30th repetitive braking event in a test sequence. The single-event model is developed first and then extended to 30 cycles. All numerical values are illustrative and should be replaced with your measured L, I₀, bus capacitance and braking profile.

---

## 1. Device Parameters (SMBJ16A, typical datasheet)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage @ 1 mA | V_BR | 17.8 V min / 19.7 V max (≈18.75 V nominal) |
| Max clamping voltage | V_C | 26.0 V @ I_PP = 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Steady-state power (on PCB) | P_M(AV) | ≈5 W |
| V_BR temperature coefficient | α_V | ≈ +0.1 %/°C |

**Dynamic (clamp) resistance:**

$$R_d \approx \frac{V_C - V_{BR,max}}{I_{PP}} = \frac{26.0 - 19.7}{23.1} \approx 0.27\ \Omega \quad (\text{use } 0.27\text{–}0.31\ \Omega)$$

**Piecewise-linear clamp model:**

$$
I_{TVS}(V)=\begin{cases}\approx 0 \ (\text{leakage } I_R \le 1\,\mu A @ V_{RWM}) & V < V_{BR}(T_j)\\[4pt]
\dfrac{V - V_{BR}(T_j)}{R_d} & V \ge V_{BR}(T_j)\end{cases}
$$

$$V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_V (T_j - 25)]$$

---

## 2. Where the Spike Comes From During Regen

The MOSFET body diodes return motor phase current to the DC bus, so the TVS on the bus sees two distinct phenomena.

**(a) Fast inductive spike.** Stray/wiring inductance L_s (battery leads, connectors) is interrupted at each switching or commutation edge. The duration is short (µs), and the energy is set by ½·L_s·I².

**(b) Bus pump-up.** Regen current charges the bus capacitance faster than the battery (or a BMS-disconnected pack) can absorb it. The duration is long (ms to s), and this is the dangerous case.

**Bus node equation:**

$$C_{bus}\frac{dV_{bus}}{dt} = I_{regen}(t) - I_{batt}(t) - I_{TVS}(V_{bus})$$

with

$$I_{regen} \approx \frac{k_e\,\omega\,|D_{brake}|}{V_{bus}}\cdot I_{phase}\eta \quad\text{(power balance: } P_{regen}=k_e\omega I_{ph}\eta\text{)}$$

---

## 3. Case A: Single Inductive Spike (Energy Dump into Clamp)

**Loop equation** (inductor current I₀ forced into the clamp, no net driving source):

$$L\frac{di}{dt} = -\left(V_{BR} + R_d\,i\right)$$

**Current solution:**

$$i(t) = \left(I_0 + \frac{V_{BR}}{R_d}\right)e^{-t/\tau} - \frac{V_{BR}}{R_d},\qquad \tau = \frac{L}{R_d}$$

**Clamp duration** (time until i = 0):

$$t_c = \tau\,\ln\!\left(1 + \frac{R_d I_0}{V_{BR}}\right)\;\approx\; \frac{L I_0}{V_{BR}}\ \ (\text{when } R_dI_0 \ll V_{BR})$$

**Peak clamp voltage:**

$$V_{pk} = V_{BR}(T_j) + R_d I_0$$

**Energy absorbed:**

$$E_{TVS} = \int_0^{t_c} (V_{BR} + R_d i)\,i\,dt = \tfrac12 L I_0^2$$

If the loop includes a source V_s (e.g., the battery) aiding the current, replace V_BR with (V_BR − V_s). The energy then becomes:

$$E_{TVS} = \tfrac12 L I_0^2 \cdot \frac{V_{BR}}{V_{BR}-V_s}\quad(\text{approx.})$$

### Worked Example

Assumed values: L = 2 µH (lead/wiring), I₀ = 20 A, V_BR = 18.9 V (warm), R_d = 0.3 Ω.

| Quantity | Result |
|---|---|
| τ = L/R_d | 6.7 µs |
| t_c | 6.7 µs × ln(1 + 6/18.9) ≈ **1.8 µs** |
| V_pk | 18.9 + 6.0 = **24.9 V** (< 26 V rating) |
| E_TVS | ½·2µ·400 = **0.4 mJ** |
| P_pk | ≈ 24.9 × 20 ≈ **500 W** |

**Pulse-width derating** (approximate datasheet curve):

$$P_{PP}(t_p) \approx 600\,\text{W}\cdot\sqrt{\frac{1000\,\mu s}{t_p}}$$

At t_p ≈ 2 µs, P_PP is several kW, so a single spike is well within rating.

---

## 4. Case B: Sustained Regen Overvoltage (Critical Case)

If the battery cannot sink the regen current (BMS open, high pack impedance, full charge), the TVS becomes the brake resistor.

**Steady clamp current:**

$$I_{TVS,ss} = I_{regen} - I_{batt}$$

**Bus voltage:**

$$V_{bus,ss} = V_{BR} + R_d\, I_{TV