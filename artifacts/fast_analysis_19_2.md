# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

**Note:** I don't have a schematic for the "STM32H743 AIO (Cycle 19)" board, so I can't confirm its actual component values or input source. The calculations below use TPS563200 datasheet parameters and the input assumptions stated. Swap in your real values if they differ.

## Assumptions

| Parameter | Value |
|---|---|
| V_IN | 12 V nominal (3S), 16.8 V max (4S full charge) |
| V_OUT | 5.0 V |
| I_OUT(max) | 3 A |
| f_SW | 650 kHz (TPS563200 fixed, D-CAP2) |
| V_REF | 0.768 V → feedback divider ≈ 56.2 kΩ / 10 kΩ (≈5.08 V) |

Duty cycle:
- D at 12 V = 5/12 = 0.417
- D at 16.8 V = 5/16.8 = 0.298

## 1. Inductor Ripple Current

ΔI_L = V_OUT·(V_IN − V_OUT) / (V_IN · L · f_SW)

| L | ΔI_L @ 12 V | ΔI_L @ 16.8 V | % of 3 A (worst case) |
|---|---|---|---|
| 2.2 µH | 2.04 A | 2.46 A | 82 % (too high) |
| 3.3 µH | 1.36 A | 1.64 A | 55 % |
| **4.7 µH** | **0.95 A** | **1.15 A** | **38 % ✔** |

An ideal 30 % ripple target at 16.8 V would need about 6 µH. However, the datasheet's recommended range for 5 V output tops out around 4.7 µH, and D-CAP2 needs some ripple to regulate stably. **4.7 µH is the best choice.**

## 2. Inductor Current Ratings (L = 4.7 µH, worst case 16.8 V)

- **Peak current:** I_pk = 3 + 1.15/2 = **3.57 A**
- **RMS current:** I_rms = √(3² + 1.15²/12) = **3.02 A**

Selection criteria:
- **I_sat ≥ 4.5–5 A.** This keeps the inductor out of saturation when the IC's current limit trips during startup or a short.
- **I_rms (thermal) ≥ 3.5 A.**
- **DCR ≤ 30 mΩ.** This gives roughly 0.27 W of copper loss at 3 A.
- Use a shielded part (e.g., a 5×5 mm or 6×6 mm molded inductor). This matters near the IMU, baro, and compass on an AIO board.

## 3. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8·f_SW·C_OUT) + ΔI_L·ESR

Assume 2 × 22 µF X5R/X7R ceramics, 10 V rated. DC-bias derating at 5 V leaves roughly 30 µF effective, with about 3 mΩ ESR.

- Capacitive term: 1.15 / (8 × 650k × 30 µF) = **7.4 mV**
- ESR term: 1.15 × 0.003 = **3.5 mV**
- **Total: about 11 mV p-p (≈0.2 %)** at the 16.8 V worst case, and about 9 mV at 12 V.

Keep C_OUT within the datasheet's roughly 20–68 µF window. Much more than that can upset D-CAP2 loop stability.

## 4. Input Capacitor

- **RMS current:** I_Cin,rms = I_OUT·√(D(1−D)) ≈ 3 × 0.493 = **1.48 A** at 12 V. The maximum is 1.5 A at D = 0.5.
- **Input ripple** (10 µF effective): ΔV_IN = I_OUT·D(1−D)/(f·C_IN) = 3 × 0.243 / (650k × 10 µF) ≈ **112 mV p-p**.
- **Recommendation:** 2 × 10 µF, 25 V X7R plus 0.1 µF, placed tight to the VIN and GND pins.

## Summary

| Item | Recommended |
|---|---|
| Inductor | 4.7 µH, I_sat ≥ 5 A, I_rms ≥ 3.5 A, DCR ≤ 30 mΩ, shielded |
| Output caps | 2 × 22 µF 10 V X7R |
| Input caps | 2 × 10 µF 25 V X7R + 0.1 µF |
| Inductor ripple | ~0.95 A (12 V) / 1.15 A (16.8 V) |
| Output ripple | ~9–11 mV p-p |

## Design Warning

The TPS563200's absolute maximum V_IN is 17 V (some revisions list 19 V abs max, with 17 V recommended). A fully charged 4S pack is 16.8 V, and ESC regenerative spikes on a flight controller's battery rail can easily exceed that. If this board is meant to accept 4S:
- add a TVS diode and bulk capacitance on V_BAT, or
- consider a higher-voltage part such as the TPS54331 or LMR33630.

Please verify the inductor range against the TPS563200 datasheet table (SLVSCS9) and check the actual board's input specification.