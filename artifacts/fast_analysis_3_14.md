# SMBJ16A Clamp Response to ESC Regenerative-Braking Back-EMF Spikes

"Cycle 3" is treated here as the **third consecutive hard-braking event** in a repeated test sequence. That means the analysis has to include thermal state carried over from cycles 1–2. The motor and bus numbers below are representative placeholders. Substitute your measured values.

---

## 1. Device Model (SMBJ16A, unidirectional, 600 W @ 10/1000 µs)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage @ 1 mA | V_BR | 17.8 V min / 18.75 V typ / 19.7 V max |
| Max clamping voltage @ I_PP | V_C | 25.8 V @ 23.3 A |
| Peak pulse power | P_PP | 600 W (10/1000 µs, T_J0 = 25 °C) |

**Piecewise-linear clamp model:**

$$
i_{TVS}(V) =
\begin{cases}
I_R \;(\le 1\,\mu\text{A}) & V < V_{BR} \\[4pt]
\dfrac{V - V_{BR}(T_J)}{R_d} & V \ge V_{BR}
\end{cases}
$$

**Dynamic resistance:**

$$
R_d \approx \frac{V_C - V_{BR,max}}{I_{PP}} = \frac{25.8 - 19.7}{23.3} \approx 0.26\ \Omega
$$

**Temperature dependence of breakdown** (α_VBR ≈ +0.1 %/°C):

$$
V_{BR}(T_J) = V_{BR,25}\,[1 + \alpha_{VBR}(T_J - 25)]
$$

This matters for cycle 3. A hotter junction clamps higher, by about 0.5 V at +25 °C.

---

## 2. Bus Dynamics During Regenerative Braking

During braking the ESC's synchronous rectification (or body diodes) returns phase current to the DC link. The bus node equation is:

$$
C_{bus}\frac{dV_{bus}}{dt} = i_{regen}(t) - i_{batt}(t) - i_{TVS}(V_{bus}) - i_{load}
$$

with the battery path modelled as:

$$
i_{batt} = \frac{V_{bus} - V_{oc}}{R_{batt} + R_{wire}} \;\;\text{(plus } L_{wire}\,di/dt\text{ term)}
$$

Two distinct overvoltage mechanisms exist, and the TVS handles them very differently.

### (a) Fast commutation spikes (µs scale)

Each PWM/commutation edge interrupts current in the phase and stray inductance. The inductor discharges into the clamp:

$$
\frac{di_L}{dt} = -\frac{V_{clamp} - V_{EMF}}{L}
$$

The clamp time is:

$$
t_{clamp} = \frac{L\, I_0}{V_{clamp} - V_{EMF}}
$$

The energy delivered to the TVS is larger than ½LI², because the source EMF keeps pushing during the decay:

$$
E_{TVS} = \tfrac{1}{2} L I_0^2 \cdot \frac{V_{clamp}}{V_{clamp} - V_{EMF}}
$$

### (b) Sustained regen pump-up (ms–s scale)

If the battery cannot sink the current, C_bus charges until the TVS conducts the *entire* regen current. Causes include a BMS charge-FET opening, a full cell, a connector bounce, or a long, inductive battery lead.

---

## 3. Worked Example: Cycle 3

**Assumed system:**

| Parameter | Value |
|---|---|
| Battery | 4S Li-ion, V_oc = 16.4 V (near full) |
| Phase L (line-line) | 20 µH; stray loop inductance 50 nH |
| Braking current | I₀ = 30 A |
| Back-EMF at braking onset | 14 V |
| Bus capacitance | C_bus = 470 µF |
| PWM frequency | 24 kHz |
| Braking duration | 0.3 s per cycle, 1 s apart |

### 3.1 Commutation spike (stray-L dominated, absorbed by TVS)

- **Stray-L spike energy:**
  $$E = \tfrac{1}{2}(50\,\text{nH})(30)^2 \times \frac{24}{24-16.4} \approx 71\ \mu\text{J}$$
- **Peak TVS current:** ≈ 30 A (initially). This **exceeds I_PP = 23.3 A**, but only for tens of ns.
- **Clamp voltage:**
  $$V_C = 18.75 + 30 \times 0.26 \approx 26.5\ \text{V}$$
- **Clamp time:**
  $$t \approx \frac{50\,\text{nH} \times 30}{26.5 - 16.4} \approx 150\ \text{ns}$$
- **Repetition:** at 24 kHz the average power is 71 µJ × 24 k ≈ **1.7 W**.

This average is **beyond the SMB package's steady rating** (P_D ≈ 1–5 W depending on copper). The spike itself is benign. The repetition rate is the problem.

> In practice C_bus and local MLCCs (≥1 µF at the half-bridge) absorb most of this. The TVS should only see what escapes the decoupling loop. Verify with a scope at the TVS pads.

### 3.2 Phase-inductance dump (sudden ESC disable mid-brake)

- **Energy into the TVS** (if C_bus is remote or disconnected):
  $$E = \tfrac{1}{2}(20\,\mu\text{H})(30)^2 \times \frac{26.5}{26.5-14} \approx 19\ \text{mJ}$$
- **Duration:**
  $$t \approx \frac{20\,\mu\text{H} \times 30}{12.5} \approx 48\ \mu\text{s}$$
- **Average power over the pulse:** ≈ 400 W.

The P_PP rating scales roughly as t^(-1/2). Normalised to a 48 µs triangular pulse, the allowable peak is about 600 × (1000/48)^0.5 × (shape factor ≈ 0.5) ≈ 1.3 kW. The pulse at 26.5 V × 30 A ≈ 800 W peak is therefore **OK, with about 1.6× margin at 25 °C.**

### 3.3 Sustained regen with battery disconnected (worst case)

Once V_bus exceeds about 18.75 V, the TVS carries the full regen current:

$$
V_{bus} = V_{BR} + I_{regen} R_d = 18.75 + 30(0.26) \approx 26.5\ \text{V}
$$

$$
P_{TVS} \approx 26.5 \times 30 \approx 800\ \text{W, continuous}
$$

From the transient Z_th of an SMB (≈ 0.2 °C/W at 1 ms, ≈ 10 °C/W at 100 ms, ≈ 100 °C/J-equivalent beyond that), T_J exceeds 150 °C within about 2–5 ms.

**The device fails short.** That is usually the safe failure mode, but it then shorts the bus.

---

## 4. Cycle-to-Cycle Thermal Accumulation (Why Cycle 3 Matters)

Model the junction with a two-pole Foster network:

$$
\Delta T_J(t) = \sum_{n} P_n \left[ Z_{th}(t - t_n) - Z_{th}(t - t_n - \tau_n) \right]
$$

Typical SMB on 1 oz copper: R₁ = 15 °C/W, τ₁ = 5 ms; R₂ = 60 °C/W, τ₂ = 3 s.

With 1.7 W repetitive spike dissipation for 0.3 s every 1 s:

| Cycle | T_J start | T_J end of brake | V_BR shift |
|---|---|---|---|
| 1 | 25 °C | ~45 °C | +0.4 V |
| 2 | ~37 °C | ~55 °C | +0.6 V |
| **3** | **~45 °C** | **~62 °C** | **+0.7 V** |

**Consequences at cycle 3:**

1. **Clamp voltage rises** to about 27.2 V on the 30 A spike. Check this against the MOSFET V_DS rating. A 30 V FET now has under 