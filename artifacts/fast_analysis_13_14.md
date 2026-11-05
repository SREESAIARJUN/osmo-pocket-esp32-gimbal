# SMBJ16A Clamp Response to ESC Regenerative-Braking Back-EMF Spikes

I don't know what "Cycle 13" refers to in your test plan or project, so I've treated it as one braking event in a repeated sequence. Replace my assumed values with your measured L, I, V_bus, and timing.

---

## 1. Device Model (SMBJ16A, datasheet values)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage (at 1 mA) | V_BR | 17.8 V min, 19.7 V max |
| Max clamping voltage at I_PP | V_C | 26.0 V |
| Peak pulse current (10/1000 µs) | I_PP | 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Temperature coefficient of V_BR | α_VBR | ≈ +0.1 %/°C |

**Piecewise-linear clamp model:**

$$
V_{TVS}(i) =
\begin{cases}
\approx 0 \;(\text{leakage}) & V < V_{BR} \\
V_{BR}(T_j) + R_d \, i & V \ge V_{BR}
\end{cases}
$$

$$
R_d \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26.0 - 19.7}{23.1} \approx 0.27\ \Omega \quad (\text{worst-case } V_{BR,max})
$$

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_{VBR}(T_j - 25^\circ\text{C})]
$$

The temperature term matters for repeated cycles. A hot junction clamps higher.

---

## 2. Circuit Formulation

During braking, the inductive energy commutates through the FET body diodes onto the DC bus. Assume the TVS sits across the bus, in parallel with the bus capacitance C_bus and the source (battery) impedance.

**Inductor discharge into the clamp.** L is the effective loop inductance: stray or motor phase. V_s is the bus/source voltage the clamp works against.

$$
L\frac{di}{dt} = -\big(V_{BR} - V_s + R_d\, i\big), \qquad i(0) = I_0
$$

**Solution:** Let $V_a = V_{BR} - V_s$ and $\tau = L/R_d$.

$$
i(t) = \Big(I_0 + \frac{V_a}{R_d}\Big)e^{-t/\tau} - \frac{V_a}{R_d}
$$

**Clamp duration** (when i reaches 0):

$$
t_{clamp} = \tau \ln\!\left(1 + \frac{R_d I_0}{V_a}\right) \;\approx\; \frac{L I_0}{V_{C,avg} - V_s} \quad (\text{when } R_d I_0 \ll V_a)
$$

**Peak clamp voltage seen by the FETs.** The last term is often dominant:

$$
V_{peak} = V_{BR} + R_d I_0 + L_{TVS,loop}\frac{di}{dt}\Big|_{edge}
$$

**Energy absorbed by the TVS.** This is more than ½LI², because the source also drives current through the clamp:

$$
E_{TVS} = \int_0^{t_{clamp}} V_{TVS}\, i\, dt \;\approx\; \tfrac12 L I_0^2 \cdot \frac{V_C}{V_C - V_s}
$$

---

## 3. Worked Cases (12 V bus, V_s ≈ 12.6 V, I_0 = 30 A)

### Case A: Switching-edge spike (stray loop L ≈ 50 nH)

- ½LI² = 22.5 µJ, so E_TVS ≈ 45 µJ
- t_clamp ≈ 50 nH × 30 A / (~27 − 12.6) V ≈ **~100 ns**
- V_clamp ≈ 19.7 + 0.27 × 30 ≈ **28 V** (before lead inductance)
- Lead/loop overshoot: 5 nH × (30 A / 10 ns) ≈ **+15 V**

**Verdict:** Energy is trivial. Peak voltage is the real risk. With 30 V MOSFETs, the TVS may react too slowly through its trace inductance. Place it directly at the FET bridge with minimal loop area, and keep low-ESL MLCCs as the first-line snubber.

### Case B: Phase-winding commutation (L ≈ 20 µH)

- ½LI² = 9 mJ, so E_TVS ≈ 9 × (27 / 14.4) ≈ **17 mJ**
- t_clamp ≈ 20 µH × 30 A / 14.4 V ≈ **~42 µs**
- Single-pulse check: 600 W over 10/1000 µs corresponds to roughly 0.8 J. **A single pulse passes.**
- **Repetitive check:** 17 mJ × 20 kHz PWM ≈ **340 W average**. The SMB package's steady-state dissipation is about 1–5 W depending on copper. **Fails catastrophically.**

### Case C: Sustained regen bus pump-up (battery disconnected or high-impedance)

Kinetic energy return, for example ½Jω² ≈ 1 J, into C_bus = 470 µF:

$$
V_{final} = \sqrt{V_0^2 + \frac{2E}{C}} = \sqrt{12^2 + \frac{2(1)}{470\mu}} \approx 66\text{ V} \;\;(\text{unclamped})
$$

The TVS would try to absorb most of that 1 J over tens of milliseconds. Pulse-power derating (roughly ∝ t^−0.5) gives about 200 W at 10 ms. That is marginal for one event and fatal when repeated each braking cycle.

---

## 4. Repeated-Cycle Thermal Formulation

For cycle *n*, use superposition with the transient thermal impedance Z_th(t):

$$
T_j(n) = T_A + \sum_{k=1}^{n} P_k \big[Z_{th}(t_n - t_k) - Z_{th}(t_n - t_k - t_{p})\big]
$$

Feed T_j back into V_BR(T_j). Clamp voltage creeps upward each cyc