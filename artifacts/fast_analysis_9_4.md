# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic or BOM for the "STM32H743 AIO (Cycle 9)" board, so this uses TPS563200 datasheet values and typical AIO flight-controller conditions. Swap in your actual L, C, and input range if they differ.

## Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V<sub>OUT</sub> | 5.0 V | |
| I<sub>OUT(max)</sub> | 3 A | Device rating |
| f<sub>SW</sub> | 650 kHz | Fixed (D-CAP2 control) |
| V<sub>IN</sub> | 2S–4S LiPo (7.4–16.8 V) | Part maxes at 17 V, so **no 5S/6S without a pre-regulator** |
| L | 3.3 µH | Datasheet recommendation for 5 V out |
| C<sub>OUT</sub> | 2 × 22 µF X5R/X7R | ≈30 µF effective after DC-bias derating, ESR ≈3 mΩ |
| C<sub>IN</sub> | 10 µF + 0.1 µF | ≈5 µF effective at 16 V |

## 1. Inductor Ripple Current

The ripple current is:

ΔI<sub>L</sub> = (V<sub>IN</sub> − V<sub>OUT</sub>) · D / (L · f<sub>SW</sub>), where D = V<sub>OUT</sub>/V<sub>IN</sub>

L · f<sub>SW</sub> = 3.3 µH × 650 kHz = 2.145 V/A.

| Battery | V<sub>IN</sub> | D | ΔI<sub>L</sub> (3.3 µH) | % of 3 A |
|---|---|---|---|---|
| 2S nominal | 7.4 V | 0.676 | 0.76 A | 25 % |
| 3S nominal | 11.1 V | 0.450 | 1.28 A | 43 % |
| 4S nominal | 14.8 V | 0.338 | 1.54 A | 51 % |
| **4S full (worst case)** | **16.8 V** | **0.298** | **1.64 A** | **55 %** |

Ripple is worst at maximum V<sub>IN</sub>.

## 2. Inductor Sizing

For a classic 30 % ripple target (0.9 A) at 16.8 V:

L = (16.8 − 5) × 0.298 / (0.9 A × 650 kHz) ≈ **6.0 µH**

D-CAP2 control, however, needs some ripple for stable regulation, and the datasheet only validates **3.3 µH (range ≈2.2–4.7 µH)** for 5 V out. Recommendations:

- **3.3 µH** is the default choice. It gives fast transients and a small footprint, at the cost of about 1.6 A ripple at 4S.
- **4.7 µH** is a good choice if the board runs mostly on 4S. It gives ΔI ≈ 1.15 A (38 %), lower core loss, and less EMI. Confirm stability with the datasheet's output-capacitor table.
- **Avoid 2.2 µH at 4S.** It gives ΔI ≈ 2.46 A, a peak near 4.2 A, and approaches the current limit.

**Inductor current ratings (3.3 µH, worst case):**

- I<sub>peak</sub> = 3 + 1.64/2 = **3.82 A**, so choose I<sub>sat</sub> ≥ 4.5 A (it must also ride through the IC's high-side current limit of about 4–5 A).
- I<sub>RMS</sub> = √(3² + 1.64²/12) ≈ **3.04 A**, so choose I<sub>rated</sub> ≥ 3.5 A.
- Use DCR below 30 mΩ (about 0.28 W loss at 3 A).
- Use a shielded part, e.g. a 4×4 to 5×5 mm molded inductor such as the XAL4030, IHLP-1616, or similar.

## 3. Output Voltage Ripple

The output ripple has a capacitive term and an ESR term:

ΔV<sub>OUT</sub> ≈ ΔI<sub>L</sub>/(8 · f<sub>SW</sub> · C<sub>OUT</sub>) + ΔI<sub>L</sub> · ESR

At 16.8 V with 3.3 µH and 30 µF effective:

- Capacitive term: 1.64 / (8 × 650k × 30 µF) ≈ **10.5 mV**
- ESR term: 1.64 × 3 mΩ ≈ **4.9 mV**
- **Total ≈ 15 mV p-p (≈0.3 % of 5 V)**, plus switching-edge spikes of 20–50 mV depending on layout.

With 4.7 µH, ripple falls to about 11 mV p-p.

## 4. Input Ripple (often overlooked on AIO boards)

The input capacitor RMS current is:

I<sub>CIN,RMS</sub> = I<sub>OUT</sub> · √(D(1 − D))

This peaks near 3S (D ≈ 0.45) at **≈1.5 A RMS**. Make sure C<sub>IN</sub> is rated for it, ideally with 2 × 10 µF in a 25 V+ rating.

The input voltage ripple at 4S full is:

ΔV<sub>IN</sub> ≈ I<sub>OUT</sub> · D(1 − D) / (f · C<sub>IN</sub>) = 3 × 0.209 / (650k × 5 µF) ≈ **190 mV p-p**

That much ripple on the battery rail couples into the ESC and other regulators. Adding a second 10 µF reduces it to about 100 mV.

## 5. Practical Notes for a Flight Controller

- The 650 kHz fundamental and its harmonics sit well above gyro bandwidth. Real problems come from layout coupling into the IMU supply or SPI lines, so keep the SW node small and away from the IMU.
- If the IMU or ADC runs from 3.3 V derived from this 5 V rail, the LDO's PSRR at 650 kHz is usually poor (≈20–30 dB). Add a ferrite bead plus 10 µF before the LDO.
- **ESC spikes on 4S can exceed 17 V and kill the TPS563200.** Use a TVS diode (e.g. SMBJ16A) or a low-ESR bulk cap on the battery input.
- In light-load Eco-mode the switching frequency drops into the audio/kHz range, which can produce audible noise or ripple. This is usually harmless, but note it if you see low-frequency artifacts.

## Summary (4S worst case, 3.3 µH)

| Quantity | Value |
|---|---|
| Inductor ripple | 1.64 A p-p |
| Peak inductor current | 3.82 A |
| Inductor I<sub>sat</sub> | ≥ 4.5 A |
| Output ripple | ≈ 15 mV p-p |
| Input cap RMS current | ≈ 1.5 A |
| Input voltage ripple | ≈ 190 mV p-p (10 µF) |

If you can share the actual inductor part number, output capacitors, and the battery range the board supports, I can recompute these with exact values.