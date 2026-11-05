# SMBJ16A TVS Clamp Response to ESC Regenerative-Braking Back-EMF

**Assumption:** "Cycle 26" is treated as the 26th repeated braking event in a test sequence. This means cumulative thermal state matters, not just a single-pulse rating. Replace the example values below with your measured bus waveforms.

---

## 1. Device Parameters (SMBJ16A, unidirectional, typical datasheet)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage (at 1 mA) | V_BR | 17.8 V min, 18.7 V nom, 19.7 V max |
| Max clamping voltage | V_C | 26.0 V at I_PP = 23.1 A |
| Peak pulse power | P_PP | 600 W (10/1000 µs) |
| Single-pulse energy (approx.) | E_max | ~0.6–0.85 J (10/1000 µs equivalent) |
| V_BR temperature coefficient | α_T | ~+0.09 %/°C |
| Steady-state dissipation | P_D | a few W, lead-temperature dependent (check datasheet) |

---

## 2. Clamp I–V Model

Use a piecewise-linear model:

$$
V_{clamp}(I) \approx
\begin{cases}
V_{bus}, & V_{bus} < V_{BR}(T) \\
V_{BR}(T) + R_{dyn}\, I_{TVS}, & \text{conducting}
\end{cases}
$$

The dynamic resistance follows from the datasheet points:

$$
R_{dyn} \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26.0 - 18.7}{23.1} \approx 0.32\ \Omega
$$

Breakdown voltage drifts upward with junction temperature:

$$
V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_T (T_j - 25)]
$$

This matters across repeated cycles: a hot TVS clamps higher.

---

## 3. Two Distinct Overvoltage Mechanisms

### (a) Switching-edge inductive spike (ns–µs scale)

This comes from bus or wiring inductance when the FETs commutate:

$$
V_{spike} = L_{par}\frac{di}{dt}, \qquad E_{spike} = \tfrac{1}{2} L_{par} I^2
$$

**Example:** with L_par = 200 nH and I = 30 A, E = ½ · 200 nH · 30² ≈ **90 µJ**.

This is trivial for the TVS. It clamps easily, provided the TVS is placed with minimal loop inductance directly at the ESC bus terminals.

### (b) Regenerative bus pumping (ms scale), the dominant threat

When the ESC brakes, it returns motor energy to the DC bus. If the source cannot absorb it, bus voltage rises. This happens if:
- the battery has high impedance or long leads,
- the BMS opens, or
- the system runs on a bench PSU that cannot sink current.

Bus capacitance absorbs the energy first:

$$
\Delta V = \frac{I_{regen}\,\Delta t}{C_{bus}}
$$

Once V_bus reaches V_BR, the TVS takes the excess current:

$$
I_{TVS}(t) = I_{regen}(t) - I_{sink}(t) - C_{bus}\frac{dV}{dt}
$$

$$
P_{TVS}(t) = V_{clamp}(I_{TVS})\cdot I_{TVS}, \qquad E_{TVS} = \int_0^{t_{brake}} P_{TVS}\,dt
$$

Mechanical energy sets the upper bound:

$$
E_{regen} \le \tfrac{1}{2} J\,(\omega_1^2 - \omega_2^2)\cdot\eta_{regen}
$$

---

## 4. Worked Example: Open-Bus Regen Event

**Inputs:**
- I_regen = 30 A
- Braking duration t = 5 ms
- C_bus = 470 µF
- Source sinks nothing (worst case)

**Step 1: Capacitor rise from 12.6 V to V_BR ≈ 18.7 V**

$$
t_{cap} = C\,\Delta V / I = 470\,\mu F \cdot 6.1 / 30 \approx 96\ \mu s
$$

The TVS conducts for essentially the full 5 ms.

**Step 2: Clamp voltage**

$$
V_{clamp} = 18.7 + 0.32 \cdot 30 \approx 28.3\ \text{V}
$$

This already exceeds the 26 V V_C specification.

**Step 3: Power**

$$
P \approx 28.3 \cdot 30 \approx 850\ \text{W}
$$

**Step 4: Pulse-width derating**

Allowable peak power scales roughly as $P_{PP}(t) \approx P_{PP,1ms}\sqrt{1\,ms/t}$:

$$
P_{PP}(5\,ms) \approx 600\sqrt{1/5} \approx 270\ \text{W}
$$

**Step 5: Energy**

$$
E \approx 850 \cdot 5\,ms \approx 4.2\ \text{J}
$$

This is about 5× the device's single-pulse energy capability.

**Result:** the SMBJ16A is overstressed by roughly 3–5×. Expect it to fail short, which is usually followed by a fuse or trace failure.

---

## 5. Repetitive-Cycle Thermal Accumulation (Cycle n = 26)

A first-order thermal model of junction temperature after n pulses:

$$
T_j(n) = T_A + P_{avg} R_{\theta JA} + \Delta T_{pulse}\,\frac{1 - e^{-n\,t_{period}/\tau_{th}}}{1 - e^{-t_{period}/\tau_{th}}}\, e^{-(t_{period}-t_p)/\tau_{th}}
$$

where:

$$
P_{avg} = \frac{E_{TVS}}{t_{period}}
$$

- R_θJA ≈ 60–100 °C/W for SMB on a typical PCB.
- τ_th is on the order of seconds.

**Consequences:**
- By cycle 26, if t_period is comparable to τ_th, T_j approaches steady state.
- V_BR rises by about 1.5–2 V.
- The clamp voltage rises with it, so stress on the ESC FETs and capacitors increases each cycle.
- Even sub-rated pulses cause cumulative degradation. Watch for rising leakage at V_RWM as an early failure indicator.

---

## 6. Additional Design Flag: Standoff Margin

V_RWM = 16 V sits below a 4S Li-ion bus at full charge (16.8 V).

- **3S systems (12.6 V max):** the SMBJ16A is appropriate.
- **4S systems:** use an SMBJ18A or SMBJ20A. Then confirm the FET V_DS rating still exceeds the resulting V_C, with derating.

---

## 7. Conclusions and Recommendations

1. **The TVS handles switching spikes only.** It is suitable for µJ-level commutation transients but not for regen energy dumping.
2. **Primary regen protection must come from elsewhere:**
   - Firmware bus-overvoltage limit that reduces regen current or switches to passive braking.
   - A brake chopper (FET plus power resistor) triggered at about 17–18 V, below V_BR.
   - Adequate bulk capacitance, sized so $C \ge 2E_{regen}/(V_{max}^2 - V_{nom}^2)$.
   - Ensure