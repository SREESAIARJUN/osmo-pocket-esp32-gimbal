# TVS Clamp Response: SMBJ16A Under ESC Regenerative Braking Back-EMF

"Cycle 24" is not a standard I recognize, so I treat it as one repetition in a repeated braking test cycle. The analysis is per event, then checked for repetition.

## 1. Device Model (SMBJ16A, unidirectional, from datasheet)

| Parameter | Value |
|---|---|
| V_RWM (standoff) | 16.0 V |
| V_BR (at 1 mA) | 17.8 V min, about 18.8 V typ, 19.7 V max |
| V_C at I_PP | 26.0 V at 23.1 A (10/1000 µs) |
| P_PP | 600 W (10/1000 µs, T_A = 25 °C) |
| P_M(AV) steady state | about 5 W (lead temperature 75 °C), much less on typical PCB copper |

**Piecewise-linear clamp model:**

$$
V_{TVS}(i) =
\begin{cases}
i\,R_{leak} \approx 0, & V < V_{BR}\\
V_{BR} + R_d\, i, & V \ge V_{BR}
\end{cases}
\qquad
R_d \approx \frac{V_C - V_{BR}}{I_{PP}} = \frac{26 - 18.8}{23.1} \approx 0.31\ \Omega
$$

## 2. Spike Source During Regen Braking

There are two distinct mechanisms. They must be treated separately.

**(a) Commutation spike (fast, every PWM edge).** When the low-side FET turns off, regen phase current I₀ freewheels into the bus through loop stray inductance L_s:

$$
V_{spike} = V_{bus} + L_s \frac{di}{dt}
$$

**(b) Bus pump-up (slow, energy-dominated).** Regenerated power flows back into the DC link. If the battery cannot absorb it (BMS open, connector bounce, high ESR, full charge), the bus rises:

$$
\frac{dV_{bus}}{dt} = \frac{I_{regen} - I_{load}}{C_{bus}}
$$

## 3. Clamp Transient Formulation (mechanism a)

Once V reaches V_BR, the inductor current diverts into the TVS:

$$
L_s\frac{di}{dt} = -\left(V_{BR} + R_d\, i - V_{bus}\right)
$$

Define ΔV = V_BR − V_bus and τ = L_s / R_d. Then:

$$
i(t) = \left(I_0 + \frac{\Delta V}{R_d}\right)e^{-t/\tau} - \frac{\Delta V}{R_d}
$$

The conduction time (when i reaches 0) is:

$$
t_c = \tau \ln\!\left(1 + \frac{I_0 R_d}{\Delta V}\right)
$$

The peak clamp voltage is:

$$
V_{C,pk} = V_{BR} + R_d I_0
$$

The energy absorbed is the stored inductive energy plus the energy the bus pushes through during conduction:

$$
E_{TVS} = \tfrac{1}{2}L_s I_0^2 + V_{bus}\int_0^{t_c} i\,dt
$$

## 4. Worked Example

Assumptions: 4S pack at nominal V_bus = 14.8 V, L_s = 50 nH, I₀ = 30 A, typical V_BR = 18.8 V.

**Inputs**
- ΔV = 4.0 V
- ΔV/R_d = 12.9 A
- τ = 50 nH / 0.31 Ω ≈ 161 ns

**Conduction time**
- t_c = 161 ns × ln(42.9/12.9) ≈ **194 ns**

**Peak clamp**
- V_C,pk = 18.8 V + 0.31 Ω × 30 A ≈ **28.1 V**
- Peak power = 28.1 V × 30 A ≈ **840 W**

**Charge and energy**
- Charge: ∫i dt = 42.9 × 161 ns × 0.70 − 12.9 × 194 ns ≈ 2.33 µC
- Inductive part: ½ L I² = 22.5 µJ
- Bus-driven part: 14.8 V × 2.33 µC = 34.5 µJ
- Total: **E ≈ 57 µJ per event**

**Single-pulse check.** Peak pulse power capability scales roughly as t^(−½) for short pulses. At about 200 ns, the device can handle well over 10 kW, so a single event is safe.

## 5. Repetitive Check (the real constraint)

**If the TVS conducts on every PWM edge:**

$$
P_{avg} = E \cdot f_{sw} = 57\,\mu J \times 20\,kHz \approx 1.1\ W
$$

Across a sustained braking cycle, this violates the datasheet duty-cycle limit (about 0.01%). It also heats the junction on ordinary PCB copper, and V_BR drifts upward at about +0.1%/°C.

The TVS should not be the primary per-edge clamp. Instead:
- Minimize L_s with tight bus-cap placement.
- Add local ceramic decoupling or an RC snubber so V_spike stays below V_BR.
- Use the TVS only for rare outlier events.

## 6. Bus Pump-Up (mechanism b): the TVS Cannot Handle It

Example: 10 A regen into an open battery.
- With C_bus = 470 µF, the bus reaches V_BR in about 150 µs.
- The TVS then sinks about 10 A × 22 V ≈ **220 W continuous**.
- Thermal runaway and a shorted die follow within milliseconds.

**Required mitigations:**
- An active brake chopper or crowbar triggered at about 17 V.
- Firmware regen current limiting based on V_bus feedback.
- A minimum-SOC or battery-connected interlock for regen.

## 7. Design Flags

1. **Standoff voltage is marginal for 4S.** A full 4S pack is 16.8 V, which exceeds V_RWM = 16 V. Leakage rises, and at worst-case V_BR(min) = 17.8 V there is only 1 V of margin. Consider SMBJ18A or SMBJ20A for 4S, or keep SMBJ16A only for 3S.
2. **MOSFET rating.** The clamp reaches about 28 V at 30 A, so FETs need V_DSS ≥ 40 V (30 V parts have no margin).
3. **Validation.** Capture V_bus and I_TVS (current probe) through the braking cycle. Confirm that conduction is not occurring per PWM edge, and that T_case stays below 100 °C under worst-case regen at maximum ambient.

## Summary

The SMBJ16A safely clamps individual commutation spikes, at about 28 V peak and about 57 µJ over about 200 ns. It is thermally inadequate as a per-edge clamp at PWM frequency, and it cannot absorb regenerative bus pump-up