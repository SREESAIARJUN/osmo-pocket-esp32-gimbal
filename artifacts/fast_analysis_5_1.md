# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 5)", so the numbers below use these assumptions. Swap in your actual values where they differ.

## Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_IN | 7.4–16.8 V (2S–4S LiPo) | The TPS563200 is rated 4.5–17 V, so 4S full charge has almost no margin. |
| V_OUT | 5.0 V | |
| I_OUT(max) | 3 A | |
| f_SW | ~650 kHz | Fixed-frequency D-CAP2 |
| Ripple target | 20–40 % of I_OUT | |

## 1. Inductor Ripple Current

ΔI_L = (V_IN − V_OUT) · V_OUT / (V_IN · L · f_SW)

The worst case is maximum V_IN (16.8 V), where D = 5/16.8 ≈ 0.30.

| L | ΔI_L @ 16.8 V | ΔI_L @ 12 V | % of 3 A (worst) |
|---|---|---|---|
| 2.2 µH | 2.46 A | 2.04 A | 82 % (too high) |
| 3.3 µH | 1.64 A | 1.36 A | 55 % (marginal) |
| **4.7 µH** | **1.15 A** | **0.96 A** | **38 % (recommended)** |
| 6.8 µH | 0.80 A | 0.66 A | 27 % (OK, but slower transients) |

**Choose 4.7 µH.** It sits at the top of TI's recommended range for a 5 V output.

## 2. Inductor Current Ratings

- I_peak = I_OUT + ΔI_L/2 = 3 + 0.575 ≈ **3.6 A**
- I_RMS = √(I_OUT² + ΔI_L²/12) ≈ **3.02 A**

Specify an inductor with:
- I_sat ≥ 4.5 A, so it survives current-limit events (check the datasheet current-limit value).
- I_rms ≥ 3.5 A.
- DCR ≤ 25 mΩ, giving about 0.23 W of copper loss at 3 A.
- A shielded construction (e.g. Würth WE-MAPI, Coilcraft XAL4040/XEL4030). This matters for EMI near the IMU.

## 3. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L · [ESR + 1/(8 · f_SW · C_OUT)]

Use 2 × 22 µF X5R/X7R 0805/1206 (≥10 V) caps. After DC-bias derating at 5 V, the effective capacitance is about 25 µF.

- Capacitive term: 1/(8 · 650 k · 25 µ) ≈ 7.7 mΩ
- ESR term: ≈ 3 mΩ (ceramics in parallel)
- **ΔV_OUT ≈ 1.15 A × 10.7 mΩ ≈ 12 mV p-p (~0.25 %)**

D-CAP2 relies on output ripple for stability. Keep C_OUT within TI's recommended range (about 20–68 µF) and don't stack large amounts of extra low-ESR capacitance directly on the output.

## 4. Input Capacitor

The RMS ripple current is I_OUT · √(D(1−D)):
- 1.37 A at 16.8 V
- 1.50 A at 10 V (worst case, where D ≈ 0.5)

For voltage ripple, with 2 × 10 µF 25 V caps (about 5 µF effective after bias):
ΔV_IN ≈ I_OUT · D(1−D) / (f_SW · C_IN) ≈ **~0.2 V p-p**

Recommendations:
- 2 × 10 µF 25 V X7R plus a 0.1 µF placed tight to VIN/GND pins.
- Rely on the board's bulk/TVS capacitance for battery-lead ringing.
- 35 V-rated parts are preferred because of ESC regen spikes.

## 5. Flight-Controller-Specific Notes

- **Keep the switch node small** and away from the gyro (ICM-42688/BMI270) and the OSD/analog video path. The 650 kHz fundamental and its edge harmonics can couple into video.
- If 5 V feeds analog video or a VTX, consider an LC post-filter (e.g. 1 µH + 22 µF, f_c ≈ 34 kHz), which gives about 50 dB attenuation at 650 kHz.
- **The 17 V absolute maximum is a real risk on 4S.** Inductive spikes from ESCs can exceed it, so a TVS (e.g. SMBJ15/SMAJ16A-class) on VIN is strongly advised. Never use this part on 5S/6S.
- At 3 A continuous, check thermal performance. The SOT-23-6 package needs a solid copper pour and vias on GND.

## Summary

| Item | Value |
|---|---|
| Inductor | **4.7 µH**, I_sat ≥ 4.5 A, DCR ≤ 25 mΩ, shielded |
| Inductor ripple | 0.96–1.15 A p-p |
| Peak inductor current | ~3.6 A |
| C_OUT | 2 × 22 µF X7R, ≥10 V |
| Output ripple | ~12 mV p-p |
| C_IN | 2 × 10 µF 25–35 V X7R + 0.1 µF |

If you share the actual V_IN range, the BOM parts, or the Cycle 5 schematic values, I can recompute with the exact figures.