# TPS563200 (5 V / 3 A) Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 10)" board, so the calculations below use the TPS563200's datasheet characteristics and typical flight-controller AIO input conditions. Substitute your actual BOM values where noted.

## Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_IN (worst case) | 16.8 V | 4S LiPo fully charged. The TPS563200 is rated to 17 V max, so it is marginal on 4S and unsuitable for 5S/6S. |
| V_IN (low case) | 7.4 V | 2S nominal |
| V_OUT | 5.0 V | |
| I_OUT | 3.0 A | |
| f_SW | 650 kHz | Fixed, D-CAP2 control |
| Ripple target | 30–40 % of I_OUT | |

Inductor ripple is worst at maximum V_IN, so size the inductor at 16.8 V.

## 1. Duty Cycle

D = V_OUT / V_IN = 5 / 16.8 = **0.298** (at 16.8 V)
D = 5 / 7.4 = **0.676** (at 7.4 V)

## 2. Inductor Ripple Current

ΔI_L = V_OUT × (1 − D) / (L × f_SW) = 3.51 V / (L × 650 kHz)

| L | ΔI_L (p-p) | % of 3 A | Peak current |
|---|---|---|---|
| 2.2 µH | 2.45 A | 82 % | 4.23 A |
| 3.3 µH | 1.64 A | 55 % | 3.82 A |
| **4.7 µH** | **1.15 A** | **38 %** | **3.57 A** |
| 6.8 µH | 0.79 A | 26 % | 3.40 A |

The inductance needed for a given ripple target is:

L = 3.51 / (650 kHz × ΔI_target)

- For 40 % ripple (1.2 A): L = 4.5 µH
- For 30 % ripple (0.9 A): L = 6.0 µH

**Recommendation: 4.7 µH.** At 2S input the ripple drops to about 0.53 A p-p.

## 3. Inductor Specifications

- **Saturation current:** at least 5 A. The peak is 3.57 A, but the inductor must also survive the IC's current-limit events (valley limit around 4 A) without saturating.
- **RMS rating:** at least 3.5 A.
- **DCR:** 20 mΩ or less. At 3 A this keeps conduction loss to about 0.18 W.
- **Construction:** shielded or molded types (e.g., Würth XGL/XAL series or Coilcraft XAL5030-472) to limit EMI near the IMU and gyro.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 × f_SW × C_OUT) + ΔI_L × ESR

With 2 × 22 µF X5R/X7R ceramics (about 30 µF effective after DC-bias derating at 5 V) and 3 mΩ combined ESR:

- Capacitive term: 1.15 / (8 × 650k × 30 µF) = **7.4 mV**
- ESR term: 1.15 A × 3 mΩ = **3.4 mV**
- **Total: about 10–11 mV p-p**, which is about 0.2 % of V_OUT.

In practice, layout-induced switching spikes of 20–50 mV are common. A 0.1 µF capacitor at the load and a short switch-node loop help reduce them.

## 5. LC Stability Check (D-CAP2)

f₀ = 1 / (2π√(4.7 µH × 44 µF)) ≈ **11 kHz**

This is well below f_SW. Still, check your L/C_OUT combination against the recommended-component table in the TPS563200 datasheet. D-CAP2 stability depends on staying within those ranges, and excessive C_OUT can cause jitter.

## 6. Input Capacitor RMS Current

I_CIN,rms = I_OUT × √(D(1 − D)) = 3 × √(0.298 × 0.702) ≈ **1.37 A**

The theoretical maximum is 1.5 A, at D = 0.5. Use at least 2 × 10 µF/25 V X7R placed directly at VIN/GND. On a flight controller, add bulk capacitance upstream to absorb ESC-induced voltage spikes, since the IC has little margin above 16.8 V.

## Summary

| Item | Value |
|---|---|
| Inductor | 4.7 µH, I_sat ≥ 5 A, DCR ≤ 20 mΩ, shielded |
| Ripple current | 1.15 A p-p (38 %) at 16.8 V in |
| Output ripple | ~10 mV p-p (theoretical) |
| C_OUT | 2 × 22 µF X7R |
| C_IN | 2 × 10 µF/25 V X7R, ~1.4 A RMS |

If you can share the board's actual inductor and capacitor part numbers or the supported input range, I can recompute with those exact values.