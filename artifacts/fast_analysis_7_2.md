# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic or BOM for the "STM32H743 AIO (Cycle 7)" board. The numbers below come from the TPS563200 datasheet and standard buck equations, using the typical flight-controller assumptions listed. Substitute your actual values if they differ.

## Assumptions

| Parameter | Value |
|---|---|
| V_IN | 4S LiPo: 16.8 V max, 12 V nominal |
| V_OUT | 5.0 V |
| I_OUT | 3 A max |
| f_SW | 650 kHz (TPS563200 fixed, D-CAP2) |
| V_REF | 0.768 V |
| C_OUT | 2 × 22 µF X7R ceramic (≈30 µF effective at 5 V DC bias) |

**Input voltage warning:** The TPS563200's absolute maximum V_IN is 17 V. A fully charged 4S pack (16.8 V) leaves almost no margin for landing spikes or ESC regen transients. 5S or 6S input will destroy the part. If this board advertises 2–6S support, the 5 V rail must be fed by a pre-regulator or use a different IC.

## 1. Duty Cycle

D = V_OUT / V_IN

- At 16.8 V: D ≈ 0.298
- At 12 V: D ≈ 0.417

## 2. Inductor Ripple Current

ΔI_L = V_OUT × (1 − D) / (L × f_SW)

| L | V_IN = 12 V | V_IN = 16.8 V (worst case) | % of 3 A at 16.8 V |
|---|---|---|---|
| 2.2 µH | 2.04 A | 2.46 A | 82 % (too high) |
| **3.3 µH** | 1.36 A | 1.64 A | 55 % |
| **4.7 µH** | 0.95 A | 1.15 A | 38 % (ideal) |

**Peak current** = I_OUT + ΔI_L / 2:

- 3.3 µH: 3 + 0.82 = **3.82 A**
- 4.7 µH: 3 + 0.57 = **3.57 A**

The TPS563200's high-side current limit is roughly 4.2 A typical, with tolerance. A 3.3 µH inductor at full load and full battery leaves little headroom, so 4.7 µH is the more robust choice. It remains within TI's recommended range for a 5 V output.

## 3. Inductor Specification

RMS current:

I_L,rms = √(I_OUT² + ΔI²/12) = √(9 + 0.22) ≈ **3.04 A**

| Spec | Requirement |
|---|---|
| Inductance | 3.3–4.7 µH (4.7 µH preferred) |
| I_sat | ≥ 5 A (above current limit, with margin) |
| I_rms (heating) | ≥ 3.5 A |
| DCR | ≤ 25 mΩ (gives ≈ 3.04² × 0.02 ≈ 185 mW copper loss) |
| Type | Shielded (important near the gyro and magnetometer) |

Example parts: Würth 744383360047 or 74438357047, Coilcraft XAL4040-472, TDK SPM5030-4R7.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 × f_SW × C_OUT) + ΔI_L × ESR

With C_eff ≈ 30 µF and ESR ≈ 3 mΩ, at 16.8 V input:

| L | Capacitive term | ESR term | **Total** |
|---|---|---|---|
| 3.3 µH | 10.5 mV | 4.9 mV | **≈ 15 mVpp (0.3 %)** |
| 4.7 µH | 7.4 mV | 3.5 mV | **≈ 11 mVpp (0.2 %)** |

Notes:
- Use 16 V or 25 V rated, 0805 or 1206 caps. A 10 V 0603 22 µF part can lose 60 % or more of its capacitance at 5 V bias, which would roughly double the ripple.
- Keep total C_OUT within TI's recommended range for D-CAP2 stability (roughly 20–68 µF effective).
- Expect additional high-frequency switching spikes of 20–50 mV from layout parasitics. These are not captured by the formula. A 100 nF cap at the load plus a ferrite bead to sensitive rails (gyro, baro) helps.

## 5. Input Capacitor

RMS ripple current:

I_CIN,rms = I_OUT × √(D(1−D))

- At 16.8 V: 1.37 A
- Worst case near D = 0.5: 1.5 A

Input voltage ripple, with 10 µF effective:

ΔV_IN = I_OUT × D(1−D) / (f_SW × C_IN) ≈ **96 mVpp**

Recommendations:
- Use at least 2 × 10 µF 25 V X7R placed tight to VIN/GND.
- Add a bulk electrolytic or polymer cap on the battery input (typically a low-ESR cap on the FC/ESC stack) to absorb ESC transients.

## 6. Feedback Divider for 5.0 V

V_OUT = 0.768 × (1 + R1/R2)

With R2 = 10.0 kΩ, R1 = 55.1 kΩ (or 56.2 kΩ / 10.2 kΩ), V_OUT ≈ 5.0 V.

## Summary

| Item | Recommended |
|---|---|
| Inductor | 4.7 µH shielded, I_sat ≥ 5 A, DCR ≤ 25 mΩ |
| Ripple current | ≈ 1.15 A pp (38 %) at 16.8 V |
| Peak inductor current | ≈ 3.6 A |
| Output ripple | ≈ 11–15 mVpp (calculated), ~30–50 mV with layout spikes |
| C_OUT | 2 × 22 µF, ≥ 16 V, X7R |
| C_IN | 2 × 10 µF, 25 V, X7R + bulk |
| Major risk | 17 V V_IN limit, so the design is 4S-only |

If you can share the actual inductor part number, capacitor values, or input voltage range from the Cycle 7 schematic, I can recompute with those exact figures.