# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 15)" board, so these calculations use TPS563200 datasheet parameters and typical flight-controller input conditions. Substitute your actual component values where they differ.

## 1. Key Parameters

| Parameter | Value |
|---|---|
| Topology | Synchronous buck, D-CAP2 control |
| Switching frequency (f_sw) | 650 kHz (typ.) |
| Input range | 4.5–17 V (2S–4S LiPo) |
| V_OUT | 5.0 V |
| I_OUT (max) | 3 A |

Analysis cases:
- **Worst-case ripple:** V_IN = 16.8 V (4S fully charged)
- **Nominal:** V_IN = 12 V

## 2. Duty Cycle

D = V_OUT / V_IN

- 16.8 V: D = 0.298
- 12 V: D = 0.417

## 3. Inductor Ripple Current

ΔI_L = (V_IN − V_OUT) · D / (L · f_sw)

| L | ΔI_L @ 12 V | ΔI_L @ 16.8 V | % of 3 A (worst case) |
|---|---|---|---|
| 2.2 µH | 2.04 A | 2.46 A | 82 % (too high) |
| 3.3 µH | 1.36 A | 1.64 A | 55 % |
| **4.7 µH** | **0.95 A** | **1.15 A** | **38 %** |

For the classic 30 % ripple target at maximum V_IN:

L = 3.51 V·(D) / (0.9 A × 650 kHz) ≈ 6.0 µH

However, D-CAP2 control needs adequate ripple for stable regulation. TI's recommended range for a 5 V output is about 3.3–4.7 µH. Confirm this against the datasheet table for your revision.

**Recommendation: 4.7 µH.**

## 4. Inductor Current Ratings (4.7 µH, worst case)

- **Peak current:** I_pk = I_OUT + ΔI_L/2 = 3 + 0.575 = **3.58 A**
- **RMS current:** I_rms = √(I_OUT² + ΔI_L²/12) = √(9 + 0.11) ≈ **3.02 A**

**Selection criteria:**
- Saturation current (I_sat) ≥ 5 A, so the inductor survives the IC's current-limit trip during overload or startup into large capacitance.
- Thermal current rating (I_rms) ≥ 3.5 A.
- DCR ≤ 25 mΩ. This gives P_cu ≈ 3.02² × 0.025 ≈ 0.23 W.
- Shielded construction, to limit EMI coupling into the gyro, OSD, and video lines.

**Example parts:**
- Würth 744383560047
- Coilcraft XGL4030-472
- TDK SPM5030-4R7

## 5. Output Voltage Ripple

Assumed output capacitance: 2 × 22 µF X5R/X7R, 10 V, 0805/1206. After DC-bias derating at 5 V, the effective capacitance is about 30 µF, with ESR ≈ 3 mΩ.

- **Capacitive term:** ΔV_C = ΔI_L / (8 · f_sw · C_eff) = 1.15 / (8 × 650k × 30µ) ≈ **7.4 mV**
- **ESR term:** ΔV_ESR = 1.15 A × 3 mΩ ≈ **3.5 mV**
- **Total:** ≈ **10–11 mV p-p** (about 0.2 % of 5 V)

With 3.3 µH instead, the total rises to about 15 mV p-p. Keep effective C_OUT within the datasheet's D-CAP2 stability window (roughly 20–68 µF). Too much ceramic capacitance can cause jitter or instability.

## 6. Input Capacitor

**RMS current:** I_Cin,rms = I_OUT · √(D(1 − D))

- 16.8 V: 1.37 A
- 12 V: 1.48 A
- Worst case (D = 0.5): 1.5 A

**Input voltage ripple:** ΔV_IN ≈ I_OUT · D(1 − D) / (f_sw · C_in)

Assuming about 5 µF effective (10 µF/25 V after bias derating):

ΔV_IN ≈ 0.63 / (650k × 5µ) ≈ **190 mV p-p**

**Recommendations:**
- Use 2 × 10 µF, 25–35 V X7R, placed directly at VIN/GND, plus a 0.1 µF high-frequency decoupler.
- On a flight controller, keep a bulk low-ESR electrolytic on the battery pad (e.g., 470 µF/35 V) to absorb ESC-induced spikes. The TPS563200's 17 V absolute maximum leaves very little margin on 4S.

## 7. Summary

| Item | Value |
|---|---|
| Inductor | 4.7 µH, I_sat ≥ 5 A, I_rms ≥ 3.5 A, DCR ≤ 25 mΩ, shielded |
| Inductor ripple | 0.95 A (12 V) / 1.15 A (16.8 V) |
| Peak inductor current | ≈ 3.6 A |
| C_OUT | 2 × 22 µF X7R 10 V (≈ 30 µF effective) |
| Output ripple | ≈ 10–11 mV p-p at 650 kHz |
| C_IN | 2 × 10 µF 25–35 V X7R + 0.1 µF |
| Input RMS current | ≤ 1.5 A |

## 8. Layout Notes for the H743 AIO

- Keep the SW node copper minimal, and route it away from the IMU, baro, and analog video.
- Make the C_IN → VIN → PGND loop as tight as possible. This is the main source of high-frequency switching noise.
- Connect the feedback divider at the C_OUT point, not at the inductor.
- If ESC voltage spikes exceed about 17 V on 4S, add a TVS diode (e.g., SMAJ15A) or switch to a 28 V-rated regulator such as the TPS54331 or LMR33630.

If you can share the actual V_IN range and the inductor and capacitor part numbers on the Cycle 15 board, I can recompute these numbers with exact values.