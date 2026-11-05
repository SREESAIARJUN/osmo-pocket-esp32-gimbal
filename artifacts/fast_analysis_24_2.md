# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic or BOM for the "STM32H743 AIO (Cycle 24)" board, so I can't confirm its actual component values. The calculations below use TPS563200 datasheet parameters and typical AIO flight-controller conditions. Substitute your board's real L, C and V_IN values where they differ.

## 1. Important input-voltage limit

The TPS563200 is rated for **V_IN = 4.5 V to 17 V**.

| Battery | V_nominal | V_full charge | OK? |
|---|---|---|---|
| 2S | 7.4 V | 8.4 V | ✅ |
| 3S | 11.1 V | 12.6 V | ✅ |
| 4S | 14.8 V | 16.8 V | ⚠️ Only 0.2 V margin. Inductive spikes from ESCs can exceed 17 V. |
| 5S/6S | 18.5–22.2 V | 21–25.2 V | ❌ Will destroy the IC |

If the board is advertised as 2–6S, the TPS563200 should sit behind a pre-regulator. Otherwise, check whether the part is actually a TPS56x2xx or a similar higher-voltage variant.

## 2. Key parameters

- Switching frequency f_sw ≈ **650 kHz** (D-CAP2, quasi-fixed frequency)
- V_FB = 0.768 V. With R2 = 10 kΩ, R1 ≈ 56.2 kΩ, which gives 5.08 V.
- Datasheet-suggested inductor for 5 V output: about 3.3–4.7 µH
- Duty cycle D = V_OUT / V_IN, efficiency ignored

## 3. Inductor ripple current

ΔI_L = (V_IN − V_OUT) · D / (L · f_sw)

| V_IN | D | ΔI_L at 3.3 µH | ΔI_L at 4.7 µH |
|---|---|---|---|
| 8.4 V (2S) | 0.595 | 0.94 A | 0.66 A |
| 12.6 V (3S) | 0.397 | 1.41 A | 0.99 A |
| 16.8 V (4S) | 0.298 | **1.64 A** | **1.15 A** |

Ripple is worst at maximum V_IN. The usual design target is 20–40 % of I_OUT, which is 0.6–1.2 A at 3 A.

- **4.7 µH** stays at about 38 % even at 16.8 V. This is the recommended choice.
- 3.3 µH gives about 55 % ripple at 4S. It works, but core losses and peak current rise.

### Peak inductor current at 3 A load and 16.8 V

- 4.7 µH: I_pk = 3 + 1.15/2 ≈ **3.58 A**
- 3.3 µH: I_pk = 3 + 1.64/2 ≈ **3.82 A**

### Inductor selection criteria

- L = 4.7 µH, ±20 %. At −20 % (3.76 µH), ripple rises to about 1.44 A and peak current to about 3.7 A.
- I_sat ≥ **4.5–5 A**, which covers the IC's current-limit region during transients and shorts.
- I_rms ≥ 3.5 A
- DCR ≤ 30 mΩ. Copper loss is 3² × 0.03 ≈ 0.27 W.
- Use a shielded or molded type to limit EMI near the gyro and magnetometer. Size 4×4 to 5×5 mm, for example XAL4040-472, IHLP-1616 4.7 µH, or Würth WE-MAPI 4.7 µH.

## 4. Output voltage ripple

ΔV_OUT ≈ ΔI_L / (8 · f_sw · C_OUT) + ΔI_L · ESR

Assumptions:
- C_OUT = 2 × 22 µF X5R 0805 or 1206
- About 30 µF effective after DC-bias derating at 5 V
- ESR ≈ 3 mΩ

| Case (16.8 V in) | Capacitive term | ESR term | Total ripple |
|---|---|---|---|
| 4.7 µH | 1.15 / (8·650k·30µ) ≈ 7.4 mV | 3.5 mV | **≈ 11 mV p-p** (0.2 %) |
| 3.3 µH | ≈ 10.5 mV | 4.9 mV | **≈ 15 mV p-p** (0.3 %) |

The datasheet limits C_OUT to roughly 20–68 µF for this L range. D-CAP2 needs a stable LC double pole, so do not add hundreds of µF of ceramics directly on the output.

There is also high-frequency ringing at the switch edges, typically 20–50 mV spikes in the tens to hundreds of MHz. It is layout-dependent and not captured by the formula above. It can be reduced with:
- A 100 nF 0402 cap at the 5 V load
- An optional ferrite bead feeding sensitive rails such as the gyro LDO, OSD video, and RX

## 5. Input capacitor ripple current

I_Cin,rms = I_OUT · √(D(1−D))

| V_IN | I_Cin,rms at 3 A |
|---|---|
| 8.4 V | 1.47 A |
| 12.6 V | 1.47 A |
| 16.8 V | 1.37 A |

The maximum is 1.5 A at D = 0.5, which corresponds to V_IN = 10 V.

Input voltage ripple at 16.8 V, with 10 µF/25 V derated to about 5 µF:

ΔV_IN ≈ I_OUT · D(1−D) / (f_sw · C_IN) = 3 × 0.209 / (650k × 5µ) ≈ **0.19 V p-p**

Recommended input capacitors:
- 2 × 10 µF/35 V X5R
- A 100 nF placed at the VIN/GND pins
- The board's main bulk electrolytic or low-ESR cap to absorb ESC spikes. This matters given the 17 V limit.

## 6. Summary

| Item | Recommended value |
|---|---|
| Inductor | 4.7 µH, I_sat ≥ 4.5 A, DCR ≤ 30 mΩ, shielded |
| ΔI_L (worst case) | ~1.15 A (38 %) |
| I_peak | ~3.6 A |
| C_OUT | 2 × 22 µF X5R + 100 nF |
| ΔV_OUT | ~11 mV p-p (plus layout-dependent spikes) |
| C_IN | 2 × 10 µF/35 V + 100 nF, ~1.5 A rms capability |
| V_IN | ≤ 4S, with TVS/bulk cap recommended |

If you can share the actual inductor part number, capacitor values, and the board's rated input range, I can recalculate for your exact hardware.