# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the specific "STM32H743 AIO (Cycle 23)" board, so this analysis uses the TPS563200 datasheet parameters and typical flight-controller AIO operating conditions. If you can share the board's actual inductor and capacitor part numbers, I can recompute with those values.

## 1. Assumptions

| Parameter | Value | Notes |
|---|---|---|
| Topology | D-CAP2 synchronous buck | Fixed ~650 kHz, internal compensation |
| V_IN range | 7.4 V to 16.8 V | 2S to 4S LiPo. The IC's absolute max is 17 V, so 5S/6S packs need a pre-regulator. |
| V_OUT | 5.0 V | |
| I_OUT (max) | 3 A | |
| f_SW | 650 kHz | |
| C_OUT | 2 × 22 µF X5R ceramic | About 30 µF effective after DC-bias derating at 5 V |

## 2. Inductor Ripple Current

The ripple current is:

ΔI_L = V_OUT · (V_IN − V_OUT) / (V_IN · L · f_SW)

| V_IN | Duty cycle (D) | ΔI_L with L = 3.3 µH | ΔI_L with L = 4.7 µH |
|---|---|---|---|
| 7.4 V (2S) | 0.68 | 0.76 A | 0.53 A |
| 12 V (3S nominal) | 0.42 | 1.36 A | 0.96 A |
| 16.8 V (4S full) | 0.30 | **1.64 A** | **1.15 A** |

The worst case occurs at maximum V_IN.

- A conventional 30 % ripple target (0.9 A) would require about 6 µH.
- D-CAP2 loop stability depends on the L·C_OUT product, and TI's recommended range for a 5 V output is 3.3 to 4.7 µH.
- **4.7 µH is therefore the practical choice.** It gives about 38 % ripple at 4S.

## 3. Inductor Current Ratings (L = 4.7 µH)

- **Peak current:** I_pk = I_OUT + ΔI_L/2 = 3 + 0.58 = **3.58 A**
  - With 3.3 µH, the peak rises to 3.82 A.
- **RMS current:** I_rms = √(I_OUT² + ΔI_L²/12) = √(9 + 0.11) ≈ **3.02 A**
- **Specification to buy:**
  - Saturation current ≥ 4.5 A. Ideally it should exceed the IC's current-limit trip point so the inductor doesn't saturate during a short circuit.
  - Thermal current rating ≥ 3.5 A.
  - DCR ≤ 30 mΩ. At 25 mΩ, conduction loss is P = 3.02² × 0.025 ≈ 0.23 W.
- **Example parts:** shielded 4.7 µH inductors in a 4×4 mm to 5×5 mm footprint, such as the Würth WE-MAPI 4030, Coilcraft XGL4030/XEL4030, or TDK SPM5030.
- Shielded construction matters on an AIO board because the inductor sits close to the gyro, the receiver, and the VTX.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 · f_SW · C_OUT) + ΔI_L · ESR

The capacitive term's denominator is 8 × 650 kHz × 30 µF = 156 A/V. ESR is taken as about 3 mΩ.

| L | Capacitive term | ESR term | Total (at 16.8 V) |
|---|---|---|---|
| 4.7 µH | 7.4 mV | 3.5 mV | **≈ 11 mVpp** (0.2 %) |
| 3.3 µH | 10.5 mV | 4.9 mV | **≈ 15 mVpp** (0.3 %) |

Both results are fine for a 5 V rail. The TPS563200 injects its own ripple internally, so an all-ceramic output capacitor bank is stable. Keep C_OUT between 20 and 68 µF effective.

## 5. Input Capacitor

**RMS current:** I_Cin,rms = I_OUT · √(D(1 − D))

- This peaks at D = 0.5 (V_IN = 10 V), giving **1.5 A RMS**.
- At 4S full charge it is 1.37 A RMS.

**Voltage ripple:** ΔV_IN ≈ I_OUT · D(1 − D) / (f_SW · C_IN)

- With 10 µF effective, the result is about 115 mVpp.

**Recommendation:**
- Use at least 2 × 10 µF, 25 V X5R/X7R capacitors, plus a 0.1 µF capacitor placed tight to the VIN and GND pins.
- Add a bulk low-ESR capacitor at the battery pads, since ESC switching transients on the shared battery bus can approach the 17 V limit.

## 6. Summary

| Item | Value |
|---|---|
| Inductor | **4.7 µH**, I_sat ≥ 4.5 A, I_rms ≥ 3.5 A, DCR ≤ 30 mΩ, shielded |
| Worst-case ΔI_L | 1.15 A (at 16.8 V input) |
| Peak inductor current | ~3.6 A |
| Output ripple | ~11 mVpp with 2 × 22 µF |
| Input capacitor RMS current | ~1.5 A (worst case at 10 V input) |
| Main caveat | 4S maximum. A 4S pack at 16.8 V plus regenerative spikes leaves very little margin to 17 V, so consider a TVS diode or a pre-regulator. |