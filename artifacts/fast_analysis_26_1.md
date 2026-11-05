# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 26)" board, so the numbers below are based on the **TPS563200 datasheet** and typical flight-controller AIO conditions. Replace the assumptions with your actual values if they differ.

## 1. Assumptions

| Parameter | Value | Note |
|---|---|---|
| V_IN range | 7.4 V (2S) to 16.8 V (4S full) | The IC's maximum V_IN is **17 V**, so it is a 2–4S part only |
| V_IN nominal | 14.8 V (4S) | |
| V_OUT | 5.0 V | |
| I_OUT max | 3 A | |
| f_SW | 650 kHz | Fixed by the TPS563200 (D-CAP2) |
| V_REF | 0.768 V | |

## 2. Duty Cycle

D = V_OUT / V_IN

- At 16.8 V: D = **0.298**
- At 14.8 V: D = **0.338**
- At 7.4 V: D = **0.676**

## 3. Inductor Ripple Current

ΔI_L = V_OUT · (V_IN − V_OUT) / (V_IN · L · f_SW)

| V_IN | L = 3.3 µH | **L = 4.7 µH** |
|---|---|---|
| 7.4 V | 0.76 A | 0.53 A |
| 14.8 V | 1.54 A | 1.08 A |
| 16.8 V (worst case) | 1.64 A (55 %) | **1.15 A (38 %)** |

**Recommendation: 4.7 µH.** It sits within the datasheet's recommended 3.3–4.7 µH range for 5 V output and keeps ripple near the 30–40 % sweet spot at maximum V_IN.

## 4. Inductor Current Ratings (L = 4.7 µH, worst case 16.8 V)

- **Peak:** I_pk = 3 + 1.15/2 = **3.58 A**
- **RMS:** I_rms = √(3² + 1.15²/12) = **3.02 A**
- **Current-limit consideration:** The TPS563200 uses valley current limiting (about 3.5 A typical low-side). During overload, the peak can reach roughly 3.5 + 1.15 ≈ **4.7 A**.

**Specify:**
- I_sat ≥ 5 A
- I_rms (thermal) ≥ 3.5 A
- DCR ≤ 25 mΩ (gives about 0.23 W conduction loss at 3 A)

Shielded parts fit the AIO footprint well, for example:
- Würth 744383560047 (≈5 A sat)
- Coilcraft XGL4030-472
- Bourns SRP4020TA-4R7M

Use a shielded part because of the nearby IMU and magnetometer.

## 5. Output Voltage Ripple

Use C_OUT = 2 × 22 µF X5R/X7R 0805/1206 ceramics. Because of DC-bias derating at 5 V, assume about **30 µF effective**.

- **Capacitive term:** ΔV_C = ΔI_L / (8 · f_SW · C_OUT) = 1.15 / (8 × 650k × 30 µF) ≈ **7.4 mV**
- **ESR term:** ΔV_ESR = ΔI_L × ESR = 1.15 A × 1.5 mΩ ≈ **1.7 mV**
- **Total switching ripple: about 9 mVpp** (under 0.2 % of 5 V)

D-CAP2 has internal ripple injection, so all-ceramic output is stable. Stay within the datasheet's 20–68 µF range. Don't load the 5 V rail with large bulk capacitance, which can upset the D-CAP2 loop.

## 6. Input Capacitor

- **RMS current:** I_CIN,rms = I_OUT · √(D(1−D)) ≈ **1.42 A** at 14.8 V. The maximum is 1.5 A at D = 0.5.
- **Input ripple:** with 2 × 10 µF 25 V (about 20 µF effective is optimistic; derate to roughly 8–10 µF real at 16 V), ΔV_IN ≈ I_OUT · D(1−D) / (f_SW · C_IN) ≈ **50–100 mVpp** at the IC pins.

**Use 2 × 10 µF / 25 V X7R** placed directly at VIN/GND, plus a 0.1 µF high-frequency decoupler.

## 7. Feedback Divider (5 V)

V_OUT = 0.768 · (1 + R1/R2)

With R1 = 56.2 kΩ and R2 = 10 kΩ, V_OUT = **5.08 V**. This matches the datasheet table.

## 8. Design Warnings for a Flight Controller AIO

1. **17 V absolute maximum.** A full 4S pack (16.8 V) plus ESC regenerative/braking spikes will exceed it. Add a TVS diode (for example SMBJ15A or SMF16A) and a low-ESR bulk capacitor on the battery input. If the board is advertised as 6S-capable, the TPS563200 needs a pre-regulator.
2. **ESC noise.** Motor PWM transients couple into V_IN. A small ferrite or π-filter ahead of the regulator improves the 5 V rail for the gyro and receiver.
3. **Layout.** Keep the SW node copper small and away from the IMU. Make the input-capacitor loop (VIN → C_IN → GND) as tight as possible.

## Summary

| Item | Value |
|---|---|
| Inductor | **4.7 µH**, I_sat ≥ 5 A, DCR ≤ 25 mΩ, shielded |
| ΔI_L (worst case) | 1.15 A pp |
| Peak I_L | 3.58 A (about 4.7 A in current limit) |
| C_OUT | 2 × 22 µF ceramic |
| Output ripple | about 9 mVpp |
| C_IN | 2 × 10 µF / 25 V + 0.1 µF, I_rms ≈ 1.5 A |

If you share the actual board's input voltage range, component values, or schematic, I can recalculate with exact figures.