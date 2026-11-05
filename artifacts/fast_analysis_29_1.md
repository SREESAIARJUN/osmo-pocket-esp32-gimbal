# TPS563200 Buck Converter: Ripple and Inductor Sizing (5 V / 3 A)

**Assumptions:** I don't have the schematic or BOM for this "STM32H743 AIO (Cycle 29)" board, so this uses TPS563200 datasheet values and typical flight-controller conditions. Check every value against your actual board.

## 1. Operating Parameters

| Parameter | Value | Notes |
|---|---|---|
| V_IN range | 4.5 – 17 V (IC limit) | On a drone this suits 2S–4S. **6S (25.2 V) will destroy it.** |
| V_IN design worst case | 16.8 V (4S full charge) | Maximum ripple occurs at maximum V_IN |
| V_IN nominal | 12 V | |
| V_OUT | 5.0 V | |
| I_OUT max | 3 A | |
| f_SW | ~650 kHz | D-CAP2 control, so frequency is pseudo-fixed |

**Duty cycle:**
- D = V_OUT / V_IN
- At 16.8 V: D = 5 / 16.8 = 0.298
- At 12 V: D = 0.417

## 2. Inductor Ripple Current

ΔI_L = V_OUT × (V_IN − V_OUT) / (V_IN × L × f_SW)

| L | V_IN = 12 V | V_IN = 16.8 V | % of 3 A (worst case) |
|---|---|---|---|
| 2.2 µH | 2.04 A | 2.46 A | 82 % (too high) |
| **3.3 µH** (datasheet recommendation for 5 V) | 1.36 A | **1.64 A** | 55 % |
| 4.7 µH | 0.96 A | 1.15 A | 38 % |

D-CAP2 control needs some ripple to operate, so the 20–40 % ripple rule of thumb is flexible here. TI recommends **3.3 µH** for 5 V output, with 4.7 µH acceptable.

## 3. Inductor Current Ratings (L = 3.3 µH, worst case)

- **Peak current:** I_pk = I_OUT + ΔI/2 = 3 + 0.82 = **3.82 A**
- **RMS current:** I_rms = √(I_OUT² + ΔI²/12) = √(9 + 0.22) = **3.04 A**
- **Current-limit check:** The TPS563200 uses low-side *valley* current limiting, typically around 3.5 A (confirm the min/typ values in the datasheet). The valley current at 3 A load is 3 − 0.82 = 2.18 A, which leaves adequate margin. However, the inductor must survive the peak during an overcurrent event: valley limit + ΔI ≈ 3.5 + 1.64 ≈ 5.1 A.

**Recommended inductor specification:**
- 3.3 µH ±20 %
- I_sat ≥ 5 A
- I_rms (thermal) ≥ 3.5 A
- DCR ≤ 30 mΩ (gives about 0.28 W copper loss at 3 A)
- Shielded or molded construction, since it sits next to the gyro, baro, and magnetometer

Example parts in a 4×4 to 5×5 mm footprint:
- Würth 74438357033
- Coilcraft XGL4030-332
- TDK SPM5030 series

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI / (8 × f_SW × C_OUT) + ΔI × ESR

Assume the datasheet arrangement of 2 × 22 µF X5R/X7R ceramic capacitors (10 V rating).

| Condition | C_eff | Capacitive term | ESR term (~3 mΩ) | Total |
|---|---|---|---|---|
| Nominal 44 µF | 44 µF | 7.2 mV | ~5 mV | ~10–12 mV p-p |
| DC-bias derated (0805, 10 V at 5 V bias) | ~25–30 µF | 10.5–12.6 mV | ~5 mV | **~13–17 mV p-p** |

This is about 0.3 % of 5 V, which is fine for the MCU, receiver, and LEDs. If 5 V also feeds an analog camera or VTX, you will likely see 650 kHz noise plus spikes from ESC/motor noise. Adding a post-LC filter or a third 22 µF capacitor helps.

Keep C_OUT within the datasheet's stable range (roughly 20–68 µF). Too much capacitance can upset D-CAP2 stability.

## 5. Input Capacitor

- **RMS current:** I_Cin,rms = I_OUT × √(D(1−D)) = 3 × √(0.298 × 0.702) ≈ **1.37 A**
- **Input ripple:** ΔV_IN ≈ I_OUT × D(1−D) / (f_SW × C_IN)
  - With 10 µF: 3 × 0.209 / (650 k × 10 µ) ≈ **96 mV**
  - With 2 × 10 µF (25 V X7R, derated to about 12 µF effective): **~80 mV**

**Recommendation:** Use at least 2 × 10 µF 25 V X7R (1206 preferred) plus a 0.1 µF capacitor tight to the VIN/GND pins. The board's main bulk low-ESR electrolytic on the battery pad is still needed to absorb ESC switching spikes, which matter more than the converter's own ripple at 4S.

## 6. Summary

| Item | Value |
|---|---|
| Inductor | 3.3 µH, I_sat ≥ 5 A, I_rms ≥ 3.5 A, DCR ≤ 30 mΩ, shielded |
| Inductor ripple (worst case, 16.8 V) | 1.64 A p-p |
| Peak inductor current | 3.82 A at full load |
| Output capacitance | 2 × 22 µF 10 V X7R |
| Output ripple | ~13–17 mV p-p |
| Input capacitance | 2 × 10 µF 25 V X7R + 0.1 µF |
| Input capacitor RMS current | 1.37 A |
| Feedback divider (V_FB = 0.768 V) | R1 = 56.2 kΩ, R2 = 10.2 kΩ (datasheet values) |

If you can share the actual BOM values (L part number, capacitor values and sizes, input voltage rating of the board), I can recalculate with the real numbers.