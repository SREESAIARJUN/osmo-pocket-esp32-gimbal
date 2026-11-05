# TPS563200 5V/3A Buck: Ripple and Inductor Sizing

**Caveat:** I don't have the schematic for the specific "STM32H743 AIO (Cycle 30)" board, so I can't confirm its actual component values. The numbers below come from the TPS563200 datasheet and typical flight-controller input conditions. Replace them with your board's actual L, C, and input voltage if they differ.

## Key Constraints (TPS563200)

| Parameter | Value |
|---|---|
| Vin range | 4.5–17 V (**max 4S**; 4S fully charged is 16.8 V, leaving almost no margin for spikes) |
| Switching frequency | ~650 kHz (D-CAP2, fixed) |
| Output current | 3 A continuous |
| Datasheet-recommended L for 5 V out | **4.7 µH** |
| Recommended Cout | 20–68 µF ceramic |

## 1. Inductor Ripple Current

ΔI_L = (Vin − Vout) · D / (L · f_sw), where D = Vout / Vin. Assumes L = 4.7 µH and f = 650 kHz.

| Input | Vin | D | ΔI_L (p-p) | I_peak @ 3 A |
|---|---|---|---|---|
| 2S full | 8.4 V | 0.595 | 0.66 A | 3.33 A |
| 3S full | 12.6 V | 0.397 | 0.99 A | 3.49 A |
| **4S full (worst case)** | **16.8 V** | **0.298** | **1.15 A** | **3.58 A** |

The ripple ratio at the worst case is about 38% of I_out, which falls within the typical 20–40% design target.

## 2. Inductor Selection

- **Inductance:** 4.7 µH (±20%). At −20% (3.76 µH), the ripple rises to about 1.44 A and the peak to about 3.72 A.
- **Saturation current (Isat):** ≥ 4.5 A, preferably 5 A or more for margin over current-limit and transient events.
- **RMS current:** I_rms = √(3² + 1.15²/12) ≈ **3.02 A**, so choose I_rms rating ≥ 3.5 A.
- **DCR:** ≤ 25 mΩ. At 20 mΩ, conduction loss is about 0.18 W.
- **Type:** Shielded (low EMI near the gyro and baro), for example a 4×4 mm or 5×5 mm molded power inductor.

## 3. Output Voltage Ripple

ΔV_out ≈ ΔI_L / (8 · f · C_eff) + ΔI_L · ESR

Assumptions:
- Cout = 2 × 22 µF (0805, 16 V, X5R).
- After DC-bias derating at 5 V, this is about 25 µF effective.
- ESR is about 3 mΩ.

Results at 4S full:
- Capacitive term: 1.15 / (8 · 650k · 25µ) ≈ **8.8 mV**
- ESR term: 1.15 × 0.003 ≈ **3.5 mV**
- **Total ≈ 12 mV p-p** (about 0.25% of 5 V)

The LC corner frequency is f₀ = 1/(2π√(LC)) ≈ **14.7 kHz**, which is well below f_sw and consistent with the datasheet's stable D-CAP2 combinations.

## 4. Input Ripple

- **Input capacitor RMS current:** I_out · √(D(1−D)).
  - At 16.8 V: **1.37 A**.
  - The maximum is 1.5 A at D = 0.5, which occurs around Vin = 10 V.
- **Input voltage ripple:** With 2 × 10 µF/25 V (about 8 µF effective), ΔV_in ≈ I_out · D(1−D) / (f · C_in) ≈ **120 mV p-p**.
- **Practical advice:** Place the ceramics right at the VIN/GND pins. Rely on the board's main bulk/low-ESR cap to absorb ESC-induced spikes, because a 4S spike can easily exceed 17 V.

## Summary

| Item | Recommendation |
|---|---|
| L | 4.7 µH, Isat ≥ 5 A, I_rms ≥ 3.5 A, DCR ≤ 25 mΩ, shielded |
| Cout | 2 × 22 µF X5R/X7R (≥ 10 V rating; 16 V preferred) |
| Cin | 2 × 10 µF/25 V X7R, close to IC |
| Worst-case ΔI_L | ~1.15 A p-p (4S) |
| Output ripple | ~10–15 mV p-p |
| Main risk | Vin headroom on 4S. Add TVS or bulk capacitance, or use a higher-Vin regulator for 5S/6S. |

If you can share the actual inductor part number, capacitor values, and battery cell count from the board, I can recompute the exact figures.