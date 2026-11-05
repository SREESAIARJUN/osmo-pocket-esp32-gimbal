# SMBJ16A Clamp Response to Motor Back-EMF Spikes During ESC Regenerative Braking (Cycle 6)

"Cycle 6" isn't defined, so I treat it as **the 6th consecutive braking event in a repetitive test**. That matters because it brings in cumulative junction heating. The defaults below are assumptions. Substitute your measured values where you have them.

---

## 1. Device Model: SMBJ16A (unidirectional)

| Parameter | Symbol | Value |
|---|---|---|
| Stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage (at 1 mA) | V_BR | 17.8 V min / 18.7 V nom / 19.7 V max |
| Clamping voltage (at I_PP) | V_C | 26.0 V |
| Peak pulse current (10/1000 µs) | I_PP | 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Max junction temperature | T_J,max | 150 °C |

**Piecewise-linear clamp model:**

$$
V_{TVS}(I) =
\begin{cases}
V_{bus}, & V_{bus} < V_{BR} \quad (\text{leakage only, } I_R \le 1\,\mu A \text{ at } V_{RWM})\\[4pt]
V_{BR}(T_J) + R_d \cdot I, & V_{bus} \ge V_{BR}
\end{cases}
$$

**Dynamic resistance:**

$$
R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26.0 - 18.7}{23.1} \approx 0.32\ \Omega
$$

**Temperature coefficient:** V_BR rises by about +0.1 %/°C, so V_BR(T_J) = V_BR,25 · [1 + α(T_J − 25)], with α ≈ 1×10⁻³ /°C.

> **Compatibility flag:** A 4S LiPo charges to 16.8 V, which exceeds V_RWM = 16 V. Regen pushes the bus even higher. The SMBJ16A will therefore sit near its knee, leak more, and self-heat. Verify your battery chemistry before going further.

---

## 2. DC Bus Model During Regen Braking

**Equivalent circuit:**

- The ESC acts as a current source, I_regen(t), feeding the DC bus.
- The bus node has three paths:
  - the bulk capacitor C_bus (with ESR),
  - the TVS,
  - the battery path through wiring inductance L_w and resistance R_b.

**Regen current magnitude:**

$$
I_{regen} \approx \frac{k_e\,\omega\cdot D_{brake} - V_{bus}\,D_{brake}^2}{R_{ph}}
\qquad (\text{simplified; } k_e\omega = \text{back-EMF})
$$

**Bus node current balance:**

$$
C_{bus}\frac{dV_{bus}}{dt} = I_{regen}(t) - I_{batt}(t) - I_{TVS}(V_{bus})
$$

$$
L_w \frac{dI_{batt}}{dt} = V_{bus} - V_{batt} - R_b I_{batt}
$$

---

## 3. Two Distinct Spike Mechanisms

### (A) Fast switching spikes (ns–µs scale)

When the MOSFETs commutate, the stray loop inductance L_σ (typically 20–100 nH) produces:

$$
V_{spike} = V_{bus} + L_\sigma \frac{di}{dt}, \qquad E_A = \tfrac{1}{2} L_\sigma I_{ph}^2
$$

**Example:** L_σ = 50 nH, I_ph = 30 A
→ E_A = 22.5 µJ per event, which is **trivial** for the SMBJ.

The limiting factor here is TVS lead and trace inductance, not energy. With 5 nH of trace and di/dt = 1 A/ns, the trace alone adds about 5 V of overshoot above V_C. Place the TVS directly across the bridge and next to the low-ESR ceramic capacitors.

### (B) Bulk regen pump-up (ms–s scale)

This is the dangerous case. It occurs when the battery cannot absorb current:

- the battery is full,
- the BMS has opened,
- a connector is bouncing,
- or high L_w delays battery acceptance.

**Time to reach clamp:**

$$
t_{clamp} = \frac{C_{bus}\,(V_{BR} - V_{bus,0})}{I_{regen} - I_{batt}}
$$

**Clamp operating point** (once dV/dt ≈ 0):

$$
I_{TVS} \approx I_{regen} - I_{batt}, \qquad
V_{clamp} = V_{BR}(T_J) + R_d\, I_{TVS}
$$

**Dissipation and energy:**

$$
P_{TVS} = V_{clamp}\cdot I_{TVS}, \qquad
E_B = \int_0^{t_{brake}} V_{clamp}(t)\, I_{TVS}(t)\,dt
$$

---

## 4. Worked Example

**Assumptions:**

- C_bus = 470 µF
- V_bus,0 = 16.8 V
- I_regen = 20 A with the battery path open
- Braking duration t_brake = 50 ms

**Results:**

1. **Time to clamp:** t_clamp = 470 µF × (18.7 − 16.8) / 20 ≈ **45 µs**
2. **Clamp voltage:** V_clamp ≈ 18.7 + 0.32 × 20 ≈ **25.1 V**
3. **Power:** P ≈ **500 W** sustained for about 50 ms
4. **Energy:** E_B ≈ **25 J**

**Capability check:**

- The 600 W rating applies to a 10/1000 µs pulse, corresponding to roughly 0.6–1 J.
- At 50 ms, the derated power curve allows only about 100–150 W.
- **The device is overstressed by 3–5× on power and 20–40× on energy. It will fail short, typically within the first cycle.**

---

## 5. Cycle-6 Thermal Accumulation

For repetitive braking events k = 1…n with period T_cyc, use superposition with the transient thermal impedance Z_θJA(t):

$$
T_J(n) = T_A + \sum_{k=1}^{n} P_k \left[ Z_{\theta}\big((n-k)T_{cyc} + t_p\big) - Z_{\theta}\big((n-k)T_{cyc}\big) \right]
$$

(with Z_θ(0) = 0)

**Feedback effects:**

- V_BR(T_J) rises as the junction heats, so V_clamp drifts upward over the cycles.
- Leakage roughly doubles every ~10 °C near the knee. This is the thermal-runaway risk when V_batt > V_RWM.

**Steady-state limit:** with R_θJA ≈ 100 °C/W (SMB on minimal copper), average dissipation must satisfy:

$$
\bar P = \frac{E_{event}}{T_{cyc}} < \frac{150 - T_A}{R_{\theta JA}} \approx 1\ \text{W} \ (\text{at } T_A = 50\,°C)
$$

**Cycle-6 outcomes:**

- **Mechanism A only** (µJ per event): T_J rise is negligible, and cycle 6 looks the same as cycle 1.
- **Any meaningful Mechanism B energy:** the junction ratchets upward. Cycle 6 shows a higher V_clamp, a larger leakage tail, and likely failure if E_event × f_cyc exceeds about 1 W.

---

## 6. Design Conclusions

1. **The SMBJ16A is suitable for switching spikes (A), not for regen energy (B).** Use it as a local snubber-clamp only.
2. **Absorb regen energy elsewhere**, using one or more of:
   - guaranteeing a battery acceptance path,
   - a brake chopper (MOSFET plus power resistor triggered at about 18 V),
   - ESC firmware that limits regen current or sets a bus-voltage ceiling,
   - an active clamp.
3. **Use a higher-rated TVS for 4S**, such as the SMBJ18A or SMBJ20A. The SMBJ18A has V_RWM = 18 V and V_C ≈ 29.2 V. Keep V_C below the MOSFET V_DS rating with margin (e.g., 30 V FETs are marginal, 40 V FETs are preferable).
4. **If the TVS must handle bounded transients**, step up to SMCJ (1500 W) or 5.0SMDJ parts, and confirm the energy against the pulse-derating curve at the actual pulse width.
5. **Validate on the bench:**
   - Capture V_bus with a short ground spring, and I_TVS with a current probe, across cycles 1–6.
   - Check that V_clamp drift stays under about 1 V and that