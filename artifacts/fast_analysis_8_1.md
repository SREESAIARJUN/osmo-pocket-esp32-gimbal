# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have documentation for an "STM32H743 AIO (Cycle 8)" board, so I can't confirm its actual component values. The calculations below use TPS563200 datasheet parameters and typical flight-controller input voltages. Substitute your board's real values where they differ.

## 1. Key Parameters

| Parameter | Value |
|---|---|
| V_IN range | 4.5–17 V (**max 4S LiPo, 16.8 V**; do not connect 5S or 6S) |
| V_OUT | 5.0 V |
| I_OUT(max) | 3 A |
| f_SW | ~650 kHz (D-CAP2, fixed) |
| V_REF | 0.768 V |
| Feedback divider (datasheet values for 5 V) | R1 = 56.2 kΩ, R2 = 10.2 kΩ |
| Datasheet-recommended L for 5 V | 3.3 µH (range 2.2–4.7 µH) |
| Recommended C_OUT | 2 × 22 µF X5R/X7R ceramic |

## 2. Inductor Ripple Current

Ripple current is calculated as:

ΔI_L = (V_IN − V_OUT) · V_OUT / (V_IN · L · f_SW)

The duty cycle is D = V_OUT / V_IN.

| V_IN | D | ΔI_L at 3.3 µH | ΔI_L at 4.7 µH | I_peak at 3 A, 3.3 µH | I_peak at 3 A, 4.7 µH |
|---|---|---|---|---|---|
| 7.4 V (2S) | 0.676 | 0.76 A | 0.53 A | 3.38 A | 3.27 A |
| 12 V (3S nominal) | 0.417 | 1.36 A | 0.95 A | 3.68 A | 3.48 A |
| **16.8 V (4S full)** | **0.298** | **1.64 A** | **1.15 A** | **3.82 A** | **3.57 A** |

The worst case is the maximum input voltage. With 3.3 µH, the peak-to-peak ripple is about 55% of the full load current. This is within the datasheet's design target but high.

## 3. Inductor Selection

Calculate RMS current at 16.8 V with 3.3 µH:

I_RMS = √(I_OUT² + ΔI²/12) = √(9 + 0.223) ≈ **3.04 A**

**Recommended specifications:**
- **L = 3.3 µH.** Use 4.7 µH if you want lower ripple and lower peak current, at the cost of slightly slower transient response.
- **I_sat ≥ 4.5 A.** This leaves margin above the 3.8 A peak and covers the IC's current-limit excursions.
- **I_rms (thermal) ≥ 3.5 A.**
- **DCR ≤ 30 mΩ.** Copper loss is about 0.28 W at 30 mΩ.
- **Shielded construction.** This matters near the IMU and on the gyro SPI lines.
- Example parts: Würth 744311330, Coilcraft XAL4030-332, TDK SPM5030T-3R3.

## 4. Output Voltage Ripple

Output ripple is calculated as:

ΔV_OUT ≈ ΔI_L / (8 · f_SW · C_eff) + ΔI_L · ESR

Assume 2 × 22 µF ceramics. DC-bias derating at 5 V leaves roughly 30 µF effective, and ESR is about 3 mΩ.

- Capacitive term at 16.8 V: 1.64 / (8 · 650k · 30 µF) ≈ **10.5 mV**
- ESR term: 1.64 A · 3 mΩ ≈ **5 mV**
- **Total ≈ 15 mV p-p (0.3%)** at 3.3 µH
- At 4.7 µH: about 11 mV p-p

## 5. Input Capacitor

RMS ripple current is calculated as:

I_CIN(RMS) = I_OUT · √(D(1−D))

Its maximum is about **1.48 A** at 12 V input (D ≈ 0.42).

Input voltage ripple with 10 µF effective is:

ΔV_IN ≈ I_OUT · D(1−D) / (f_SW · C_IN) = 3 · 0.243 / (650k · 10 µF) ≈ **112 mV**

**Recommendations:**
- Use 2 × 10 µF 25 V X7R ceramics, rated 25 V for DC-bias margin.
- Add a 100 nF ceramic right at the VIN and GND pins.
- Rely on the board's bulk electrolytic or low-ESR capacitor on the battery rail for ESC spike suppression.

## 6. Practical Notes for a Flight Controller

- **Input voltage limit:** the 17 V absolute maximum means 4S is the ceiling. LiPo transients from ESC regeneration can exceed this, so a TVS diode such as SMBJ16A on VBAT is advisable.
- **Thermals:** about 15 W output at 3 A gives an efficiency of roughly 88–90% at 16.8 V. That means about 1.7 W of loss in a small area, so use thermal vias under the IC's GND pad.
- **Layout:** keep the SW node copper small and away from the gyro and the baro. Keep the input-cap loop (VIN → IC → GND) as tight as possible.
- **Sizing for real load:** if your actual 5 V load is servos, an FPV camera, or a receiver drawing about 2 A, the peak current drops to about 2.8 A. 3.3 µH with I_sat ≥ 4 A is then comfortably adequate.

If you can share the board's actual inductor part number, input voltage spec, or schematic, I can recompute these figures with the real values.