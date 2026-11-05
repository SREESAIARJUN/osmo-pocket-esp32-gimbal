# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 21)" board, so the calculations below use TPS563200 datasheet values and typical flight-controller AIO operating conditions. Replace the assumed values with the actual BOM values if they differ.

## 1. Assumptions

| Parameter | Value | Note |
|---|---|---|
| V_IN (nominal) | 12 V | 3S nominal / 4S partly discharged |
| V_IN (max) | 16.8 V | 4S fully charged |
| V_OUT | 5.0 V | FB = 0.768 V, R1 = 56.2 kΩ, R2 = 10 kΩ gives about 5.08 V |
| I_OUT (max) | 3 A | |
| f_SW | 650 kHz | Fixed-frequency D-CAP2 |
| L | 3.3 µH | Datasheet recommendation for 5 V output |
| C_OUT | 2 × 22 µF X5R/X7R | |

**Input voltage warning:** the TPS563200's absolute input limit is 17 V. A full 4S pack (16.8 V) leaves almost no margin for ESC regen spikes. 5S or 6S input is not allowed. If the board is marketed as 2–6S, it must have a pre-regulator, or the part is being misapplied.

## 2. Duty Cycle

D = V_OUT / V_IN

- At 12 V: D = 0.417
- At 16.8 V: D = 0.298

## 3. Inductor Ripple Current

ΔI_L = (V_IN − V_OUT) · D / (L · f_SW)

| V_IN | L = 3.3 µH | L = 4.7 µH |
|---|---|---|
| 12 V | **1.36 A** (45 %) | 0.96 A (32 %) |
| 16.8 V | **1.64 A** (55 %) | 1.15 A (38 %) |

Worst case is at maximum V_IN. With 3.3 µH, the ripple exceeds the usual 20–40 % guideline. That is acceptable for D-CAP2, but 4.7 µH gives lower ripple and lower core loss when running on 4S.

## 4. Peak and RMS Inductor Current (worst case, 16.8 V, 3.3 µH)

- **Peak:** I_pk = I_OUT + ΔI/2 = 3 + 0.82 = **3.82 A**
- **Valley:** 3 − 0.82 = 2.18 A. This stays below the low-side valley current limit (about 3.5 A typical; check the datasheet minimum).
- **RMS:** I_rms = √(I_OUT² + ΔI²/12) = √(9 + 0.22) ≈ **3.04 A**

### Inductor selection criteria

- **Inductance:** 3.3 µH (datasheet) or 4.7 µH (preferred for 4S), ±20 %
- **Saturation current:** I_sat ≥ 4.5 A. This adds margin above the 3.82 A peak to cover current-limit transients and the inductance drop when hot.
- **Thermal current:** I_rms rating ≥ 3.5 A
- **DCR:** ≤ 30 mΩ. Conduction loss is I_rms² · DCR, which is about 0.28 W at 30 mΩ and about 0.14 W at 15 mΩ.
- **Construction:** shielded or molded (e.g., Coilcraft XAL4030/XAL4040 or Würth WE-XHMI in 4 × 4 mm). Shielding matters next to the gyro, baro, and OSD video lines on an AIO board.

## 5. Output Voltage Ripple

ΔV_OUT ≈ ΔI / (8 · f_SW · C_eff) + ΔI · ESR

Ceramic capacitors lose capacitance under DC bias. A 22 µF / 10 V 0805 part typically retains about 50–60 % at 5 V, so 2 × 22 µF gives roughly 25 µF effective.

| Case | Capacitive term | ESR term (about 1.5 mΩ) | Total |
|---|---|---|---|
| 16.8 V, 3.3 µH, C_eff = 25 µF | 12.6 mV | 2.5 mV | **≈ 15 mVpp** |
| 16.8 V, 4.7 µH, C_eff = 25 µF | 8.8 mV | 1.7 mV | **≈ 10.5 mVpp** |
| 12 V, 3.3 µH, C_eff = 25 µF | 10.5 mV | 2.0 mV | **≈ 12.5 mVpp** |

All cases are well under 1 % of 5 V. Stay within the datasheet's 20–68 µF output capacitance range for D-CAP2 stability. Do not add large low-ESR ceramic banks beyond that range without checking loop behavior.

## 6. Input Capacitor

**RMS current:** I_CIN,rms = I_OUT · √(D(1−D)) = 3 · √(0.417 × 0.583) ≈ **1.48 A** (at 12 V)

**Input ripple** with 10 µF effective:

ΔV_IN ≈ I_OUT · D(1−D) / (f_SW · C_IN) = 0.729 / (650k × 10 µF) ≈ **112 mVpp**

**Recommendation:**
- 2 × 10 µF, 25 V (or higher), X7R, placed tightly at VIN/GND
- Plus a 100 nF high-frequency decoupler
- Plus the board's bulk/low-ESR capacitor on the battery pad to absorb ESC switching spikes

## 7. Summary

| Item | Recommended value |
|---|---|
| Inductor | 3.3–4.7 µH, I_sat ≥ 4.5 A, DCR ≤ 30 mΩ, shielded |
| Worst-case ΔI_L | 1.64 A (3.3 µH) / 1.15 A (4.7 µH) |
| Peak I_L | 3.82 A (3.3 µH) |
| C_OUT | 2 × 22 µF, 10 V or higher, X7R |
| Output ripple | about 10–15 mVpp |
| C_IN | 2 × 10 µF, 25 V + 100 nF, RMS rating ≥ 1.5 A |
| Input range limit | **4S maximum** (17 V absolute) |

If you share the actual inductor part number, capacitor BOM, and battery cell count for this board, I can recompute with the exact values and check the thermal margin.