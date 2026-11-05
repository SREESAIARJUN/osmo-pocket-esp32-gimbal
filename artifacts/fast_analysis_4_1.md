# TPS563200 Buck Design: 5 V / 3 A Rail

I don't have the schematic for the "STM32H743 AIO (Cycle 4)" board, so I can't confirm the components actually fitted. Below is a first-principles design for this regulator in a typical flight-controller AIO application. Swap in your real input range and parts.

## 1. Operating Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_IN | 7.0 – 16.8 V | 2S–4S LiPo. The TPS563200 absolute max input is 17 V (see warning below). |
| V_OUT | 5.0 V | Set by the feedback divider (V_FB ≈ 0.768 V) |
| I_OUT(max) | 3 A | |
| f_SW | ~650 kHz | Fixed, D-CAP2 control |
| Ripple target | 30–40 % of I_OUT | Typical for D-CAP2 |

Worst-case inductor ripple occurs at **maximum V_IN**, so the calculations use V_IN = 16.8 V.

## 2. Duty Cycle

D = V_OUT / V_IN

- At 16.8 V: D = 5 / 16.8 = **0.298**
- At 7.0 V: D = 5 / 7.0 = **0.714**

## 3. Inductor Ripple Current

ΔI_L = V_OUT × (1 − D) / (L × f_SW)

At 16.8 V input, V_OUT(1 − D) = 5 × 0.702 = 3.51 V.

| L | ΔI_L (p-p) | % of 3 A | I_peak = I_OUT + ΔI/2 |
|---|---|---|---|
| 2.2 µH | 2.45 A | 82 % | 4.23 A |
| 3.3 µH | 1.64 A | 55 % | 3.82 A |
| **4.7 µH** | **1.15 A** | **38 %** | **3.57 A** |
| 6.8 µH | 0.79 A | 26 % | 3.40 A |

**Recommendation: 4.7 µH.** It keeps ripple in the 30–40 % range, which:
- gives D-CAP2 enough ripple signal for stable operation,
- keeps peak current comfortably below the IC's current limit, and
- limits core loss.

Before finalizing, check this value against the datasheet's recommended L/C table for 5 V output. D-CAP2 stability depends on the LC double-pole frequency, and TI lists validated combinations.

### Inductor Specification

| Parameter | Requirement |
|---|---|
| Inductance | 4.7 µH ±20 % |
| I_sat | ≥ 5 A. Covers the 3.57 A peak plus margin for current-limit transients and tolerance. |
| I_rms | ≥ 3.5 A. The RMS current is ≈ √(3² + 1.15²/12) ≈ 3.02 A. |
| DCR | ≤ 25 mΩ. Conduction loss is I_rms² × DCR ≈ 0.23 W at 25 mΩ. |
| Construction | Shielded (molded/composite), e.g. 4×4 mm or 5×5 mm. This is important near the gyro and the OSD/video lines. |

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 × f_SW × C_OUT) + ΔI_L × ESR

Assume C_OUT = 2 × 22 µF X5R/X7R ceramics. These derate to roughly 25 µF effective at 5 V DC bias.

- Capacitive term: 1.15 / (8 × 650 k × 25 µ) ≈ **8.8 mV**
- ESR term (≈ 3 mΩ): 1.15 × 0.003 ≈ **3.5 mV**
- **Total ≈ 12 mV p-p**, about 0.25 % of 5 V.

With 2.2 µH the same caps give roughly 25 mV p-p. That is still acceptable, but noisier for camera and VTX loads.

## 5. Input Ripple and Capacitors

**RMS current** through the input capacitors is I_IN,rms = I_OUT × √(D(1 − D)):
- At 16.8 V: 3 × √(0.298 × 0.702) ≈ **1.37 A**
- Worst case is D = 0.5 (V_IN = 10 V): **1.5 A**

**Input voltage ripple** with about 10 µF effective capacitance:

ΔV_IN ≈ I_OUT × D(1 − D) / (f_SW × C_IN) ≈ 0.63 / 6.5 ≈ **~95 mV p-p**

**Recommendations:**
- Use 2 × 10 µF, 25–35 V-rated X7R ceramics (0805/1206), placed right at VIN/GND.
- Add 0.1 µF for high-frequency decoupling.
- Rely on the board's main bulk capacitor (e.g. ≥ 470 µF low-ESR across the battery pads) for ESC-induced transients.

## 6. Important Caveat: 4S Input Margin

The TPS563200 is rated for 17 V absolute maximum. A fully charged 4S pack sits at 16.8 V, and ESC switching spikes on the battery bus routinely exceed 20 V. If this board accepts 4S:
- confirm it has a TVS diode and/or a pre-regulator or input filter, **or**
- consider a higher-voltage part (e.g. TPS54360 or LMR33630 class) for this rail.

Many AIO 5 V regulator failures come from exactly this issue.

## Summary

| Item | Value |
|---|---|
| Inductor | **4.7 µH, I_sat ≥ 5 A, DCR ≤ 25 mΩ, shielded** |
| Inductor ripple | ≈ 1.15 A p-p (38 %) at 16.8 V input |
| Peak inductor current | ≈ 3.6 A |
| Output caps | 2 × 22 µF ceramic, giving ≈ 12 mV p-p ripple |
| Input caps | 2 × 10 µF / 35 V ceramic plus bulk, handling ≈ 1.5 A RMS |

If you can share the board's actual input range, inductor part number, and capacitor values (or the schematic), I can check the as-built design against these numbers.