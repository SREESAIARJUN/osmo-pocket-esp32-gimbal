# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 3)" board, so this uses TPS563200 datasheet values and typical flight-controller AIO conditions. Replace the assumptions with your actual BOM values if they differ.

## Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_OUT | 5.0 V | |
| I_OUT(max) | 3 A | |
| f_SW | 650 kHz | TPS563200 nominal (D-CAP2, fixed frequency) |
| V_IN range | 7.4 – 16.8 V | 2S–4S LiPo |
| L (datasheet recommendation for 5 V out) | 3.3 µH (allowed range 2.2–4.7 µH) | |
| C_OUT | 2 × 22 µF X5R ceramic, about 30 µF effective after DC-bias derating at 5 V | |

**Important:** The TPS563200's absolute maximum V_IN is 17 V. A 4S pack (16.8 V full) has almost no margin for inductive spikes on the battery lead. **It cannot run directly from 5S or 6S.** If the AIO accepts 6S, the 5 V rail must come from a pre-regulator or a different part.

## 1. Inductor Ripple Current

ΔI_L = V_OUT·(V_IN − V_OUT) / (V_IN · L · f_SW)

The worst case occurs at maximum V_IN.

| V_IN | Duty D | ΔI_L (3.3 µH) | ΔI_L (4.7 µH) |
|---|---|---|---|
| 7.4 V (2S) | 0.676 | 0.76 A p-p | 0.53 A |
| 12.6 V (3S) | 0.397 | 1.41 A p-p | 0.99 A |
| 16.8 V (4S) | 0.298 | **1.64 A p-p** | 1.15 A |

At 4S with 3.3 µH, the ripple is about 55 % of I_OUT. That is higher than the classic 20–40 % target but within TI's D-CAP2 design range. Using 4.7 µH brings it to about 38 %.

Do not exceed 4.7 µH. The D-CAP2 loop relies on the L·C_OUT corner frequency, and a larger L risks instability or poor transient response.

## 2. Peak and RMS Inductor Current (worst case, 16.8 V, 3.3 µH)

- **Peak:** I_pk = I_OUT + ΔI_L/2 = 3 + 0.82 = **3.82 A**
- **RMS:** I_rms = √(I_OUT² + ΔI_L²/12) = √(9 + 0.22) = **3.04 A**

## 3. Inductor Selection

| Spec | Requirement |
|---|---|
| Inductance | 3.3 µH (or 4.7 µH for lower ripple), ±20 % |
| I_sat | ≥ 4.5 A. This must exceed I_pk and the IC's valley current limit plus ripple; check the limit in the datasheet. |
| I_rms (thermal rating) | ≥ 3.5 A |
| DCR | ≤ 30 mΩ. At 3 A this is about 0.27 W copper loss. |
| Construction | Shielded, molded type (e.g., 4×4 or 5×5 mm class) to limit EMI near the IMU, baro, and video lines |

Example parts in this class include Würth WE-MAPI/XHMI, Coilcraft XAL4030/XAL5030-332, and Bourns SRP5030. Verify the current ratings against your exact part.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8·f_SW·C_eff) + ΔI_L·ESR

At 4S with 3.3 µH, using C_eff ≈ 30 µF and ESR ≈ 1.5 mΩ (two ceramics in parallel):

- Capacitive term: 1.64 / (8 × 650k × 30µ) ≈ 10.5 mV
- ESR term: 1.64 × 0.0015 ≈ 2.5 mV
- **Total ≈ 13 mV p-p** (about 0.26 % of 5 V)

Real boards measure higher because of ESL spikes at the switching edges, often 30–60 mV of HF spikes. A ferrite bead plus a 10 µF capacitor feeding sensitive loads (receiver, GPS, camera) helps.

## 5. Input Capacitor

**RMS current:** I_Cin,rms = I_OUT·√(D(1−D)). The maximum occurs at D = 0.5 (V_IN = 10 V), giving **1.5 A RMS**.

**Input ripple:** ΔV_IN ≈ I_OUT·D(1−D) / (f_SW·C_in) = 3 × 0.25 / (650k × 10 µF eff.) ≈ **115 mV p-p**

**Recommendation:**
- 2 × 10 µF, 25 V X5R, plus 0.1 µF placed tight to VIN/GND.
- Keep the bulk low-ESR electrolytic on the battery pads (ESC side), since motor-induced spikes are the real threat to the 17 V maximum.

## 6. Summary

| Item | Value |
|---|---|
| Inductor | 3.3 µH, I_sat ≥ 4.5 A, I_rms ≥ 3.5 A, DCR ≤ 30 mΩ, shielded |
| Worst-case ΔI_L | 1.64 A p-p (4S); 1.15 A with 4.7 µH |
| I_peak | 3.82 A |
| Output ripple | about 13 mV p-p calculated; expect 30–60 mV of spikes in practice |
| C_OUT | 2 × 22 µF, 10 V or higher, X5R/X7R |
| C_IN | 2 × 10 µF, 25 V, plus 0.1 µF HF decoupling |
| Main risk | V_IN spikes above 17 V on 4S; no 6S support |

If you share the actual inductor part number and capacitor values from the board's BOM, I can rerun the numbers against the real components.