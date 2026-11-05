# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 20)" board, so the calculations below use TPS563200 datasheet values and typical flight-controller assumptions. Check the actual L, C and input range on your board and substitute them.

## Key Parameters

| Parameter | Value | Source |
|---|---|---|
| Topology | D-CAP2, synchronous buck | Datasheet |
| Switching frequency f_sw | 580 kHz (typ) | Datasheet |
| V_IN range | 4.5–17 V | Datasheet |
| V_REF | 0.768 V | Datasheet |
| V_OUT | 5.0 V | Board spec |
| I_OUT(max) | 3 A | Board spec |
| Feedback divider | R1 ≈ 56.2 kΩ, R2 = 10 kΩ, giving 5.08 V | Datasheet recommendation |

**Input-voltage warning:** 17 V absolute recommended max means **4S LiPo maximum** (16.8 V fully charged). On a 6S AIO, this regulator must sit behind a pre-regulator. A direct 6S connection is out of spec, and a 5S connection has no margin for inductive spikes.

## 1. Duty Cycle

D = V_OUT / V_IN

| V_IN | Condition | D |
|---|---|---|
| 16.8 V | 4S full | 0.298 |
| 12.0 V | 3S nominal / 4S sagged | 0.417 |

## 2. Inductor Ripple Current

ΔI_L = (V_IN − V_OUT) · V_OUT / (V_IN · L · f_sw)

| L | V_IN = 12 V | V_IN = 16.8 V (worst case) |
|---|---|---|
| 2.2 µH | 2.29 A (76%) | 2.75 A (92%) – too high |
| **3.3 µH** (datasheet rec.) | 1.52 A (51%) | **1.84 A (61%)** |
| 4.7 µH | 1.07 A (36%) | 1.29 A (43%) |

## 3. Inductor Current Ratings

Worst case is V_IN = 16.8 V with L = 3.3 µH:

- **Peak current:** I_pk = I_OUT + ΔI_L/2 = 3 + 0.92 = **3.92 A**
- **RMS current:** I_rms = √(I_OUT² + ΔI_L²/12) = √(9 + 0.28) = **3.05 A**

**Inductor specification:**
- L = 3.3 µH (4.7 µH if you want lower ripple and can accept a slower transient response)
- I_sat ≥ 4.5–5 A. This must stay above the IC's current limit so the inductor cannot saturate during a short circuit.
- I_rms ≥ 3.5 A
- DCR ≤ 30 mΩ. At 3 A this gives about 0.27 W of copper loss.
- Use a shielded part (molded/composite) to limit EMI into the IMU, baro and compass.
- Typical footprint: 4×4 to 5×5 mm, e.g. Würth 744383560033 or Coilcraft XGL4030-332.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L · [ESR + 1/(8 · f_sw · C_OUT)]

Assumptions: 2 × 22 µF X5R ceramics, derated to about 30 µF effective at 5 V DC bias, with ESR about 3 mΩ combined.

- Capacitive term: 1/(8 × 580k × 30µ) = 7.2 mΩ, which contributes 13.2 mV
- ESR term: 1.84 A × 3 mΩ = 5.5 mV
- **Total ΔV_OUT ≈ 15–20 mV p-p (about 0.3–0.4%)**

D-CAP2 regulates on the ripple, so don't drive ripple near zero with excessive capacitance. Stay within the datasheet's 20–68 µF window.

## 5. Input Ripple

- **Input RMS current:** I_CIN,rms = I_OUT · √(D(1−D)). This is 1.48 A at 12 V and 1.37 A at 16.8 V.
- **Input ripple voltage:** ΔV_IN ≈ I_OUT · D(1−D) / (f_sw · C_IN). With 2 × 10 µF/25 V ceramics (about 8 µF effective), this is **about 157 mV p-p** at 12 V.

**Recommendation:** add a 0.1 µF 0402 capacitor directly at the VIN–GND pins for high-frequency decoupling. Keep the VIN–SW–GND hot loop under about 5 mm.

## 6. High-Frequency Switch-Node Ringing (>50 MHz)

Fast SW edges (a few ns) excite loop parasitics and typically produce 100–300 MHz ringing. On an AIO this can couple into the gyro SPI lines, the ESC current sense, or the GPS/receiver antennas.

**Mitigations:**
- Minimize the input hot-loop area. This matters most.
- Add an optional RC snubber from SW to GND, around 2.2 Ω + 470 pF. Tune it to the observed ring frequency.
- Use a shielded inductor and keep the SW copper pour small.
- Keep the switch node away from the IMU and the 5 V feedback trace.
- Consider a ferrite bead plus 10 µF on the 5 V rail feeding the RX and VTX.

## Summary

| Item | Value |
|---|---|
| Inductor | 3.3 µH, I_sat ≥ 4.5 A, DCR ≤ 30 mΩ, shielded |
| Inductor ripple current | 1.5–1.8 A p-p |
| Peak inductor current | 3.9 A |
| Output ripple | about 15–20 mV p-p (2 × 22 µF) |
| Input RMS current | about 1.5 A |
| Input ripple voltage | about 150 mV p-p (2 × 10 µF + 0.1 µF) |
| Maximum battery | 4S |

If you can share the board's actual L and C values or a schematic excerpt, I can recompute these against the real components.