# TPS563200 Buck Design: 5 V / 3 A Rail on an STM32H743 AIO Board

**Assumption:** I don't have the specific "Cycle 1" schematic, so this uses typical flight-controller AIO conditions. Replace them with your actual BOM values where they differ.

## 1. Design Parameters

| Parameter | Value | Notes |
|---|---|---|
| V_IN range | 7.0 – 16.8 V | 2S–4S LiPo. The IC's absolute max is 17 V, so 4S has almost no margin. |
| V_OUT | 5.0 V | |
| I_OUT(max) | 3.0 A | |
| f_SW | 650 kHz | TPS563200 nominal (D-CAP2, pseudo-fixed frequency) |
| V_REF | 0.768 V | |
| Ripple target | 30–40 % of I_OUT | 0.9–1.2 A |

## 2. Duty Cycle

D = V_OUT / V_IN

| V_IN | D |
|---|---|
| 7.4 V (2S nominal) | 0.676 |
| 12.6 V (3S full) | 0.397 |
| 16.8 V (4S full, worst case for ripple) | 0.298 |

## 3. Inductor Sizing

The required inductance is:

L = V_OUT·(1 − D) / (ΔI_L · f_SW)

At worst case (V_IN = 16.8 V), V_OUT·(1 − D) = 5 × 0.702 = 3.51 V:

- For 40 % ripple (1.2 A): L = 3.51 / (1.2 × 650 k) = **4.5 µH**
- For 30 % ripple (0.9 A): L = 3.51 / (0.9 × 650 k) = **6.0 µH**

**Selected value: 4.7 µH.** This is a standard value and fits within the datasheet's recommended range for a 5 V output.

### Inductor Ripple Current with 4.7 µH

ΔI_L = V_OUT·(1 − D) / (L · f_SW), with L · f_SW = 3.055 V/A.

| V_IN | ΔI_L (p-p) | % of 3 A | I_peak |
|---|---|---|---|
| 7.4 V | 0.53 A | 18 % | 3.27 A |
| 12.6 V | 0.99 A | 33 % | 3.49 A |
| 16.8 V | **1.15 A** | **38 %** | **3.57 A** |

For comparison, 3.3 µH would give 1.64 A p-p (55 %) at 16.8 V. That raises output ripple and pushes the peak current toward the IC's current limit.

### Inductor Ratings

- **RMS current:** I_RMS = √(I_OUT² + ΔI²/12) = √(9 + 0.11) ≈ **3.02 A**
- **Saturation current:** I_sat ≥ 3.57 A peak plus 20–25 % margin, so **≥ 4.5 A**
- **DCR:** ≤ 25 mΩ. At 20 mΩ, copper loss = 3.02² × 0.02 ≈ **0.18 W**
- **Construction:** shielded, to limit EMI near the gyro and OSD/video lines

Example parts (verify current stock): Würth 74438357047, Coilcraft XGL5030-472, TDK SPM6530T-4R7.

## 4. Output Capacitor and Voltage Ripple

Assume 2 × 22 µF X5R/X7R, 0805/1206, rated 10 V or higher. Ceramic capacitance drops under DC bias, so at 5 V the effective value is about 30 µF.

Capacitive component:
ΔV_C = ΔI / (8 · f · C) = 1.15 / (8 × 650 k × 30 µ) ≈ **7.4 mV**

ESR component (about 3 mΩ for the two caps in parallel):
ΔV_ESR = 1.15 × 0.003 ≈ **3.5 mV**

**Total ripple ≈ 10–11 mV p-p (about 0.2 %)** at worst case. This is well within limits for the 5 V rail feeding receivers, VTX, and LEDs. The TPS563200's D-CAP2 control is designed for all-ceramic output capacitors, so no ESR is needed for stability.

## 5. Input Capacitor

Input RMS ripple current peaks at D = 0.5 (V_IN = 10 V):
I_CIN(RMS) = I_OUT · √(D(1 − D)) = 3 × 0.5 = **1.5 A**

Input voltage ripple, assuming 10 µF that derates to about 5 µF effective at 16 V:
ΔV_IN = I_OUT · D(1 − D) / (f · C) = 0.75 / (650 k × 5 µ) ≈ **0.23 V p-p**

**Recommendations:**
- Use 2 × 10 µF, 25 V or higher (ideally 35 V), X7R, placed tight to the VIN and GND pins.
- Add a 0.1 µF high-frequency decoupling cap at the pins.
- Keep a bulk low-ESR electrolytic on the battery pads. ESC switching spikes on a 4S pack can easily exceed the 17 V absolute max.

## 6. Feedback Divider

V_OUT = 0.768 × (1 + R1/R2)

With R1 = 56.2 kΩ and R2 = 10 kΩ: V_OUT = **5.08 V**. That slight overshoot is common practice to offset cable and trace drop.

## 7. Design Notes and Risks

1. **4S headroom:** a 16.8 V pack plus transients is right at the 17 V absolute max. Consider a TVS diode (for example SMBJ16A) or a higher-voltage part such as the TPS54360 or LMR33630 if 4S operation is required.
2. **2S dropout:** at a sagging 2S pack (about 6.4 V), the minimum off-time limits regulation. Expect the 5 V rail to droop under load.
3. **Thermal:** at 3 A continuous in the SOT-23-6 package, use wide copper pours and thermal vias. Efficiency is about 88–90 %, which means roughly 1.7–2 W total loss.
4. **Layout:** keep the SW node small and away from the MPU6000/ICM gyro, the baro, and the analog OSD. Keep the VIN–C_IN–GND loop as small as possible.

## Summary

| Item | Value |
|---|---|
| Inductor | **4.7 µH**, I_sat ≥ 4.5 A, DCR ≤ 25 mΩ, shielded |
| Worst-case ΔI_L | 1.15 A p-p (38 %) |
| Peak inductor current | 3.57 A |
| C_OUT | 2 × 22 µF X7R ≥ 10 V |
| Output ripple | ~10 mV p-p |
| C_IN | 2 × 10 µF X7R ≥ 25 V, plus 0.1 µF |
| Feedback divider | 56.2 kΩ / 10 kΩ, giving 5.08 V |

If you can share the actual Cycle 1 BOM (inductor part number, capacitor values, and input voltage spec), I can redo these numbers against the real components.