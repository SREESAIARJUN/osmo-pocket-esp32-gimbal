# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic or BOM for the "STM32H743 AIO (Cycle 6)" board, so I can't confirm its actual inductor, capacitors or input voltage range. The calculations below use the TPS563200 datasheet parameters and typical flight-controller inputs. Replace the assumptions with your actual values.

## Assumptions

| Parameter | Value | Note |
|---|---|---|
| V_IN | 12.6 V (3S full) to 16.8 V (4S full) | The part's absolute recommended maximum is **17 V**, so it is **not safe on 5S/6S** without a pre-regulator. |
| V_OUT | 5.0 V | |
| I_OUT | 3 A (maximum) | |
| f_SW | 650 kHz | Nominal for the D-CAP2 control mode |
| C_OUT | 2 × 22 µF X5R/X7R | Assumed about 26 µF effective after DC-bias derating at 5 V |
| ESR | about 3 mΩ | Ceramic capacitors |

## 1. Duty Cycle

D = V_OUT / V_IN

- At 16.8 V, D = 0.298.
- At 12.6 V, D = 0.397.

The worst-case inductor ripple occurs at the **maximum V_IN**.

## 2. Inductor Ripple Current

ΔI_L = (V_IN − V_OUT) · D / (L · f_SW)

| L | ΔI_L at 16.8 V | ΔI_L at 12.6 V | Ripple as % of 3 A |
|---|---|---|---|
| 2.2 µH | 2.46 A | 2.11 A | 82 % (too high) |
| 3.3 µH | 1.64 A | 1.41 A | 55 % |
| **4.7 µH** | **1.15 A** | **0.99 A** | **38 % (good)** |

The datasheet's recommended range for a 5 V output is 2.2 to 4.7 µH. **4.7 µH** keeps the ripple near the conventional 20 to 40 % target at full load.

## 3. Inductor Current Ratings (L = 4.7 µH)

- **Peak current:** I_pk = I_OUT + ΔI/2 = 3 + 0.575 ≈ **3.6 A**.
- **RMS current:** I_rms = √(I_OUT² + ΔI²/12) = √(9 + 0.11) ≈ **3.02 A**.

Because the converter uses valley current limiting, the peak during an overload is roughly the valley limit plus the full ripple.

**Inductor specification:**
- 4.7 µH ±20 %
- I_sat ≥ 4.5 to 5 A
- I_rms ≥ 3.5 A
- DCR ≤ 25 mΩ (about 0.2 W loss at 3 A)
- Shielded, molded construction (for example, the 4×4 or 5×5 mm XAL/IHLP class)

Shielding matters near the IMU, compass and barometer.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI / (8 · f · C_eff) + ΔI · ESR

- Capacitive term: 1.15 / (8 × 650 k × 26 µ) ≈ **8.5 mV**
- ESR term: 1.15 × 0.003 ≈ **3.5 mV**
- **Total ≈ 12 mVpp** at the fundamental (650 kHz)

D-CAP2 requires total C_OUT in the datasheet range (about 20 to 68 µF for 5 V). Going much above that range can affect loop stability.

## 5. Input Capacitor

- **RMS current:** I_OUT · √(D(1−D)) ≈ **1.4 A** at 16.8 V (1.5 A worst case at D = 0.5).
- **Input ripple** with 2 × 10 µF/25 V (about 5 µF effective): ΔV_IN ≈ I_OUT · D(1−D) / (f · C_eff) ≈ **0.19 Vpp**.

**Recommendation:**
- At least 2 × 10 µF, 25 V or higher, X7R, placed tight to VIN/GND.
- A 0.1 µF HF decoupling capacitor at the pin.
- Board-level bulk capacitance (a low-ESR electrolytic) to absorb ESC-induced spikes, which can easily exceed 17 V on 4S.

## 6. High-Frequency Switching Noise

The SW-node edges (a few ns) produce ringing at about 100 to 300 MHz, which typically shows up as 20 to 50 mV spikes on top of the ripple calculated above. To mitigate it:
- Minimize the VIN capacitor → IC → GND loop area.
- Keep the SW copper small.
- Optionally add a ferrite bead plus 10 µF after the output to feed sensitive rails (camera, VTX, 3.3 V LDO).

## Summary

| Item | Value |
|---|---|
| Inductor | 4.7 µH, I_sat ≥ 4.5 A, DCR ≤ 25 mΩ, shielded |
| ΔI_L (worst case) | about 1.15 A pp |
| Peak inductor current | about 3.6 A |
| Output ripple | about 12 mVpp, plus HF spikes |
| C_OUT | 2 × 22 µF ceramic |
| C_IN | 2 × 10 µF/25 V + 0.1 µF |
| Input limit | ≤ 4S only (17 V maximum) |

If you can share the actual inductor part number, capacitor values and battery cell count from the Cycle 6 board, I can rerun these numbers precisely.