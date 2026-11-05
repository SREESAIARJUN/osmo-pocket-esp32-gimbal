# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 2)" board, so these calculations use typical flight-controller assumptions. Replace them with your actual values if they differ.

## 1. Assumptions

| Parameter | Value | Note |
|---|---|---|
| V_IN range | 7–16.8 V | 2S–4S LiPo. The TPS563200 is rated to 17 V max, so it is **not 5S/6S safe**. |
| V_IN nominal | 12 V | |
| V_OUT | 5.0 V | |
| I_OUT(max) | 3 A | |
| f_SW | 650 kHz | D-CAP2 control, pseudo-fixed frequency |
| V_REF | 0.768 V | |

**Feedback divider:** R1 = 56.2 kΩ (top), R2 = 10 kΩ (bottom).
V_OUT = 0.768 × (1 + 56.2/10) = **5.08 V**

## 2. Inductor Ripple Current

Ripple current is:

ΔI_L = V_OUT · (V_IN − V_OUT) / (V_IN · L · f_SW)

Worst case is at **maximum V_IN (16.8 V)**, where D = 5/16.8 = 0.298.

| L | V_IN = 12 V | V_IN = 16.8 V | % of 3 A (worst case) |
|---|---|---|---|
| 2.2 µH | 2.04 A | 2.46 A | 82 % (too high) |
| **3.3 µH** (TI table value for 5 V) | 1.36 A | 1.64 A | 55 % |
| **4.7 µH** | 0.95 A | 1.15 A | 38 % |

Sample calculation (3.3 µH, 16.8 V):
5 × 11.8 / (16.8 × 3.3 µH × 650 kHz) = 59 / 36.04 = **1.64 A p-p**

## 3. Inductor Current Stress (3.3 µH, worst case)

- **Peak current:** I_pk = 3 + 1.64/2 = **3.82 A**
- **RMS current:** I_rms = √(3² + 1.64²/12) = **3.04 A**
- **Valley current:** 3 − 0.82 = 2.18 A. Check this against the IC's low-side valley current limit (≈3.5–4 A) to confirm you get the full 3 A without tripping the limit.

## 4. Inductor Selection

| Spec | Requirement |
|---|---|
| Inductance | 3.3 µH (or 4.7 µH for lower ripple, at the cost of slightly slower transients) |
| I_sat | ≥ 4.5 A (use ≥ 5 A for margin over the current limit) |
| I_rms (thermal) | ≥ 3.5 A |
| DCR | ≤ 30 mΩ. Conduction loss = 3.04² × DCR ≈ 0.28 W at 30 mΩ. |
| Construction | Shielded or molded type, to limit EMI near the gyro and baro |

Example parts: Würth 744383360033 (3.3 µH), Coilcraft XAL4030-332, Bourns SRP4020TA-3R3M.

## 5. Output Voltage Ripple

Use C_OUT = 2 × 22 µF X5R/X7R (0805 or 1206, 10 V or higher).
DC-bias derating at 5 V leaves about **26 µF effective**.

- **Capacitive term:** ΔV_C = ΔI / (8 · f · C) = 1.64 / (8 × 650k × 26 µF) ≈ **12 mV**
- **ESR term:** ≈ 1.64 A × 1.5 mΩ ≈ **2.5 mV**
- **Total:** about **12–15 mV p-p** (≈0.3 % of 5 V) at 16.8 V input. About 10 mV at 12 V.

If you use the 4.7 µH inductor, output ripple drops to about **8–10 mV**.

## 6. Input Capacitor

Input RMS current is:

I_CIN(rms) = I_OUT · √(D(1−D))

- At 12 V: 3 × √(0.417 × 0.583) = **1.48 A rms**
- At 16.8 V: **1.37 A rms**

Input voltage ripple with about 5 µF effective (10 µF/25 V after derating):
ΔV_IN = I_OUT · D(1−D) / (f · C) ≈ 3 × 0.243 / (650k × 5 µF) ≈ **0.22 V p-p**

**Recommendation:**
- Use 2 × 10 µF/25 V X7R plus a 0.1 µF placed directly at VIN/GND.
- Rely on the board's bulk/ESC cap (low-ESR electrolytic) for LiPo transients.

## 7. Summary

| Item | Value |
|---|---|
| Inductor | **3.3 µH, I_sat ≥ 5 A, DCR ≤ 30 mΩ, shielded** |
| ΔI_L (worst case) | 1.64 A p-p |
| I_L peak | 3.82 A |
| C_OUT | 2 × 22 µF ceramic, giving ~12–15 mV p-p ripple |
| C_IN | 2 × 10 µF/25 V + 0.1 µF, carrying ~1.5 A rms |
| Bootstrap cap | 0.1 µF (BST to SW) |

## 8. Layout Notes for an AIO Flight Controller

- Keep the **SW node** small and away from the gyro (SPI/IMU) and the baro. The 650 kHz switching edges and their harmonics couple easily into those circuits.
- Place the input caps tight to the VIN and GND pins to keep the hot loop small.
- Route FB away from SW and the inductor.

If you share the actual input voltage range, load current, and the parts used on Cycle 2, I can redo the numbers precisely.