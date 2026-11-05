# TPS563200 (5 V / 3 A) Ripple and Inductor Sizing

**Note:** I don't have the schematic for the "STM32H743 AIO (Cycle 14)" board, so the input range and capacitor values below are assumptions. Replace them with your board's values and the formulas still apply.

## 1. Design Assumptions

| Parameter | Value | Notes |
|---|---|---|
| V_OUT | 5.0 V | |
| I_OUT(max) | 3 A | |
| f_SW | 650 kHz | Typical for the TPS563200. Verify against your datasheet revision. |
| V_IN range | 2S–4S LiPo (8.4–16.8 V) | The TPS563200 is rated for **17 V max**, so it is **not suitable for 5S/6S** without a pre-regulator. |
| C_OUT | 2 × 22 µF X5R/X7R ceramic | About 30 µF effective after DC-bias derating |
| C_IN | 10 µF ceramic | About 10 µF effective at bus voltage |

The worst-case ripple occurs at the highest input voltage (4S full charge, 16.8 V).

## 2. Inductor Ripple Current

Duty cycle:

D = V_OUT / V_IN

Peak-to-peak ripple current:

ΔI_L = (V_IN − V_OUT) · D / (L · f_SW)

### Duty Cycle at Each Input Voltage

| V_IN | D | (V_IN − V_OUT)·D / f_SW |
|---|---|---|
| 8.4 V (2S) | 0.595 | 3.11 µV·s |
| 12.6 V (3S) | 0.397 | 4.64 µV·s |
| 16.8 V (4S) | 0.298 | 5.40 µV·s |

### Ripple Current for Candidate Inductors

| L | ΔI @ 2S | ΔI @ 3S | ΔI @ 4S | Ripple % of 3 A (4S) | I_peak (4S) |
|---|---|---|---|---|---|
| 2.2 µH | 1.41 A | 2.11 A | **2.46 A** | 82 % (too high) | 4.23 A |
| 3.3 µH | 0.94 A | 1.41 A | **1.64 A** | 55 % | 3.82 A |
| 4.7 µH | 0.66 A | 0.99 A | **1.15 A** | 38 % (good) | 3.57 A |

The ideal inductance for 30–40 % ripple (about 1 A) is:

L_ideal ≈ 5.40 µV·s / 1.0 A ≈ 5.4 µH

The TPS563200 uses D-CAP2 internal compensation, so you should stay within the datasheet's recommended L/C combinations for 5 V output (check the table in your revision). Within that constraint, **4.7 µH is the best choice** for a 4S-capable flight controller. 3.3 µH is acceptable if board space is tight.

## 3. Inductor Specification (4.7 µH Recommended)

| Parameter | Requirement | Reason |
|---|---|---|
| Inductance | 4.7 µH ±20 % | Keeps ripple about 1.15 A at 4S |
| I_SAT | ≥ 4.5 A | Above the IC's current limit, so the inductor does not saturate during overload or short |
| I_RMS (heating) | ≥ 3.2 A | I_RMS = √(3² + 1.15²/12) ≈ 3.02 A, plus margin |
| DCR | ≤ 25 mΩ | Copper loss ≈ 3² × 0.025 = 0.23 W |
| Construction | Shielded / molded | Reduces radiated noise into the gyro, OSD, and VTX |
| Size | 4×4 mm or 5×5 mm | Typical for this current class |

Examples include the Würth WE-MAPI / WE-XHMI series, Coilcraft XAL4040 / XAL5030, and TDK SPM5030. Verify I_SAT on each specific part number.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 · f_SW · C_OUT) + ΔI_L · ESR

With C_OUT = 30 µF effective and ESR ≈ 3 mΩ (ceramic), the capacitive term is ΔI / (8 × 650 k × 30 µ) = ΔI / 156:

| L | ΔI (4S) | Capacitive term | ESR term | **Total ΔV_OUT** |
|---|---|---|---|---|
| 2.2 µH | 2.46 A | 15.8 mV | 7.4 mV | **≈ 23 mV** |
| 3.3 µH | 1.64 A | 10.5 mV | 4.9 mV | **≈ 15 mV** |
| 4.7 µH | 1.15 A | 7.4 mV | 3.4 mV | **≈ 11 mV p-p (0.2 %)** |

The 4.7 µH result of about 11 mV p-p is fine for 5 V rails feeding a receiver, GPS, or LED strips. If this rail also feeds a camera or VTX, consider adding a ferrite bead and 10 µF downstream to keep 650 kHz content out of the video.

## 5. Input Capacitor

**RMS ripple current** (the capacitor must handle this without overheating):

I_CIN(rms) = I_OUT · √(D(1−D))

- At 4S: 3 × √(0.298 × 0.702) ≈ **1.37 A**
- The worst case is D = 0.5 (V_IN = 10 V): **1.5 A**

**Input voltage ripple:**

ΔV_IN ≈ I_OUT · D(1−D) / (f_SW · C_IN) = 3 × 0.209 / (650 k × 10 µ) ≈ **96 mV**

Recommendations:
- Use at least 10 µF (preferably 2 × 10 µF) X7R rated 25 V, plus a 0.1 µF capacitor placed tight to the VIN/GND pins.
- The board's bulk low-ESR electrolytic on the battery pads is still needed to absorb ESC switching spikes. Those spikes can exceed 17 V on a 4S pack, so a TVS diode (for example, an SMBJ15A-class part) on VIN is strongly recommended.

## 6. Summary

| Item | Recommendation |
|---|---|
| Inductor | **4.7 µH, I_SAT ≥ 4.5 A, DCR ≤ 25 mΩ, shielded** |
| Inductor ripple | ≈ 1.15 A p-p worst case (4S) |
| Peak inductor current | ≈ 3.6 A |
| C_OUT | 2 × 22 µF X7R (≥ 10 V) → ≈ 11 mV p-p ripple |
| C_IN | 2 × 10 µF X7R 25 V + 0.1 µF, rated ≥ 1.5 A RMS combined |
| Input limit | 4S max; protect against transients with a TVS |

If you can share the board's actual input range, capacitor part numbers, or schematic, I can redo the numbers with real values, including capacitor DC-bias derating and a thermal estimate for the IC.