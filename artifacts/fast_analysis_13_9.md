# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

**Note:** I can't verify the exact BOM of the "STM32H743 AIO (Cycle 13)" board. The numbers below use TPS563200 datasheet values and typical flight-controller AIO operating conditions. Substitute your board's actual L, C, and input range if they differ.

## 1. Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_IN | 7.4 – 16.8 V | 2S–4S LiPo. The part's absolute max is 17 V, so **4S is the ceiling**. |
| V_OUT | 5.0 V | |
| I_OUT(max) | 3 A | |
| f_SW | 650 kHz | Fixed, D-CAP2 control |
| V_REF | 0.768 V | |
| L | 3.3 µH | Datasheet recommendation for 5 V output |
| C_OUT | 2 × 22 µF X5R/X7R | ≈ 25 µF effective after DC-bias derating at 5 V |

**Feedback divider:** V_OUT = 0.768 × (1 + R1/R2). With R2 = 10 kΩ and R1 = 56.2 kΩ, V_OUT ≈ 5.08 V.

## 2. Duty Cycle

D = V_OUT / V_IN

| V_IN | D |
|---|---|
| 7.4 V (2S nominal) | 0.68 |
| 12.6 V (3S full) | 0.40 |
| 16.8 V (4S full) | 0.30 |

## 3. Inductor Ripple Current

ΔI_L = V_OUT × (V_IN − V_OUT) / (V_IN × L × f_SW)

| V_IN | ΔI_L (p-p) | % of 3 A |
|---|---|---|
| 7.4 V | 0.76 A | 25 % |
| 12.6 V | 1.41 A | 47 % |
| **16.8 V** | **1.64 A** | **55 %** (worst case) |

The ripple is high relative to the classic 30 % rule. That is intentional for D-CAP2, which needs ripple at the feedback node for stable on-time control. Don't "fix" it by going to a much larger L.

## 4. Inductor Sizing

**Peak current (worst case, 16.8 V, 3 A):**
I_PK = I_OUT + ΔI_L/2 = 3 + 0.82 = **3.82 A**

**RMS current:**
I_RMS = √(I_OUT² + ΔI_L²/12) = √(9 + 0.22) ≈ **3.04 A**

**Minimum inductance for ≤ 40 % ripple at 16.8 V:**
L ≥ 5 × 11.8 / (16.8 × 1.2 A × 650 kHz) ≈ 4.5 µH

The datasheet allows roughly 2.2–4.7 µH for 5 V. Stay within its L/C table to keep the LC double pole where the D-CAP2 loop expects it.

**Recommended inductor specification:**

| Spec | Requirement |
|---|---|
| L | 3.3 µH (4.7 µH acceptable if C_OUT stays within the datasheet table) |
| I_SAT (−30 %) | **≥ 5 A**, giving margin above the ~4 A switch current limit during overload or startup |
| I_RMS (ΔT 40 °C) | ≥ 3.5 A |
| DCR | ≤ 25 mΩ. At 20 mΩ, P_DCR = 3.04² × 0.02 ≈ 185 mW |
| Construction | Shielded/molded (e.g., XAL4030-332, IHLP-1616 3.3 µH) to reduce EMI near the gyro and OSD lines |

## 5. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 × f_SW × C_eff) + ΔI_L × ESR

At 16.8 V with C_eff = 25 µF and ESR ≈ 3 mΩ:
- Capacitive term: 1.64 / (8 × 650k × 25 µF) ≈ **12.6 mV**
- ESR term: 1.64 × 0.003 ≈ **4.9 mV**
- **Total ≈ 15–18 mV p-p** (≈ 0.35 % of 5 V)

**LC corner frequency:** f₀ = 1 / (2π√(3.3 µH × 44 µF)) ≈ **13 kHz**, comfortably below f_SW.

Practical caveat: measured ripple on an AIO is usually dominated by **high-frequency switching spikes (tens to over 100 mV)** from layout and probe ground loop, not by the triangular ripple above. Measure with a short spring-tip ground, 20 MHz bandwidth limit off, and compare to the bandwidth-limited value.

## 6. Input Capacitor

**RMS current:** I_CIN = I_OUT × √(D(1−D))
- Worst case near D = 0.5 (≈ 10 V): **1.5 A**
- At 16.8 V: 1.37 A

**Input voltage ripple at 16.8 V with 10 µF effective:**
ΔV_IN = I_OUT × D(1−D) / (f_SW × C_IN) = 3 × 0.209 / (650k × 10 µF) ≈ **96 mV p-p**

**Recommendation:** 2 × 10 µF 25 V X7R (0805/1206) placed tight to VIN/GND pins, plus a 0.1 µF HF decoupler. The board's main bulk/low-ESR cap on the battery rail handles ESC-induced spikes. Without it, 4S transients can exceed the 17 V absolute max.

## 7. Edge Cases to Check

1. **2S low-battery dropout:** At 6.4 V, D ≈ 0.78. With a ~1.54 µs period and the part's minimum off-time, regulation becomes marginal and 5 V will sag below about 6 V input.
2. **4S full charge plus regen spikes:** 16.8 V leaves almost no margin to 17 V. A TVS or LC pre-filter is advisable if the board is used on 4S with high-current ESCs.
3. **Thermal:** At 3 A continuous, total buck loss is ≈ 0.8–1.0 W (efficiency ≈ 85–88 % at 12 V). On a dense AIO, this is often the real limit, not the inductor. Derate to about 2–2.5 A continuous unless there is airflow.

## Summary

| Item | Value |
|---|---|
| Inductor | 3.3 µH shielded, I_SAT ≥ 5 A, DCR ≤ 25 mΩ |
| Worst-case ΔI_L | 1.64 A p-p (16.8 V in) |
| Peak inductor current | 3.82 A |
| Output ripple (theoretical) | ≈ 15–18 mV p-p with 2 × 22 µF |
| Input cap RMS | ≈ 1.5 A max, 2 × 10 µF 25 V |

If you share the actual inductor part number and capacitor values from the Cycle 13 BOM or schematic, I can recompute with exact DCR, saturation curve, and derated capacitance.