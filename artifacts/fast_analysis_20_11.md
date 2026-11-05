# SMBJ16A Clamp Response Under Regenerative-Braking Back-EMF Spikes (Cycle 20)

"Cycle 20" is interpreted here as **the 20th consecutive braking event in a repetitive test**, so the analysis includes thermal accumulation. If it means something else in your test plan, such as a specific drive profile, substitute those values into the same equations.

---

## 1. Device Parameters (SMBJ16A, unidirectional, typical datasheet values)

| Parameter | Symbol | Value |
|---|---|---|
| Reverse standoff voltage | V_RWM | 16.0 V |
| Breakdown voltage at 1 mA | V_BR | 17.8 V min / 18.75 V nom / 19.7 V max |
| Max clamp voltage at I_PP | V_C | 26.0 V at 23.1 A (10/1000 µs) |
| Peak pulse power | P_PP | 600 W (10/1000 µs, T_A = 25 °C) |
| V_BR temperature coefficient | α_VBR | ≈ +0.1 %/°C (≈ +18 mV/°C) |
| Steady-state power | P_D | ≈ 5 W (lead at 75 °C, with good copper) |
| Max junction temperature | T_J,max | 150 °C |

**Derived dynamic resistance (worst case):**

$$R_d = \frac{V_C - V_{BR,min}}{I_{PP}} = \frac{26.0 - 17.8}{23.1} \approx 0.35\ \Omega$$

**Clamp model**, piecewise-linear and temperature-dependent:

$$V_{TVS}(i, T_J) = \begin{cases} V_{bus}, & i \approx 0 \ \ (V < V_{BR}) \\ V_{BR}(T_J) + R_d \, i, & V \ge V_{BR} \end{cases}$$

$$V_{BR}(T_J) = V_{BR,25}\,\big[1 + \alpha_{VBR}(T_J - 25)\big]$$

**Bus compatibility warning:** this part suits a 3S bus (12.6 V maximum). On a 4S bus, a fully charged pack reaches 16.8 V, which exceeds V_RWM. Leakage rises sharply and the device may conduct continuously when hot. For 4S, use SMBJ20A or SMBJ22A.

---

## 2. Two Distinct Spike Mechanisms

### (A) Commutation spike from parasitic inductance (ns scale, every PWM edge)

When a MOSFET turns off, current I₀ in the loop inductance L_σ (traces, bus-cap ESL, wiring) must go somewhere. Once the TVS clamps, the current decays linearly:

$$\frac{di}{dt} = -\frac{V_{clamp} - V_s}{L_\sigma}, \qquad t_{clamp} = \frac{L_\sigma I_0}{V_{clamp} - V_s}$$

The energy dumped into the TVS includes energy pulled from the source during the clamp interval:

$$E_{TVS} = \tfrac{1}{2} L_\sigma I_0^2 \cdot \frac{V_{clamp}}{V_{clamp} - V_s}$$

### (B) Regenerative bus pump (ms scale, during braking)

The ESC returns motor current I_regen to the DC link. If the battery cannot absorb it (high source impedance, BMS cutoff, connector bounce, battery full), the bulk capacitor charges:

$$\frac{dV_{bus}}{dt} = \frac{I_{regen} - I_{batt}}{C_{bus}}$$

Once V_bus reaches V_BR, the TVS becomes the energy sink:

$$P_{TVS} \approx (V_{BR} + R_d I_{regen})\, I_{regen}$$

---

## 3. Worked Example

**Assumptions:**
- 3S bus, V_s = 12.6 V
- L_σ = 50 nH
- Phase current I₀ = 30 A
- C_bus = 470 µF
- I_regen = 20 A
- PWM frequency 20 kHz
- Rotor J = 1×10⁻⁴ kg·m², ω = 1000 rad/s

### Case A: Commutation spike

- V_clamp ≈ 18.75 + 0.35·30 ≈ **29.3 V** (exceeds the 26 V datasheet V_C because I₀ > 23.1 A)
- t_clamp = 50 nH · 30 A / 16.7 V ≈ **90 ns**
- E_TVS = ½ · 50 nH · 30² · (29.3/16.7) ≈ **39 µJ per event**
- If this occurs on every PWM edge: P_avg ≈ 39 µJ × 20 kHz ≈ **0.8 W**

This is survivable, but it adds steady heating. Note that the TVS lead and package inductance (≈ 2–5 nH) plus the turn-on overshoot add several volts during the first few ns. The MOSFETs' own avalanche capability and local ceramic capacitors often absorb this before the TVS does.

### Case B: Regenerative bus pump

- Bus rise rate: 20 A / 470 µF ≈ **42.5 V/ms**, so the bus reaches V_BR in about 0.15 ms
- TVS power during clamp: (18.75 + 0.35·20)·20 ≈ **515 W**, continuous for as long as braking lasts
- Available rotor kinetic energy: ½Jω² = **50 J**
- TVS single-pulse energy capability: about **0.6–1 J** (600 W, 10/1000 µs)

**Result: catastrophic failure within about 1–2 ms.** The TVS cannot be the regen sink. It is only a backstop for nanosecond- and microsecond-scale spikes.

---

## 4. Cycle-20 Thermal Accumulation

For N braking events of duration t_p, repeated with period T, use superposition with the transient thermal impedance Z_th(t) from the datasheet curve:

$$T_J(N) = T_A + P \sum_{k=0}^{N-1}\Big[Z_{th}\big((N-1-k)T + t_p\big) - Z_{th}\big((N-1-k)T\big)\Big]$$

As T_J rises, three effects compound:

1. **Clamp voltage rises.** At T_J = 100 °C, V_BR rises by about 1.4 V, giving V_clamp(Cycle 20) ≈ 30.7 V instead of 29.3 V in Case A. Check this against the MOSFET V_DS rating (typically 30 V parts on 3S boards, so the margin is thin) and the ESC's capacitor voltage ratings.
2. **Power rating derates.** P_PP falls linearly to 0 at 150 °C:

$$P_{PP}(T_J) = 600\,\text{W} \cdot \frac{150 - T_J}{125}$$

3. **Leakage grows** exponentially near V_RWM. This becomes a th