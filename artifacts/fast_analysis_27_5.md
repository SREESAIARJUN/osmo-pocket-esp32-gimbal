# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the specific "STM32H743 AIO (Cycle 27)" board, so this uses TPS563200 datasheet parameters and typical flight-controller input conditions. Replace the assumptions with your board's actual values.

## Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_IN | 12 V (nominal 3S) to 16.8 V (4S full) | The TPS563200 is rated to **17 V max**, so 4S is the limit. **5S or 6S cannot connect directly.** |
| V_OUT | 5.0 V | |
| I_OUT | 3 A max | |
| f_SW | 650 kHz | Fixed, D-CAP2 control |
| C_OUT | 2 × 22 µF X5R ceramic, about 30 µF effective after DC bias derating | ESR about 3 mΩ |
| C_IN | 10 µF ceramic, about 10 µF effective at 12–17 V | |

## 1. Duty Cycle

D = V_OUT / V_IN (ideal)

- 12 V: D = 0.417
- 16.8 V: D = 0.298

## 2. Inductor Ripple Current

ΔI_L = V_OUT · (1 − D) / (L · f_SW)

| L | ΔI_L at 12 V | ΔI_L at 16.8 V | Ripple at 16.8 V (% of 3 A) |
|---|---|---|---|
| 2.2 µH | 2.04 A | 2.46 A | 82 % |
| 3.3 µH | 1.36 A | 1.64 A | 55 % |
| 4.7 µH | 0.96 A | 1.15 A | 38 % |

Worst-case ripple occurs at maximum V_IN (16.8 V).

The classic 30–40 % ripple target would need L ≈ 5.4 µH. However, D-CAP2 loop stability constrains the LC double-pole, and TI's recommended range for this part is about 1.5–4.7 µH. For a 5 V output the datasheet table suggests about 2.2 µH. Check the exact table in your datasheet revision.

**Recommendation: 3.3 µH.** It is a good compromise: lower ripple and peak current than 2.2 µH, while staying inside the stable range. Use 4.7 µH only with the C_OUT values TI lists for it.

## 3. Peak Current and Inductor Rating

I_PK = I_OUT + ΔI_L / 2. At 16.8 V and 3 A:

- 2.2 µH: 4.23 A
- 3.3 µH: 3.82 A
- 4.7 µH: 3.58 A

The TPS563200 uses a low-side valley current limit, roughly 3.5–4.5 A depending on revision and temperature. With 2.2 µH at 4S and full load, the valley current is 3 − 1.23 ≈ 1.8 A, which is fine. However, the high peak current stresses the inductor saturation rating.

**Inductor spec for 3.3 µH:**
- I_sat ≥ 4.5–5 A, covering current-limit transients
- I_rms ≥ 3.5 A
- DCR ≤ 25–30 mΩ. Conduction loss ≈ 3² × 0.025 ≈ 0.23 W.
- Shielded part (EMI near the gyro and baro)
- Candidates: Würth 744383560033, Coilcraft XAL4030-332, TDK SPM4012-3R3, or similar 4×4 mm parts

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 · f_SW · C_OUT) + ΔI_L · ESR

Here 8 · f_SW · C_OUT = 8 × 650 kHz × 30 µF = 156 A/V.

| L | Capacitive term | ESR term | Total (16.8 V in) |
|---|---|---|---|
| 2.2 µH | 15.7 mV | 7.4 mV | **≈ 23 mV p-p** |
| 3.3 µH | 10.5 mV | 4.9 mV | **≈ 15 mV p-p** |
| 4.7 µH | 7.4 mV | 3.5 mV | **≈ 11 mV p-p** |

All are well under 1 % of 5 V. Expect slightly less at 12 V input.

## 5. Input Ripple

**RMS current:** I_CIN,rms = I_OUT · √(D(1−D))
- 12 V: 1.48 A rms (worst case, closest to D = 0.5)
- 16.8 V: 1.37 A rms

**Voltage ripple:** ΔV_IN ≈ I_OUT · D(1−D) / (f_SW · C_IN)
- 12 V: about 112 mV p-p with 10 µF effective
- 16.8 V: about 96 mV p-p

Use 2 × 10 µF (25 V, X5R/X7R, 0805 or larger) placed right at VIN/GND. Add a 0.1 µF 0402 cap at the pins for high-frequency decoupling. Keep the board's bulk ESC-rail capacitor (low-ESR electrolytic) to absorb motor-induced spikes. Those spikes can approach the 17 V absolute maximum on 4S.

## 6. High-Frequency (Switch-Node) Ripple

The ripple values above are fundamental 650 kHz ripple. Spikes in the 50–300 MHz range come from switch-node ringing (loop inductance with MOSFET capacitance) and dominate scope readings if layout is poor. To control them:

- Keep the hot loop (C_IN → VIN → GND) as small as possible.
- Keep the SW node copper small and away from the gyro, baro, and OSD lines.
- Use a short ground spring when measuring, not the long scope ground clip.
- If needed, add a ferrite bead with a 10 µF + 100 nF π-filter on 5 V rails feeding video or receivers.

## Summary

| Item | Value |
|---|---|
| Inductor | **3.3 µH**, I_sat ≥ 4.5 A, DCR ≤ 30 mΩ, shielded |
| Inductor ripple current | 1.36 A (12 V) to 1.64 A (16.8 V) |
| Peak inductor current | ≈ 3.8 A |
| Output ripple | ≈ 15 mV p-p (2 × 22 µF ceramic) |
| Input RMS current | ≈ 1.5 A, using 2 × 10 µF / 25 V |
| Max input | 4S (16.8 V). Do not use on 5S/6S without a pre-regulator. |

If you share the actual board values (inductor part number, C_OUT, input voltage range), I can recompute with exact numbers.