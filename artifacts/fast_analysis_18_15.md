# SMBJ16A Clamp Response to Motor Back-EMF Spikes During Regenerative Braking (Cycle 18)

"Cycle 18" is not a standard designation, so I treat it here as **the 18th consecutive regenerative-braking event in a repetitive test sequence**. That makes thermal accumulation the key question. All numbers marked *(assumed)* are placeholders. Substitute your measured values.

---

## 1. Device Parameters (SMBJ16A, unidirectional)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage @ 1 mA | V_BR | 17.8 V min / ~18.7 V nominal / 19.7 V max |
| Max clamping voltage | V_C | 26.0 V @ I_PP = 23.1 A |
| Peak pulse power | P_PP | 600 W (10/1000 µs) |
| Steady-state power | P_D | ~5 W (lead at 75 °C), far less on typical FR4 |
| Thermal resistance | R_θJA | ~100 °C/W (typical pad) |
| Temperature coefficient of V_BR | α_VBR | ~+0.1 %/°C (≈ +18 mV/°C) |

**Linearized clamp model**

$$
V_{TVS}(I) = V_{BR}(T_j) + R_d \cdot I, \qquad R_d \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26 - 18.7}{23.1} \approx 0.32\ \Omega
$$

$$
V_{BR}(T_j) = V_{BR,25}\left[1 + \alpha_{VBR}(T_j - 25)\right]
$$

---

## 2. Spike Source Model

During braking the ESC acts as a boost converter. Phase current I₀ is driven back onto the DC bus. A spike forms when the bus cannot absorb the current. Causes include a battery disconnect, BMS cutoff, high battery ESR/lead inductance, or an undersized bulk capacitor.

**Inductive energy per event**

$$
E_L = \tfrac{1}{2} L_{eff} I_0^2
$$

Here L_eff is the phase inductance plus the inductance of the battery leads.

**Clamp-phase current decay.** The current falls roughly linearly while the TVS holds the bus:

$$
\frac{dI}{dt} = -\frac{V_{TVS} - V_{bus}}{L_{eff}}, \qquad t_{clamp} = \frac{L_{eff} I_0}{V_{TVS} - V_{bus}}
$$

**Energy absorbed by the TVS.** The TVS absorbs more than E_L, because the source keeps delivering energy during the decay:

$$
E_{TVS} = \int_0^{t_{clamp}} V_{TVS}\, I(t)\, dt \approx E_L \cdot \frac{V_{TVS}}{V_{TVS} - V_{bus}}
$$

**Voltage seen at the MOSFETs/IC** includes the TVS loop parasitics:

$$
V_{node} = V_{TVS} + L_{trace}\frac{dI}{dt}
$$

As a rule of thumb, each nH of loop inductance adds about 1 V per A/ns.

---

## 3. Worked Example (single spike)

**Assumed values:** 3S Li-ion bus with V_bus = 12.6 V, L_eff = 20 µH, I₀ = 30 A, T_j = 25 °C.

**Note:** a 4S pack (16.8 V full) exceeds V_RWM = 16 V. The SMBJ16A would then leak and self-heat continuously. Use an SMBJ18A or higher for 4S.

| Quantity | Calculation | Result |
|---|---|---|
| Peak clamp voltage | 18.7 + 0.32·30 | **≈ 28.3 V** (exceeds datasheet V_C because I₀ > I_PP) |
| Inductive energy | ½·20 µH·30² | 9.0 mJ |
| Clamp duration | 20 µH·30 / (28.3 − 12.6) | ≈ 38 µs |
| TVS energy | 9.0 · 28.3/15.7 | **≈ 16 mJ** |
| Peak power | 30 · 28.3 | ≈ 850 W |

**Single-pulse check.** Derate P_PP for a ~38 µs triangular pulse using the datasheet pulse-power curve. This gives roughly 1.5–2.5 kW allowable, so a **single spike passes**. MOSFETs rated 30 V, however, have only about 1.7 V of margin before parasitic overshoot is added.

---

## 4. Repetitive Stress: Cycle 18 Thermal Accumulation

**Assumed burst pattern:** each braking event produces spikes at the commutation rate, f = 1 kHz for 50 ms. That is 50 spikes per event, repeated every T = 1 s, with a board thermal time constant τ ≈ 5 s.

**Power levels**
- Burst energy: 50 × 16 mJ = 0.8 J
- Burst average power: 16 W, about 3× the steady rating
- Long-term average power: P_avg = 0.8 W

**Junction temperature after n cycles** (superposition of thermal responses):

$$
T_j(n) = T_A + \underbrace{P_{avg} R_{\theta JA}\left(1 - e^{-nT/\tau}\right)}_{\text{baseline creep}} + \underbrace{P_{burst} Z_{th}(t_{burst})}_{\text{burst rise}}
$$

**Values at n = 18** (with T_A = 25 °C, Z_th(50 ms) ≈ 5 °C/W):

- Baseline creep: 0.8 · 100 · (1 − e^{−3.6}) ≈ **78 °C**
- Burst rise: 16 · 5 ≈ **80 °C**
- **T_j(18) ≈ 183 °C**, above the 150 °C T_j(max). **This is a fail.**

**Secondary effect: the clamp drifts upward.** V_BR rises by about 18 mV/°C × 158 °C ≈ +2.8 V. The peak clamp therefore reaches about **31 V** at cycle 18, which is now above a 30 V MOSFET's V_DS rating. The protection gets worse precisely as stress accumulates, so this is a runaway-type failure mode.

---

## 5. Summary of Behavior

1. **Spikes 1 to roughly 5:** the device clamps cleanly