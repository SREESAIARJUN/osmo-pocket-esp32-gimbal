# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

**Caveat:** I don't have the schematic for the "STM32H743 AIO (Cycle 18)" board, so I can't confirm its actual inductor, capacitors or input voltage range. The numbers below are worked from TPS563200 datasheet parameters and typical flight-controller assumptions. Substitute your board's real values.

## Assumptions

| Parameter | Value | Note |
|---|---|---|
| V_IN | 12.6 V (3S full) and 16.8 V (4S full) | TPS563200 abs. max is 17 V |
| V_OUT | 5.0 V | |
| I_OUT(max) | 3 A | |
| f_SW | 650 kHz (typ) | Fixed-frequency D-CAP2 |
| Target ΔI_L | 30–40 % of I_OUT | 0.9–1.2 A |

**Input limit:** The 17 V maximum means this part cannot run directly from 5S or 6S. A 4S pack is the practical ceiling, and transients and regen spikes on a 4S bus can approach the limit. If the AIO advertises 2–6S input, the TPS563200 must be fed by a pre-regulator.

## 1. Duty Cycle

D = V_OUT / V_IN

- 12.6 V: D = 0.397
- 16.8 V: D = 0.298

Worst-case inductor ripple occurs at maximum V_IN.

## 2. Inductor Sizing

ΔI_L = V_OUT · (1 − D) / (L · f_SW)

Solving for L at 16.8 V with 35 % ripple (ΔI = 1.05 A):

L = 5 × 0.702 / (650 kHz × 1.05 A) ≈ **5.1 µH**

| L | ΔI_L @ 12.6 V | ΔI_L @ 16.8 V | I_peak @ 3 A, 16.8 V |
|---|---|---|---|
| 2.2 µH | 2.11 A | 2.46 A | 4.23 A |
| 3.3 µH | 1.41 A | 1.64 A | 3.82 A |
| **4.7 µH** | **0.99 A** | **1.15 A** | **3.58 A** |

**Recommendation:** Use 4.7 µH. Then cross-check against TI's recommended L/C table for 5 V output in the datasheet, because D-CAP2 stability depends on the L·C_OUT combination, not just ripple.

**Inductor specifications (4.7 µH):**
- I_sat ≥ 4.5 A. This leaves margin above the ~3.6 A peak and the IC's current-limit trip.
- I_rms ≥ 3.5 A.
- DCR ≤ 25 mΩ. At 3 A, 25 mΩ dissipates about 0.23 W.
- Shielded or molded construction (for example a 5×5 mm or 6×6 mm package) to limit EMI into the IMU, baro and compass.

## 3. Output Voltage Ripple (High-Frequency)

ΔV_OUT ≈ ΔI_L / (8 · f_SW · C_eff) + ΔI_L · ESR

Assume 2 × 22 µF X5R/X7R 0805 capacitors. With DC-bias derating at 5 V, C_eff ≈ 30 µF and ESR ≈ 3 mΩ. At 16.8 V input with 4.7 µH:

- Capacitive term: 1.15 / (8 × 650k × 30 µ) ≈ 7.4 mV
- ESR term: 1.15 × 0.003 ≈ 3.5 mV
- **Total ≈ 11 mVpp**, about 0.2 % of 5 V.

With 3.3 µH the total rises to about 15–16 mVpp. With 2.2 µH it rises to about 23 mVpp.

The real-world spike content on the 5 V rail will be higher, often 30–80 mV, due to switch-node ringing coupled through layout. Measure across the output capacitor with a short ground spring, not a long ground lead.

## 4. Input Capacitor

**RMS current:** I_rms = I_OUT · √(D(1 − D))

- 12.6 V: about 1.47 A
- 16.8 V: about 1.37 A

**Voltage ripple:** ΔV_IN ≈ I_OUT · D(1 − D) / (f_SW · C_IN)

With 10 µF / 25 V (derated to about 5 µF at 16 V), ΔV_IN ≈ 0.19 Vpp.

**Recommendation:**
- Use 2 × 10 µF / 25 V X7R placed directly at VIN–GND.
- Add a 100 nF high-frequency capacitor.
- Rely on the board's bulk/ESC bus capacitor for low-frequency energy.

## 5. Summary

| Item | Value |
|---|---|
| Inductor | 4.7 µH, I_sat ≥ 4.5 A, DCR ≤ 25 mΩ, shielded |
| Inductor ripple (worst case) | ~1.15 App |
| Peak inductor current | ~3.6 A |
| Output capacitance | ≥ 2 × 22 µF ceramic (≥ 10 V rating) |
| Output ripple (fundamental) | ~11 mVpp |
| Input capacitance | 2 × 10 µF / 25 V + 100 nF |
| Input RMS current | ~1.5 A |

If you can share the actual inductor part number, output capacitor count, and rated input range from the Cycle 18 schematic or BOM, I can recompute these numbers for your exact configuration.