# TPS563200 5 V / 3 A Buck: Ripple and Inductor Sizing

**Assumption:** I don't have the specific schematic for the "STM32H743 AIO (Cycle 31)" board. This analysis uses TPS563200 datasheet parameters and typical flight-controller input conditions (2S–4S LiPo). Substitute your actual component values where they differ.

## 1. Key Parameters

| Parameter | Value |
|---|---|
| Topology | Synchronous buck, D-CAP2 control |
| Switching frequency (f_sw) | 650 kHz (nominal) |
| V_IN range | 4.5–17 V (so 4S max at 16.8 V; no margin for 5S/6S) |
| V_REF | 0.768 V |
| V_OUT | 5.0 V (e.g., R1 = 54.9 kΩ, R2 = 10 kΩ gives 4.98 V) |
| I_OUT(max) | 3 A |
| Datasheet recommended L for 5 V output | 3.3 µH (range 3.3–4.7 µH) |
| Recommended C_OUT | 20–68 µF ceramic |

## 2. Inductor Ripple Current

ΔI_L = V_OUT × (V_IN − V_OUT) / (V_IN × L × f_sw)

With L = 3.3 µH (L × f_sw = 2.145):

| Input | V_IN | Duty cycle | ΔI_L (p-p) | I_peak at 3 A |
|---|---|---|---|---|
| 2S nominal | 7.4 V | 0.68 | 0.76 A | 3.38 A |
| 3S nominal | 12.0 V | 0.42 | 1.36 A | 3.68 A |
| 4S full (worst case) | 16.8 V | 0.30 | **1.64 A** | **3.82 A** |

With L = 4.7 µH at 16.8 V: ΔI_L = 1.15 A, giving I_peak = 3.58 A.

**Observations:**
- At 4S with 3.3 µH, ripple is about 55% of I_OUT. This is high, but it is within the datasheet design envelope.
- The "ideal" 30% ripple would need about 6 µH. That exceeds the D-CAP2 recommended range and risks loop instability and a slow transient response, so 4.7 µH is the practical ceiling.
- The worst-case peak (about 3.8 A) is close to the IC's current-limit region. If you really draw a sustained 3 A at 4S, prefer 4.7 µH.

## 3. Inductor Selection Criteria

- **Inductance:** 3.3 µH (datasheet default) or 4.7 µH (lower ripple for 4S builds).
- **Saturation current (I_sat):** at least 4.5 A, giving about 20% margin over the 3.82 A peak.
- **RMS current:** I_RMS = √(I_OUT² + ΔI²/12) = √(9 + 0.22) ≈ 3.04 A, so choose I_rated ≥ 3.5 A.
- **DCR:** below 25 mΩ. At 20 mΩ, copper loss is about 0.18 W.
- **Construction:** shielded (molded or composite) to limit radiated EMI near the IMU, baro and compass.
- **Example candidates:** Würth 744383560-33 / -47, Coilcraft XAL4030-332 / XAL4040-472, Sunlord MWSA0503S-3R3.

## 4. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 × f_sw × C_eff) + ΔI_L × ESR

Assume 2 × 22 µF X5R 10 V (0805/1206). DC-bias derating at 5 V leaves about 25–30 µF effective. Use 30 µF:

| Case | ΔV_OUT (capacitive term) |
|---|---|
| 16.8 V, 3.3 µH | 1.64 / (8 × 650k × 30µ) ≈ **10.5 mV p-p** |
| 12 V, 3.3 µH | ≈ 8.7 mV p-p |
| 16.8 V, 4.7 µH | ≈ 7.4 mV p-p |

- ESR is negligible with ceramics (a few mΩ contributes under 5 mV).
- D-CAP2 tolerates all-ceramic output caps. Keep C_OUT within 20–68 µF so the loop remains stable.

## 5. High-Frequency Switching Spikes

On top of the 650 kHz triangle ripple, you will see ringing at each switch-node edge, typically 50–300 MHz and 20–100 mV with poor layout. This is driven by hot-loop inductance (V_IN cap → high-side FET → low-side FET → GND).

**Mitigations:**
- Place a 0.1 µF 0402 input cap directly at the VIN/GND pins, alongside 10 µF bulk.
- Keep the switch-node copper small, and route it away from the gyro SPI and analog lines.
- Add an optional ferrite bead plus 10 µF post-filter if feeding a camera or VTX.
- Add an optional RC snubber on SW (about 2.2 Ω + 470 pF) if ringing is excessive.

## 6. Input Capacitor Ripple

- **RMS current:** I_CIN,RMS = I_OUT × √(D(1−D)) ≈ 1.48 A at 12 V (maximum about 1.5 A at D = 0.5).
- **Ripple voltage:** ΔV_IN ≈ I_OUT × D(1−D) / (f_sw × C_IN) ≈ 112 mV p-p with 10 µF effective.
- **Recommendation:** use 2 × 10 µF, 25 V X5R/X7R ceramics, plus the board's bulk electrolytic or low-ESR cap to absorb ESC-induced spikes.
- **Voltage rating:** a 4S input is close to the 17 V absolute maximum. Use a TVS diode (e.g., SMBJ15A-class) or a pre-regulation stage if you see voltage transients from the ESCs.

## Summary

| Item | Recommendation |
|---|---|
| Inductor | 3.3 µH (2S–3S) or 4.7 µH (4S), I_sat ≥ 4.5 A, DCR < 25 mΩ, shielded |
| Worst-case ripple current | 1.64 A p-p (3.3 µH, 16.8 V) |
| Output ripple | ~10 mV p-p switching component, plus HF spikes dependent on layout |
| C_OUT | 2 × 22 µF 10 V X5R |
| C_IN | 2 × 10 µF 25 V + 0.1 µF HF decoupling |

If you can share the actual V_IN range, inductor part number, and capacitor values from the Cycle 31 schematic, I can recompute with exact figures.