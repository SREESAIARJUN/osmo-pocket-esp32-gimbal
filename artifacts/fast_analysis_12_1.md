# TPS563200 5 V / 3 A Buck: Inductor Sizing and Switching Ripple

I don't have the schematic for the "STM32H743 AIO (Cycle 12)" board, so these figures use TPS563200 datasheet parameters and typical flight-controller AIO assumptions. Replace the assumptions with your actual values, especially V_IN(max) and the real capacitor part numbers.

## 1. Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_IN | 12 V nominal, 16.8 V max | 4S LiPo. The TPS563200 is rated to 17 V max, so do not use it on 5S/6S. |
| V_OUT | 5.0 V | |
| I_OUT(max) | 3 A | |
| f_SW | 650 kHz | Fixed, D-CAP2 control |
| V_REF | 0.768 V | |
| C_OUT | 2 × 22 µF X5R/X7R ceramic | About 26 µF effective after DC-bias derating at 5 V |
| ESR (combined) | About 3 mΩ | |

## 2. Feedback Divider

V_OUT = 0.768 V × (1 + R1/R2)

With R2 = 10 kΩ, the ideal R1 is 55.1 kΩ. The nearest standard value, **R1 = 54.9 kΩ**, gives V_OUT ≈ 4.98 V.

## 3. Duty Cycle

D = V_OUT / V_IN

- At 12 V: D = 0.417
- At 16.8 V: D = 0.298

## 4. Inductor Ripple Current

ΔI_L = (V_IN − V_OUT) × D / (L × f_SW)

| L | ΔI_L @ 12 V | ΔI_L @ 16.8 V (worst case) | Ripple at 3 A |
|---|---|---|---|
| 2.2 µH | 2.04 A | 2.46 A | 68–82 % (too high) |
| 3.3 µH | 1.36 A | 1.64 A | 45–55 % |
| **4.7 µH** | **0.96 A** | **1.15 A** | **32–38 % (recommended)** |

**Recommendation: 4.7 µH.** It keeps ripple near the 30–40 % target at the worst-case input. 3.3 µH, the datasheet's typical value for 5 V, also works, but it costs more output ripple and a higher peak current.

## 5. Inductor Current Ratings (4.7 µH, worst case at 16.8 V)

- **Peak current:** I_pk = 3 + 1.15/2 = **3.58 A**
- **RMS current:** I_rms = √(3² + 1.15²/12) = **3.02 A**
- **Required ratings:** I_sat ≥ 4.5 A (margin above the roughly 4 A current-limit region) and I_rms ≥ 3.5 A
- **Copper loss:** with DCR ≈ 20 mΩ, about 0.18 W

Suitable parts include:
- Würth 744 383 560 47 (4.7 µH)
- Coilcraft XGL4030-472
- TDK SPM5030T-4R7M (pick a size that meets I_sat)

Use a shielded inductor. The board likely sits near gyros, the magnetometer, and RF.

## 6. Output Voltage Ripple (switching frequency)

ΔV_OUT ≈ ΔI_L / (8 × f_SW × C_OUT) + ΔI_L × ESR

At 16.8 V with 4.7 µH (ΔI_L = 1.15 A):
- Capacitive term: 1.15 / (8 × 650 kHz × 26 µF) = 8.5 mV
- ESR term: 1.15 A × 3 mΩ = 3.5 mV
- **Total: ≈ 12 mV p-p, about 0.24 %**

At 16.8 V with 3.3 µH: **≈ 17 mV p-p**

The TPS563200 uses Eco-mode (pulse skipping) at light load. Below roughly 0.3–0.5 A, the ripple frequency drops and the amplitude can rise to about 20–30 mV. If that matters for analog or video loads, consider the TPS563201, which runs in forced continuous mode.

## 7. Input Capacitor (12 V)

- **RMS current:** I_CIN,rms = 3 × √(0.417 × 0.583) ≈ **1.48 A**
- **Input ripple:** ΔV_IN = 3 × 0.243 / (650 kHz × 10 µF effective) ≈ **112 mV p-p**

Recommendation:
- Use 2 × 10 µF, 25 V X7R plus a 100 nF ceramic placed close to the VIN and GND pins.
- Add bulk capacitance at the battery pads (a low-ESR electrolytic of at least 470 µF) to clamp ESC-induced spikes. The 17 V absolute maximum leaves very little headroom on 4S.

## Summary

| Item | Value |
|---|---|
| Inductor | 4.7 µH, I_sat ≥ 4.5 A, shielded, DCR ≤ 25 mΩ |
| Ripple current | 0.96 A at 12 V, 1.15 A at 16.8 V |
| Output ripple | ≈ 12 mV p-p (CCM, full load) |
| C_OUT | 2 × 22 µF, 10 V or higher, X5R/X7R |
| C_IN | 2 × 10 µF, 25 V + 100 nF |
| Feedback divider | R1 = 54.9 kΩ, R2 = 10 kΩ |

If you can share the actual Cycle 12 schematic values (inductor part number, capacitors, and whether the board is 6S-rated), I can recompute with those. Note that a 6S rating would rule out the TPS563200 entirely.