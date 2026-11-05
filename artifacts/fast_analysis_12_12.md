# TVS Clamp Response Model: SMBJ16A Under ESC Regenerative-Braking Spikes

I don't know what "Cycle 12" refers to in your test plan. I've treated it as **the 12th consecutive braking event in a repeated braking test**, where cumulative heating matters. The bus voltage, wiring, and braking profile are also assumptions. Replace them with your measured values.

---

## 1. Device Parameters (SMBJ16A, typical datasheet)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage at 1 mA | V_BR | 17.8 V min / ~18.7 V nom / 19.7 V max |
| Max clamping voltage | V_C | 26.0 V at I_PP = 23.1 A (10/1000 µs) |
| Peak pulse power | P_PP | 600 W (10/1000 µs) |
| Temperature coefficient of V_BR | α_T | ≈ +0.09 %/°C |
| Steady-state dissipation | P_D(AV) | ~5 W at T_L = 75 °C (≈1 W realistic on a small PCB pad) |

**Bus compatibility check:**
- **3S Li-ion (12.6 V max):** fine.
- **4S (16.8 V max):** exceeds V_RWM. Leakage will rise sharply and the part will run warm continuously. Use an SMBJ20A or SMBJ22A for 4S.

---

## 2. Clamp I–V Model

A piecewise-linear model with dynamic resistance:

$$
V_{TVS}(I,T_j) = V_{BR}(T_j) + R_d \cdot I
$$

$$
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26 - 18.7}{23.1} \approx 0.32\ \Omega
$$

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_T (T_j - 25)]
$$

Below V_BR, the device conducts only leakage (µA).

---

## 3. Two Distinct Stress Mechanisms in Regen Braking

### (A) Inductive commutation spike (µs scale, every PWM edge)

When the ESC low-side/high-side commutation interrupts current in the battery-lead inductance L_w, the lead current must decay through the bus node.

**Step 1: Check whether the bus capacitor absorbs it first.**

$$
\Delta V_{cap} \approx I_{pk}\sqrt{L_w / C_{bus}}
$$

Example: L_w = 200 nH (≈20 cm lead pair), I = 30 A, C_bus = 1000 µF:

$$
\Delta V_{cap} = 30\sqrt{2\times10^{-10}/10^{-3}} \approx 0.42\ \text{V}
$$

The TVS never conducts. **With healthy bulk capacitance, the TVS is idle.** It becomes active only if:
- C_bus is small, or
- ESR/ESL is high (aged electrolytics, cap placed far from the FETs), or
- the TVS is closer to the switching node than the caps are.

**Step 2: If the TVS does conduct (worst case, caps ineffective).**

Clamp voltage:
$$V_{cl} = V_{BR} + R_d I_{pk} \approx 18.7 + 0.32(30) \approx 28.3\ \text{V}$$

Note that 30 A exceeds the 23.1 A datasheet point, so this is extrapolated.

Clamp duration:
$$t_{cl} = \frac{L_w I_{pk}}{V_{cl} - V_{bus}} = \frac{200\text{n}\cdot 30}{28.3 - 12.6} \approx 0.38\ \mu s$$

Energy per spike (the source keeps feeding during the clamp):
$$E_{spike} = \tfrac12 L_w I_{pk}^2 \cdot \frac{V_{cl}}{V_{cl}-V_{bus}} = \tfrac12(200\text{n})(900)\cdot\frac{28.3}{15.7} \approx 0.16\ \text{mJ}$$

Peak power ≈ 28.3 × 30 ≈ 850 W.

**Single-pulse rating:** Pulse capability scales roughly as \(P_{PP}(t) \approx 600\sqrt{1000/t_{\mu s}}\), giving tens of kW at 0.4 µs. **A single spike is safe.**

**Repetitive rating (the real problem):**
$$P_{avg} = E_{spike}\cdot f_{PWM} = 0.16\text{ mJ} \times 24\text{ kHz} \approx 3.9\ \text{W}$$

This exceeds realistic SMB steady dissipation. **The TVS will overheat during a sustained brake if it clamps on every edge.**

### (B) Sustained regen overvoltage (ms–s scale)

If the battery cannot absorb the regen current, the bus is pumped up. Causes include a BMS charge-FET cutout, a full pack, or a disconnected connector. Then:

$$
I_{regen} \approx \frac{P_{brake}}{V_{bus}}, \qquad P_{TVS} \approx V_{cl}\cdot I_{regen}
$$

Example: 10 A of regen into the TVS at ~22 V gives ~220 W continuous. The 10/1000 µs rating is far exceeded within milliseconds, so **the SMBJ16A fails short** (typically).

**Conclusion:** A TVS is not a brake resistor. Mechanism B requires a different solution:
- a brake chopper,
- an active clamp, or
- firmware that limits regen when V_bus exceeds a threshold.

---

## 4. Thermal Accumulation Across Braking Cycles (Cycle 12)

Let each braking event deposit energy into the junction, and let the device cool between events.

**Per-cycle temperature rise:**
$$
\Delta T_{event} = P_{avg}\cdot Z_{th}(t_{brake})
$$

Here \(Z_{th}\) is the transient thermal impedance (SMB on FR-4: R_th(j-a) ≈ 80–100 °C/W, τ ≈ a few seconds).

**Cycle recursion** (brake for t_on, rest for t_off):
$$
T_{n} = T_a + \big[(T_{n-1} - T_a) + \Delta T_{event}\big]\,e^{-t_{off}/\tau}
$$
$$
T_{peak,n} = T_{n-1} + \Delta T_{event}
$$

**Steady-state limit** (approached well before n = 12 if t_off is short relative to τ):
$$
T_{peak,\infty} = T_a + \frac{\Delta T_{event}}{1 - e^{-t_{off}/\tau}}
$$

**Example:** P_avg = 3.9 W, t_on = 0.5 s, R_th ≈ 90 °C/W, τ