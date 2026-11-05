# SMBJ16A Clamp Response to Motor Back-EMF During ESC Regenerative Braking (Cycle 1)

## 0. Scope and Assumptions

| Item | Cycle-1 value |
|---|---|
| Bus | 3S Li-ion/LiPo, V_bus,max = 12.6 V (nominal 11.1 V) |
| ESC | 3-phase MOSFET bridge, complementary PWM, synchronous regen braking |
| Phase current at brake onset | I₀ = 30 A |
| Bulk capacitance | C_bus = 470 µF (low-ESR electrolytic), ESR ≈ 20 mΩ, plus 4 × 10 µF ceramic at the bridge |
| Power-loop stray inductance | L_s ≈ 20 nH (FET, decoupling cap, PCB loop) |
| Battery lead inductance | L_w ≈ 1 µH (≈ 2 × 50 cm leads) |
| TVS placement | Across the DC bus at the bridge. TVS lead and trace inductance L_pkg ≈ 3–5 nH. |

> ⚠ **Rail check:** The SMBJ16A is **not suitable for 4S packs**. A full 4S pack sits at 16.8 V, which exceeds V_RWM = 16 V. Leakage then rises steeply, and the TVS begins conducting near V_BR,min = 17.8 V on any charging overshoot. This analysis therefore assumes a 3S bus.

---

## 1. SMBJ16A Device Model

Datasheet parameters (Littelfuse/Vishay class, 600 W at 10/1000 µs):

| Parameter | Value |
|---|---|
| V_RWM | 16.0 V |
| V_BR at 1 mA | 17.8 V min / 18.8 V typ / 19.7 V max |
| V_C at I_PP | 26.0 V at 23.1 A (10/1000 µs) |
| P_PP | 600 W (10/1000 µs) |
| Steady-state P_D | ≈ 5 W (with adequate copper) |
| Temperature coefficient of V_BR | ≈ +0.1 %/°C |

**Piecewise-linear clamp model:**

$$
I_{TVS}(V)=\begin{cases}0 \;(\text{leakage }<1\,\mu A), & V < V_{BR}\\[4pt] \dfrac{V-V_{BR}}{R_d}, & V\ge V_{BR}\end{cases}
$$

Dynamic resistance, taken from the worst-case corner:

$$
R_d=\frac{V_C - V_{BR,max}}{I_{PP}}=\frac{26.0-19.7}{23.1}\approx 0.27\ \Omega
$$

(Using typical V_BR = 18.8 V gives R_d ≈ 0.31 Ω. Use 0.3 Ω below.)

**Thermal correction.** V_BR rises with junction temperature:

$$
V_{BR}(T_j)=V_{BR,25}\,[1+\alpha (T_j-25)], \qquad \alpha\approx 1\times10^{-3}/^\circ C
$$

A junction at 125 °C therefore adds about 1.9 V to the clamp.

**Effective clamp seen by the FETs.** Package and trace inductance add a term on fast edges:

$$
V_{peak}=V_{BR}+R_d\,i+L_{pkg}\,\frac{di}{dt}
$$

---

## 2. Spike Mechanisms During Regenerative Braking

During braking, the low-side (or high-side) FETs short the phase so current builds from the back-EMF, E_bemf = k_e·ω. When the FETs open, the winding current freewheels through the body diodes or the opposite FETs **into the bus**. Three distinct overvoltage sources result:

| # | Mechanism | Time scale | Energy | Who should absorb it |
|---|---|---|---|---|
| A | Turn-off edge × L_s (commutation spike) | 10–100 ns | µJ | Ceramic decoupling / RC snubber |
| B | Bus pump-up: regen current charges C_bus (battery present) | 10 µs–ms | mJ | Battery + C_bus |
| C | Bus pump-up with battery disconnected (connector bounce, BMS open, fuse) | ms–s | J (unbounded) | Brake chopper / firmware — **not** the TVS |

---

## 3. Formulation: Inductive Discharge Into the Clamp

When an inductance L carrying I₀ discharges into the TVS against the bus:

$$
L\frac{di}{dt} = -(V_{BR}-V_{bus}) - R_d\, i
$$

Define the overdrive a = V_BR − V_bus and the time constant τ = L/R_d. Then:

$$
i(t)=\left(I_0+\frac{a}{R_d}\right)e^{-t/\tau}-\frac{a}{R_d}
$$

**Clamp duration** (time until i = 0):

$$
t_c=\tau\,\ln\!\left(1+\frac{I_0R_d}{a}\right)
$$

**Peak clamp voltage:**

$$
V_{pk}