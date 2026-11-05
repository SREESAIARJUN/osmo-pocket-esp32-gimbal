# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 17)" board, so these numbers are based on the TPS563200 datasheet and typical flight-controller AIO conditions. Substitute your actual L, C, and input-voltage values if they differ.

## Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_IN | 7.4 V to 16.8 V | 2S to 4S LiPo. The IC's absolute max is 17 V, so **4S is the ceiling**. |
| V_OUT | 5.0 V | V_REF = 0.768 V. Example divider: R1 = 56.2 kΩ, R2 = 10.2 kΩ. |
| I_OUT | 3 A | Maximum rated load |
| f_SW | 650 kHz | D-CAP2 control, so frequency varies somewhat |
| L | 3.3 µH | TI-recommended value for 5 V out |
| C_OUT | 2 × 22 µF X5R | About 22 µF effective after DC-bias derating |

## 1. Duty cycle

D = V_OUT / V_IN

| V_IN | D |
|---|---|
| 16.8 V (4S full) | 0.298 |
| 12 V | 0.417 |
| 7.4 V (2S nominal) | 0.676 |

## 2. Inductor ripple current

ΔI_L = V_OUT · (1 − D) / (L · f_SW), where L · f_SW = 3.3 µH × 650 kHz = 2.145

| V_IN | ΔI_L (p-p) | I_peak = I_OUT + ΔI/2 |
|---|---|---|
| 16.8 V | **1.64 A** (55% of I_OUT) | **3.82 A** |
| 12 V | 1.36 A | 3.68 A |
| 7.4 V | 0.76 A | 3.38 A |

The worst case is at maximum input voltage. A ripple of 55% is high but within TI's guidance for D-CAP2, which needs some ripple for stable operation.

## 3. Inductor sizing

- **Inductance:** 3.3 µH. Using 2.2 µH would raise ripple to about 2.5 A p-p and peak current to about 4.2 A. Using 4.7 µH would slow transient response.
- **RMS current:** I_RMS = √(I_OUT² + ΔI²/12) = √(9 + 0.224) ≈ **3.04 A**
- **Saturation current:** I_sat should be at least 4.5 A. This covers the 3.82 A peak plus margin for current-limit excursions and load transients.
- **DCR:** 30 mΩ or less. At 30 mΩ, conduction loss is 3.04² × 0.03 ≈ 0.28 W.
- **Example parts:** Würth 744383560033, Coilcraft XAL4030-332, TDK SPM5030-3R3. These are shielded types, which matter near the IMU and the compass.

## 4. Output voltage ripple

Capacitive component:
ΔV_C = ΔI / (8 · f · C) = 1.64 / (8 × 650k × 22 µF) ≈ **14.3 mV**

ESR component (about 3 mΩ for ceramics):
ΔV_ESR ≈ 1.64 × 0.003 ≈ 5 mV

**Total ripple is about 15 to 20 mV p-p (0.3 to 0.4%) worst case.**

- At 12 V input this drops to about 12 mV.
- Adding a third 22 µF capacitor brings it under 10 mV.
- Expect additional high-frequency switching spikes (tens of MHz) from layout parasitics. These are not captured by the formula above. A small 100 nF capacitor, or a ferrite bead plus capacitor to the MCU and sensor rails, helps suppress them.

## 5. Input capacitor

RMS current: I_CIN = I_OUT · √(D(1−D))
- Maximum is **1.5 A** at D = 0.5 (V_IN = 10 V)
- About 1.37 A at 16.8 V

Input voltage ripple: ΔV_IN = I_OUT · D(1−D) / (f · C_IN)
- About 96 mV with 10 µF effective
- About 190 mV if the capacitor derates to 5 µF at 16 V

**Recommendation:** 2 × 10 µF 25 V X7R (0805 or 1206) placed tight to VIN/GND, plus a 100 nF high-frequency decoupler. The board's main bulk low-ESR electrolytic absorbs ESC-induced spikes. This is critical because 4S spikes can exceed the 17 V absolute max.

## 6. Notes and caveats

- **2S dropout:** Minimum off-time limits the maximum duty cycle to about 75 to 80%. Expect regulation to fail below roughly 6.3 to 6.5 V input, so a sagging 2S pack will brown out the 5 V rail.
- **Current limit:** The datasheet low-side valley limit is around 3.5 to 4 A typical. Continuous 3 A at 4S operates close to that limit, so derate to about 2.5 A for margin.
- **Thermal:** At 3 A out, efficiency is about 88 to 90%, giving roughly 1.7 to 2 W of total loss. On a dense AIO board, add copper pour and thermal vias under the SOT-23-6 package.

If you can share the actual inductor part number, the output capacitor values, and the battery cell count from the Cycle 17 design, I can recalculate these with exact figures.