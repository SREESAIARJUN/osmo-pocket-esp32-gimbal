# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

I don't have the schematic for the "STM32H743 AIO (Cycle 28)" board, so the numbers below use TPS563200 datasheet parameters and typical flight-controller input conditions. If you share the actual inductor, capacitors and input range, I can recompute with your values.

## 1. Assumptions

| Parameter | Value | Notes |
|---|---|---|
| Switching frequency (f_sw) | 650 kHz | D-CAP2 control, so the frequency is approximately fixed |
| V_IN nominal | 12 V | 3S battery |
| V_IN max | 16.8 V | Fully charged 4S, which is near the part's 17 V absolute limit |
| V_OUT | 5.0 V | V_FB = 0.768 V, R1 = 56.2 kΩ, R2 = 10 kΩ, giving 5.08 V |
| I_OUT | 3 A | Maximum rated load |
| C_OUT | 2 × 22 µF X5R/X7R | About 25 µF effective after DC-bias derating at 5 V |
| C_IN | 2 × 10 µF | About 10 µF effective at 12–16 V |

**Warning:** at 4S, 16.8 V leaves almost no margin to the 17 V absolute maximum, and battery transients will exceed it. A 4S-capable board should use a higher-voltage part, such as the TPS54360 or LMR36015.

## 2. Inductor ripple current

ΔI_L = V_OUT × (1 − D) / (L × f_sw), where D = V_OUT / V_IN.

| L | V_IN | D | ΔI_L (p-p) | Ripple % of 3 A | I_peak |
|---|---|---|---|---|---|
| 2.2 µH | 12 V | 0.417 | 2.04 A | 68 % | 4.02 A |
| 3.3 µH | 12 V | 0.417 | 1.36 A | 45 % | 3.68 A |
| 3.3 µH | 16.8 V | 0.298 | 1.64 A | 55 % | 3.82 A |
| **4.7 µH** | 12 V | 0.417 | **0.96 A** | **32 %** | **3.48 A** |
| **4.7 µH** | 16.8 V | 0.298 | **1.15 A** | **38 %** | **3.57 A** |

## 3. Inductor selection

To target about 30 % ripple at 12 V:

L = 5 × 0.583 / (0.9 A × 650 kHz) ≈ 5.0 µH, so use the standard value **4.7 µH**. This sits at the top of TI's recommended 2.2–4.7 µH range for a 5 V output.

Recommended specification:
- **L:** 4.7 µH ±20 %
- **I_sat:** ≥ 4.5 A. This covers the 3.57 A peak plus margin for the valley current limit and load transients.
- **I_rms:** ≥ 3.5 A. The calculated RMS current is about 3.02 A: √(3² + ΔI²/12).
- **DCR:** ≤ 30 mΩ, which keeps conduction loss to about 0.27 W.
- **Construction:** shielded, molded 5 × 5 mm or larger. Shielding keeps the magnetic field away from the IMU and magnetometer.

The 2.2 µH option leaves the peak current too close to the current limit at 3 A load, so avoid it at full load.

## 4. Output voltage ripple

ΔV_OUT ≈ ΔI_L / (8 × f_sw × C_eff) + ΔI_L × ESR

For L = 4.7 µH at 12 V (ΔI_L = 0.96 A, C_eff = 25 µF, ESR about 3 mΩ):
- Capacitive term: 0.96 / (8 × 650k × 25µ) ≈ 7.4 mV
- ESR term: 0.96 × 0.003 ≈ 2.9 mV
- **Total: about 10 mV p-p.** This rises to about 12 mV p-p at 16.8 V.

With 3.3 µH, the total is about 14 mV p-p. Either value is fine for 5 V logic, servos and receivers. D-CAP2's internal ramp keeps the loop stable with all-ceramic output capacitors.

## 5. Input ripple

- RMS current in C_IN: I_OUT × √(D(1−D)) = 3 × √0.243 ≈ **1.48 A**. Each capacitor must handle this current.
- Voltage ripple: I_OUT × D(1−D) / (f_sw × C_IN) = 0.729 / (650k × 10µ) ≈ **112 mV p-p**.

To reduce this ripple, add a 0.1 µF 0402 capacitor tight to VIN and GND, plus bulk capacitance (≥ 470 µF low-ESR) at the battery pads. The bulk capacitor also limits ESC-induced spikes that threaten the 17 V limit.

## 6. High-frequency spikes (above 50 MHz)

The 10 mV calculated ripple is the fundamental component. On real flight controllers, the dominant noise is usually switch-node ringing at about 100–300 MHz. It is set by the parasitic inductance of the hot loop, not by the inductor value. To control it:

- **Hot loop:** keep the VIN capacitor, VIN pin and GND pin loop under about 5 mm, with a solid ground plane underneath.
- **Switch node:** keep the SW copper minimal and away from gyro SPI traces and ADC inputs (VBAT, current sense).
- **Snubber (optional):** an RC snubber on SW (about 2.2 Ω + 470 pF) if ringing exceeds about 1 V overshoot.
- **Downstream filtering (optional):** a ferrite bead plus 10 µF before the 5 V rail feeds the 3.3 V LDO for the MCU and IMU.
- **Measurement:** use a tip-and-barrel (short ground spring) probe at the output capacitor with 20 MHz bandwidth limit off. A long ground lead will show false spikes.

## Summary

| Item | Recommendation |
|---|---|
| Inductor | 4.7 µH, I_sat ≥ 4.5 A, DCR ≤ 30 mΩ, shielded |
| Inductor ripple | about 0.96–1.15 A p-p |
| Output ripple | about 10–12 mV p-p, with 2 × 22 µF |
| Input capacitor RMS current | about 1.5 A; use 2 × 10 µF 25 V plus 0.1 µF |
| Main risk | 17 V maximum input on 4S; use 3S only or change the part |