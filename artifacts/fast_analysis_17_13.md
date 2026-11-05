# SMBJ16A Clamp Response to Regenerative-Braking Back-EMF Spikes

I don't have your "Cycle 17" waveforms or board parameters. The model below uses datasheet values and stated assumptions. Each quantity is defined so you can substitute your measured values.

---

## 1. Device Parameters (SMBJ16A, typical datasheet values)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage at 1 mA | V_BR | 17.8 V min, 19.7 V max (~18.75 V nominal) |
| Max clamping voltage at I_PP | V_C | 26.0 V at 23.1 A |
| Peak pulse power (10/1000 µs) | P_PP | 600 W |
| Steady-state power dissipation | P_D | ~5 W (lead temp 75 °C, good copper) |
| V_BR temperature coefficient | α_T | ~+0.1 %/°C (~18 mV/°C) |

**Compatibility check:** V_RWM = 16 V is suitable only for a 3S Li-ion bus (12.6 V max) or a 12 V lead-acid bus (≤14.7 V charging). On a 4S pack (16.8 V full charge), the diode would be in leakage or partial breakdown continuously. Confirm your bus voltage before going further.

---

## 2. Clamp Model

Model the diode as a piecewise-linear device with dynamic resistance:

$$
V_{TVS}(I,T_j) = V_{BR}\,[1+\alpha_T (T_j-25^\circ C)] + R_d\, I
$$

The dynamic resistance follows from the datasheet clamping point:

$$
R_d \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26.0-18.75}{23.1} \approx 0.31\ \Omega
$$

The effective clamp seen by the node also includes package and trace inductance (L_par ≈ 2–5 nH for SMB plus layout):

$$
V_{node}(t) = V_{TVS}(I) + L_{par}\frac{dI}{dt}
$$

The silicon itself responds in picoseconds, so L_par dominates the first few nanoseconds of overshoot.

---

## 3. Spike Mechanisms During Regenerative Braking

Two distinct phenomena occur during regen, and they must be treated separately.

### (a) Switching-edge spikes, caused by loop stray inductance L_s

$$
V_{spike} = V_{bus} + L_s \frac{di}{dt}
$$

- **Duration:** nanoseconds.
- **Energy:** microjoules.
- **Example:** with L_s = 15 nH and di/dt = 1 A/ns, the spike is about 15 V above the bus, so roughly 27 V peak.
- **Mitigation:** the local ceramic capacitors should absorb most of this. If the TVS conducts here, it mainly indicates a layout or decoupling problem.

### (b) Inductive energy dump and bus pumping

This is the main concern. If the battery cannot accept regen current, the phase inductance L_ph freewheels its current I₀ through the body diodes into the DC bus. Typical causes are:
- a BMS cutoff,
- a blown fuse,
- high pack impedance,
- a connector bounce.

When the bus reaches the clamp voltage, the TVS current decays linearly. The voltage across the inductor is V_C − V_bus, where V_bus is the opposing back-EMF or residual bus voltage:

$$
i_{TVS}(t) = I_0 - \frac{V_C - V_{bus}}{L_{ph}}\,t
$$

The clamp duration is:

$$
t_{clamp} = \frac{L_{ph} I_0}{V_C - V_{bus}}
$$

The energy absorbed by the TVS exceeds the stored inductive energy, because the source keeps driving current while the diode clamps:

$$
E_{TVS} = \tfrac12 L_{ph} I_0^2 \cdot \frac{V_C}{V_C - V_{bus}}
$$

The peak power is:

$$
P_{pk} = V_C(I_0)\cdot I_0
$$

### Capacitor pre-absorption

The bulk capacitor C_bus delays TVS conduction. The TVS only conducts once the capacitor has charged from V_bus to V_BR:

$$
\Delta t_{delay} \approx \frac{C_{bus}(V_{BR}-V_{bus})}{I_0}
$$

---

## 4. Thermal Formulation

### Single pulse

Convert the triangular pulse to an equivalent rectangular pulse with the same energy and peak, so t_eq ≈ t_clamp / 2. Then compare it with the device's P_PP-versus-pulse-width derating curve. That curve scales roughly as P_PP ∝ t^(−0.5), which gives several kW at 10–50 µs.

The junction temperature rise is:

$$
\Delta T_j = P_{pk}\cdot Z_{th}(t_{eq})
$$

### Repetitive pulses

Spikes repeat at the commutation or PWM rate f_rep during braking:

$$
P_{avg} = E_{TVS}\cdot f_{rep} \;\; \le\;\; P_D \approx 5\text{ W (derated for } T_{amb})
$$

The steady-state junction temperature is:

$$
T_j = T_{amb} + P_{avg}\,R_{\theta JA} + P_{pk}\,Z_{th}(t_{eq})
$$

This must stay below 150 °C, with margin. As T_j rises, V_BR rises with it, which pushes V_C higher.

---

## 5. Worked Example (assumed values; replace with Cycle 17 data)

**Assumptions:**
- L_ph = 20 µH
- I₀ = 30 A
- V_bus = 12.6 V
- Battery disconnected during braking

**Clamp voltage at 30 A:**
$$V_C \approx 18.75 + 0.31\times30 \approx 28\text{ V}$$
This is above the 26 V datasheet point, so check that the MOSFET V_DS rating has margin.

**Clamp duration:**
$$t_{clamp} = \frac{20\,\mu H \times 30}{28-12.6} \approx 39\ \mu s$$

**Energy per event:**
$$E_{TVS} = 0.5\times20\mu\times900\times\frac{28}{15.4} \approx 16\text{ m