# TPS563200 (5 V / 3 A) Ripple and Inductor Sizing

**Caveat:** I don't have the schematic for this specific "STM32H743 AIO (Cycle 25)" board, so I can't confirm its exact inductor, capacitors, or input range. The numbers below use TPS563200 datasheet parameters and typical flight-controller assumptions. Replace them with the board's real values if you have the BOM.

## 1. Key Parameters

| Parameter | Value |
|---|---|
| Topology | Synchronous buck, D-CAP2 control |
| V_IN range | 4.5–17 V, so 2S–4S LiPo (max 16.8 V) |
| V_OUT | 5.0 V |
| I_OUT(max) | 3 A |
| f_SW | ~650 kHz (fixed, nominal) |
| Datasheet L for 5 V out | 2.2–4.7 µH (3.3 µH typical) |
| Datasheet C_OUT | ~20–68 µF ceramic |

Duty cycle: D = V_OUT / V_IN

- At 16.8 V (4S full): D ≈ 0.30, t_on ≈ 458 ns
- At 12 V: D ≈ 0.42
- At 7.4 V (2S nominal): D ≈ 0.68

## 2. Inductor Ripple Current

ΔI_L = (V_IN − V_OUT) · V_OUT / (V_IN · L · f_SW)

| V_IN | L = 2.2 µH | **L = 3.3 µH** | L = 4.7 µH |
|---|---|---|---|
| 7.4 V | 1.13 A | 0.76 A | 0.53 A |
| 12 V | 2.04 A | 1.36 A | 0.96 A |
| 16.8 V | 2.46 A (82% of I_OUT) | **1.64 A (55%)** | 1.15 A (38%) |

**Worst case is maximum V_IN.**

- 2.2 µH gives excessive ripple on 4S.
- 3.3 µH is the sensible choice. It keeps ripple moderate while preserving D-CAP2 transient response.
- 4.7 µH lowers ripple but slows load-step response.

## 3. Inductor Current Ratings (3.3 µH, 16.8 V, 3 A)

- **Peak:** I_pk = I_OUT + ΔI/2 = 3 + 0.82 = **3.82 A**
- **RMS:** I_rms = √(I_OUT² + ΔI²/12) = √(9 + 0.22) = **3.04 A**

**Inductor selection:**

- L = 3.3 µH, ±20%
- I_sat ≥ 4.5–5 A. This leaves margin above the IC's current limit, so the inductor won't saturate during overload or short-circuit.
- I_rms (thermal) ≥ 3.5 A
- DCR ≤ ~25 mΩ, which gives about 0.23 W of copper loss at 3 A
- Shielded, molded type, such as a 4×4 mm or 5×5 mm molded power inductor. Shielding matters near gyros and the MCU.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI/(8 · f_SW · C_eff) + ΔI · ESR

Assumptions: 2 × 22 µF X5R ceramics, about 30 µF effective after DC-bias derating at 5 V (10 V-rated parts), ESR about 3 mΩ.

- Capacitive term: 1.64 / (8 × 650k × 30µ) ≈ **10.5 mV**
- ESR term: 1.64 × 0.003 ≈ **4.9 mV**
- **Total ≈ 15 mV p-p (~0.3%) at 16.8 V input.** Expect about 12 mV at 12 V input.

Notes:

- D-CAP2 uses an internal ramp, so all-ceramic output capacitors are fine.
- Stay within the datasheet's maximum C_OUT to keep the loop stable.
- Avoid 6.3 V-rated 0603 capacitors. At 5 V bias they can lose more than 60% of their capacitance.

## 5. Input Ripple and Capacitor RMS Current

Worst case is at D ≈ 0.5 (V_IN ≈ 10 V), which falls within 3S range:

- I_Cin,rms = I_OUT · √(D(1−D)) ≈ 3 × 0.5 = **1.5 A**
- ΔV_IN ≈ I_OUT · D(1−D) / (f_SW · C_in) ≈ 3 × 0.25 / (650k × 10µ) ≈ **115 mV p-p** with 10 µF effective

Recommendations:

- Use at least 2 × 10 µF, 25 V X5R/X7R as close as possible to VIN/GND.
- Add a 100 nF 0402 capacitor right at the pins for the high-frequency loop.
- Keep the board's bulk electrolytic or low-ESR capacitor at the battery pads to absorb ESC spikes. TPS563200's 17 V absolute limit is tight for 4S.

## 6. High-Frequency Switching Noise (Beyond the Ripple Math)

The calculated ripple is the 650 kHz triangular component. The real high-frequency content is the switch-node ringing, typically **50–300 MHz** with spikes of tens of mV on V_OUT. This depends on layout and parasitics, so it cannot be calculated without the PCB.

To mitigate it:

- Minimize the hot loop area: C_IN → high-side FET → low-side FET → GND.
- Keep the SW node copper small, and route it away from the gyro, baro, and OSD video lines.
- Optional: an RC snubber on SW (~2.2 Ω + 470 pF–1 nF), or a ferrite bead plus 10 µF post-filter feeding sensitive rails (camera/VTX 5 V, gyro LDO input).
- Measure ripple with a tip-and-barrel probe or a short ground spring, not the long ground clip, at 20 MHz bandwidth limit vs full bandwidth to separate fundamental ripple from spikes.

## 7. Low-Input Headroom

With a minimum off-time of roughly 200–250 ns, D_max is about 0.85. Regulation therefore holds down to roughly **V_IN ≈ 6.0–6.3 V**. A sagging 2S pack below about 6 V will drop the 5 V rail out of regulation.

## Summary (Recommended / Typical Design)

| Item | Value |
|---|---|
| Inductor | 3.3 µH, I_sat ≥ 4.5 A, I_rms ≥ 3.5 A, DCR ≤ 25 mΩ, shielded |
| Ripple current (4S full) | ~1.64 A p-p |
| Peak inductor current | ~3.8 A |
| C_OUT | 2 × 22 µF (10 V X5R) → ~15 mV p-p ripple |
| C_IN | 2 × 10 µF 25 V + 100 nF; RMS current up to ~1.5 A |

If you can share the actual inductor part number, the capacitor values, or the board's input voltage rating, I can redo the numbers for that exact design.