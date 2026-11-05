# TPS563200 Buck Regulator: Ripple and Inductor Sizing (5 V / 3 A)

**I don't have the schematic or BOM for the "STM32H743 AIO (Cycle 16)" board.** The figures below use TPS563200 datasheet parameters and typical flight-controller assumptions. Replace them with your board's actual input range, inductor and capacitor values.

## Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_IN | 12 V nominal, 16.8 V max | 2–4S LiPo. The TPS563200 is rated to 17 V max, so it can't run directly on 5S/6S. |
| V_OUT | 5.0 V | |
| I_OUT | 3 A max | |
| f_SW | ~650 kHz | D-CAP2 control, pseudo-fixed frequency |
| L | 3.3 µH | Typical datasheet recommendation for 5 V out. Verify against the datasheet table. |
| C_OUT | 2 × 22 µF X5R/X7R | About 30 µF effective after DC-bias derating at 5 V |
| C_IN | 10 µF ceramic | About 10 µF effective |

## 1. Duty Cycle

D = V_OUT / V_IN

- At 12 V: D ≈ 0.417
- At 16.8 V: D ≈ 0.298

## 2. Inductor Ripple Current

ΔI_L = (V_IN − V_OUT) · D / (L · f_SW)

| V_IN | ΔI_L (3.3 µH) | ΔI_L (4.7 µH) |
|---|---|---|
| 12 V | 1.36 A (45 % of I_OUT) | 0.96 A |
| 16.8 V | **1.64 A (55 %)** | 1.15 A (38 %) |

Ripple is highest at maximum V_IN, so size for that case. The usual target is a ripple ratio of 20–40 %. With 3.3 µH at 4S full charge, ripple is on the high side but still works with D-CAP2. A 4.7 µH inductor brings it into the ideal range at the cost of slightly slower transient response.

## 3. Inductor Current Ratings (3.3 µH, worst case)

- **Peak:** I_pk = I_OUT + ΔI_L/2 = 3 + 0.82 = **3.82 A**
- **RMS:** I_rms = √(I_OUT² + ΔI_L²/12) = √(9 + 0.22) ≈ **3.04 A**
- **Saturation rating:** I_sat ≥ ~4.5 A. This covers the peak plus margin and also current-limit excursions during a short or load step.

Also check the peak against the TPS563200's valley/peak current-limit spec. A large ripple reduces the usable DC output current before limit.

**Recommended inductor:** 3.3–4.7 µH, I_sat ≥ 4.5 A, I_rms ≥ 3.5 A, DCR ≤ 30 mΩ. Shielded parts suit 4×4 to 5×5 mm footprints, for example the Würth WE-XHMI, Coilcraft XAL4030 and TDK SPM series.

DCR loss: 3.04² × 0.025 Ω ≈ 0.23 W.

## 4. Output Voltage Ripple (high-frequency switching component)

ΔV_OUT ≈ ΔI_L / (8 · f_SW · C_OUT) + ΔI_L · ESR

- Capacitive term: 1.64 / (8 × 650 k × 30 µ) ≈ **10.5 mV**
- ESR term (about 3 mΩ for parallel ceramics): ≈ 5 mV
- **Total ≈ 12–16 mV p-p**, about 0.3 % of 5 V

With a 4.7 µH inductor this drops to roughly 9–11 mV p-p.

ESL spikes at the switching edges (often 50–150 mV, in the 100 MHz+ range) usually dominate on the scope. To reduce them:
- Add a 100 nF 0402 capacitor close to the load.
- Optionally add a ferrite bead and 10 µF capacitor before sensitive loads such as the gyro, OSD and receiver.
- When measuring, use the scope probe's ground-spring tip, not the long ground lead.

## 5. Input Ripple

- **RMS current in C_IN:** I_OUT · √(D(1−D)) = 3 × √(0.298 × 0.702) ≈ **1.37 A**. At 12 V it is about 1.48 A, the maximum near D = 0.5.
- **Voltage ripple:** ΔV_IN ≈ I_OUT · D(1−D) / (f_SW · C_IN) = 0.63 / 6.5 ≈ **~95 mV p-p** with 10 µF effective.

Use at least 10 µF rated 25 V, plus a 100 nF capacitor tight to the VIN/GND pins. A 25 V rating loses significant capacitance at 16.8 V bias, so 2 × 10 µF is better. On a flight controller, ESC switching transients on VBAT matter more than the regulator's own ripple. Bulk capacitance at the main battery input and TVS protection help keep V_IN below the 17 V abs-max.

## Summary

| Item | Result (3.3 µH, worst case 16.8 V) |
|---|---|
| Inductor ripple | 1.64 A p-p |
| Peak inductor current | 3.82 A, so choose I_sat ≥ 4.5 A |
| Output switching ripple | ~12–16 mV p-p |
| Input capacitor RMS current | ~1.4–1.5 A |
| Input ripple | ~95 mV p-p (10 µF effective) |

If you share the actual L, C_OUT, C_IN part numbers and battery cell count from the Cycle 16 design, I can redo these numbers with real derated values.