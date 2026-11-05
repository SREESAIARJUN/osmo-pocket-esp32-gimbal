# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

**Note:** I don't have the schematic for the specific "STM32H743 AIO (Cycle 11)" board, so I can't confirm its actual component values. The analysis below uses TI datasheet parameters and typical flight-controller AIO assumptions. Substitute your real BOM values where they differ.

## 1. Design Assumptions

| Parameter | Value | Note |
|---|---|---|
| V_IN range | 9 – 16.8 V | 3S–4S LiPo. TPS563200 abs. max is 17 V, so **4S is the ceiling**. |
| V_OUT | 5.0 V | |
| I_OUT(max) | 3 A | |
| f_SW | 650 kHz | Fixed, D-CAP2 control |
| V_REF | 0.768 V | |
| Feedback divider | R1 = 56.2 kΩ, R2 = 10.2 kΩ | 0.768 × (1 + 56.2/10.2) ≈ 5.00 V |
| C_OUT | 2 × 22 µF X5R/X7R ceramic | |
| C_IN | 10 µF + 0.1 µF (recommend 2 × 10 µF) | |

The worst-case ripple occurs at **maximum V_IN (16.8 V)**.

## 2. Inductor Ripple Current

ΔI_L = V_OUT × (V_IN − V_OUT) / (V_IN × L × f_SW)

At V_IN = 16.8 V, the duty cycle is D = 5 / 16.8 = 0.298.

| L | ΔI_L at 16.8 V | ΔI_L at 11.1 V | Ripple % of 3 A |
|---|---|---|---|
| 2.2 µH | 2.46 A | 1.92 A | 82 % (too high) |
| **3.3 µH** | **1.64 A** | 1.28 A | 55 % |
| **4.7 µH** | **1.15 A** | 0.90 A | 38 % |

**Recommendation:** use **3.3 µH**, which is the TI datasheet value for 5 V out. Use 4.7 µH if you want lower ripple and better margin from current limit, at the cost of slower transient response.

## 3. Inductor Current Ratings (3.3 µH, worst case)

- **Peak current:** I_pk = I_OUT + ΔI_L/2 = 3 + 0.82 = **3.82 A**
- **RMS current:** I_rms = √(I_OUT² + ΔI_L²/12) = √(9 + 0.224) ≈ **3.04 A**

**Inductor requirements:**
- I_sat ≥ **4.5–5 A**. This covers the current-limit trip point plus margin; verify the limit value in the current datasheet revision.
- I_rms (thermal) ≥ **3.5 A**
- DCR ≤ ~25 mΩ, giving about 0.23 W conduction loss at 3 A
- Use a shielded, molded type to reduce magnetic coupling into the IMU, magnetometer, and gyro traces. Example sizes are 4×4 mm or 5×5 mm (e.g., Würth 744383360033, Coilcraft XAL4030-332).

At 3.82 A peak, the design runs close to the TPS563200 current limit. If the board actually needs a sustained 3 A at 4S, 4.7 µH gives more headroom.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 × f_SW × C_eff) + ΔI_L × ESR

Ceramic DC-bias derating matters here. A 22 µF 0805 10 V X5R part at 5 V bias drops to roughly 12–14 µF, so 2 × 22 µF gives **C_eff ≈ 25 µF**. Combined ESR is about 1.5 mΩ.

| Case | Capacitive term | ESR term | **Total ΔV_OUT (p-p)** |
|---|---|---|---|
| C_eff = 25 µF (realistic) | 12.6 mV | 2.5 mV | **≈ 15 mV (0.3 %)** |
| C_eff = 44 µF (nominal) | 7.2 mV | 2.5 mV | ≈ 10 mV |

This is well within the tolerance for 5 V loads such as receivers, VTX, LEDs, and the 3.3 V LDO feeding the STM32H743. Real measurements will show additional **switching spikes of 50–150 mV** caused by switch-node ringing, layout, and probe ground loop. Measure with a tip-and-barrel probe directly across C_OUT.

**LC double-pole frequency:** f₀ = 1 / (2π√(3.3 µH × 44 µF)) ≈ **13 kHz**. This sits in the range TI recommends for D-CAP2 stability. Don't push C_OUT far beyond ~68 µF without checking stability.

## 5. Input Ripple (often the bigger problem on an AIO)

- **RMS current in C_IN:** I_CIN,rms = I_OUT × √(D(1−D)) = 3 × √(0.298 × 0.702) ≈ **1.37 A**. The maximum is 1.5 A at D = 0.5 (about 10 V in).
- **Voltage ripple:** ΔV_IN ≈ I_OUT × D(1−D) / (f_SW × C_IN,eff)
  - A single 10 µF 25 V 0805 derates to about 5 µF at 16.8 V, giving **≈ 190 mV p-p**.
  - With 2 × 10 µF (≈ 10 µF effective), ripple is **≈ 95 mV p-p**.

**Recommendations:**
- Use 2 × 10 µF, rated **25 V minimum** (35 V preferred), placed tight to the VIN/GND pins.
- Add a 0.1 µF high-frequency decoupling cap.
- On a 4S ESC-shared bus, add a TVS or bulk low-ESR cap at the battery pad. ESC switching spikes can easily exceed the 17 V abs-max rating, and this is the most common failure mode for this part on FC/AIO boards.

## 6. Summary

| Item | Value |
|---|---|
| Inductor | **3.3 µH** (4.7 µH optional), I_sat ≥ 4.5 A, I_rms ≥ 3.5 A, DCR ≤ 25 mΩ, shielded |
| ΔI_L (worst case, 16.8 V) | 1.64 A p-p (3.3 µH) |
| Peak inductor current | 3.82 A |
| ΔV_OUT | ≈ 10–15 mV p-p (ripple only) |
| C_OUT | 2 × 22 µF X5R/X7R, 10 V or higher |
| C_IN | 2 × 10 µF, 25 V or higher, plus 0.1 µF |
| I_CIN,rms | ≈ 1.4–1.5 A |
| Max input | **4S only** (17 V abs max) |

**Layout tips for an AIO board:**
- Keep the SW node copper minimal.
- Place the C_IN loop as tight as possible.
- Keep the inductor and SW node away from the IMU, baro, and STM32 crystal.
- Route the FB trace away from SW, connecting to the C_OUT positive terminal.