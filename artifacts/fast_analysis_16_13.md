# SMBJ16A Clamp Response to Regenerative Back-EMF Transients

"Cycle 16" isn't a standard I can identify, so I've treated it as one discrete braking event in your test sequence. The model below works for any cycle. Replace my assumed parameters with your measured values.

---

## 1. Device Parameters (SMBJ16A, unidirectional)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse stand-off voltage | V_RWM | 16.0 V |
| Breakdown voltage (@ 1 mA) | V_BR | 17.8 V min / 18.75 V nom / 19.7 V max |
| Max clamping voltage | V_C | 26.0 V @ I_PP = 23.1 A |
| Peak pulse power | P_PP | 600 W (10/1000 µs waveform) |
| Max leakage @ V_RWM | I_R | ~1 µA (25 °C) |
| V_BR temperature coefficient | α_VBR | ≈ +0.1 %/°C |
| Thermal resistance (SMB, typical PCB) | θ_JA | ~75–100 °C/W |

**Linearized clamp model**

$$V_{TVS}(I) = V_{BR}(T_j) + R_d \cdot I_{TVS}, \qquad R_d \approx \frac{V_C - V_{BR,nom}}{I_{PP}} = \frac{26 - 18.75}{23.1} \approx 0.31\ \Omega$$

$$V_{BR}(T_j) = V_{BR,25}\,[1 + \alpha_{VBR}(T_j - 25)]$$

---

## 2. Transient Formulation During Regenerative Braking

### 2.1 Bus current balance
When the ESC commands braking, phase current commutates through the high-side FETs or body diodes into the DC bus:

$$I_{regen}(t) = C_{bus}\frac{dV_{bus}}{dt} + I_{batt}(t) + I_{TVS}(t)$$

The battery path includes lead inductance L_w and ESR R_b:

$$V_{bus} = V_{batt} + R_b I_{batt} + L_w \frac{dI_{batt}}{dt}$$

The TVS conducts only when V_bus exceeds V_BR. This happens in three situations:
- **(a)** The battery is disconnected, or the BMS opens its charge FET.
- **(b)** Lead inductance L_w blocks fast di/dt, so the battery cannot absorb the current edge.
- **(c)** The battery is full and has high ESR at low temperature.

### 2.2 Phase 1: Capacitor charging (pre-clamp)

$$t_1 \approx \frac{C_{bus}\,(V_{BR} - V_{bus,0})}{I_{regen} - I_{batt}}$$

### 2.3 Phase 2: Clamped inductive discharge
The worst case is inductive energy dumped against a near-constant clamp, with no battery absorption. The motor phase inductance L_m discharges with initial current I_0:

$$\frac{dI}{dt} = -\frac{V_{TVS} - V_{bus,0} + E_{bemf}}{L_m}$$

Conservatively, ignore back-EMF assist and take V_TVS ≈ V_C,eff. Then:

$$t_{clamp} = \frac{L_m I_0}{V_{C,eff} - V_{bus}}$$

$$E_{TVS} = \tfrac{1}{2} L_m I_0^2 \cdot \frac{V_{C,eff}}{V_{C,eff} - V_{bus}}$$

$$V_{C,eff} = V_{BR}(T_j) + R_d I_0$$

---

## 3. Worked Example (assumed values)

Assumptions: 3S Li-ion bus (V_bus = 12.6 V full), L_m = 20 µH, I_0 = 30 A peak braking current, T_j = 25 °C.

1. **Clamp voltage:** V_C,eff = 18.75 + 0.31 × 30 ≈ **28.1 V**.
   This exceeds the 26 V datasheet figure because I_0 is greater than I_PP.
2. **Clamp duration:** t_clamp = (20 µH × 30 A) / (28.1 − 12.6) ≈ **39 µs**.
3. **Absorbed energy:** E = 0.5 × 20e-6 × 900 × (28.1 / 15.5) ≈ **16 mJ**.
4. **Peak power:** P_pk ≈ 28.1 × 30 ≈ **845 W**, as a triangular pulse.
5. **Rating check:** Peak pulse power scales approximately as t^-0.5. Rated power at ~40 µs is roughly 600 × √(1000/40) ≈ **3 kW**.

**Result:** a single event is within rating, with about 3.5× margin. Confirm this against the manufacturer's P_PP vs. t_p curve.

---

## 4. Critical Failure Modes

### 4.1 Sustained regen into an open bus (most important)
The TVS is a transient suppressor, not a brake resistor. If the battery disconnects during continuous braking, the TVS must dissipate the full regenerative power, P = V_C × I_regen. At 30 A that is about 840 W continuous.

The SMB package can handle only about 1–1.5 W steady-state. It will fail short within milliseconds. A shorted TVS on the bus is a fire risk.

**Mitigations:**
- Add firmware bus-overvoltage limiting: reduce braking current when V_bus exceeds about 15.5 V.
- Add a brake chopper resistor for systems that can run without the battery.
- Add BMS coordination so the charge FET does not open mid-brake.

### 4.2 Repetitive pulses
Average dissipation is P_avg = E_TVS × f_events. At 16 mJ per event, the TVS can sustain only about 60 events/s at ~1 W. That limit holds only if each event produces a full clamp.

Thermal rise is ΔT_j = P_avg × θ_JA. As T_j rises, V_BR increases (≈ +0.1 %/°C), so the clamp voltage climbs.

### 4.3 MOSFET voltage margin
A clamp of 28 V or higher, plus layout ringing (L_stray × di/dt), can exceed the rating of **30 V FETs**. Use **40 V MOSFETs**, or add low-ESR ceramic capacitance to reduce dV/dt.

### 4.4 Stand-off mismatch
- A 3S pack (12.6 V) is fine.
- A **4S pack (16.8 V) exceeds V_RWM = 16 V**. Leakage will rise, and the TVS will self-heat at hot ambient.
- For 4S, use an **SMBJ20A** and re-verify FET margin.

---

## 5. Validation Procedure

1. Probe V_bus differentially at the TVS pads with a short ground spring. Measure I_TVS with a current probe on the TVS leg.
2. Trigger a full-throttle-to-full-brake step with the battery connected. Then repeat with a lab supply that cannot sink current, to emulate an open BMS.
3. Integrate ∫V·I dt to get the measured E_TVS. Compare it with the formula in §2.3.
4. Log T_j (thermocouple on the TVS package) across repeated cycles to check thermal accumulation.
5. Pass criteria:
   - V_bus,peak < 0.8 × V_DS(max) of the FETs
   - E_TVS < 50 % of rated at the measured t_p
   - T_j,peak < 125 °C

If you share your actual motor inductance, braking current, bus capacitance, cell count, and the definition of "Cycle 16", I can compute the specific clamp waveform and margins.