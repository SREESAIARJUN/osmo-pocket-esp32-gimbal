# TPS563200 (5 V / 3 A) Ripple and Inductor Sizing

**I don't have the schematic for the "STM32H743 AIO (Cycle 22)" board.** The numbers below use the TPS563200 datasheet parameters and typical flight-controller AIO assumptions. Replace the assumed values with your actual BOM values (L, C_out, V_in range) to get final figures.

---

## 1. Key Parameters

| Parameter | Value | Source |
|---|---|---|
| V_IN range | 4.5 – 17 V | Datasheet |
| V_OUT | 5.0 V | Target |
| I_OUT(max) | 3 A | Target |
| f_SW | ~650 kHz | Datasheet (D-CAP2, pseudo-fixed) |
| V_FB | 0.768 V | Datasheet |
| Battery | 2S–4S LiPo (7.4 – 16.8 V) | Assumed |

> ⚠️ **Input voltage limit:** The TPS563200's absolute operating maximum is 17 V. A fully charged 4S pack (16.8 V) leaves almost no margin for motor/ESC voltage spikes. 5S/6S is **not** supported. If this AIO is marketed for 2–6S, check whether the TPS563200 is fed from a pre-regulator. Otherwise, add TVS clamping and a large bulk capacitor.

---

## 2. Feedback Divider

V_OUT = 0.768 × (1 + R1/R2)

With R2 = 10 kΩ, R1 ≈ **56.2 kΩ** gives 5.08 V. 54.9 kΩ gives 4.98 V.

---

## 3. Inductor Ripple Current

The worst case is at maximum V_IN, where duty cycle is lowest:

ΔI_L = V_OUT × (1 − D) / (L × f_SW), where D = V_OUT / V_IN

**Duty cycle:**
- At 16.8 V (4S full): D = 0.298
- At 12.0 V (3S nominal): D = 0.417
- At 7.4 V (2S nominal): D = 0.676

**Ripple current by inductor value:**

| L | ΔI_L at 16.8 V | % of 3 A | ΔI_L at 12 V |
|---|---|---|---|
| 2.2 µH | 2.46 A | 82% ❌ | 2.04 A |
| 3.3 µH | 1.64 A | 55% | 1.36 A |
| **4.7 µH** | **1.15 A** | **38%** ✅ | **0.95 A** |
| 6.8 µH | 0.79 A | 26% | 0.66 A |

**For a 30% ripple target:** L = 3.51 V / (0.9 A × 650 kHz) ≈ 6.0 µH.

**Recommendation: 4.7 µH.** It balances ripple, transient response, and footprint. D-CAP2 control also needs some ripple to regulate stably, so very large inductance is not desirable. Check the datasheet's recommended L/C_out table for 5 V output and stay within it.

---

## 4. Inductor Current Ratings (L = 4.7 µH, V_IN = 16.8 V)

- **Peak current:** I_pk = 3 + 1.15/2 = **3.58 A**
- **RMS current:** I_RMS = √(3² + 1.15²/12) = **3.02 A**
- **Saturation current (I_sat):** at least **4.5–5 A**. This keeps the inductor out of saturation during overcurrent-limit events and load transients.
- **DCR:** at most 25 mΩ. Copper loss is about 3.02² × 0.020 ≈ **0.18 W**.
- **Example parts:** shielded 4.7 µH, 5 × 5 mm or 4 × 4 mm class, such as the Würth 744383560047 or Coilcraft XGL4030-472. Verify that I_sat is at least 5 A.

---

## 5. Output Voltage Ripple

ΔV_OUT ≈ ΔI_L / (8 × f_SW × C_eff) + ΔI_L × ESR

Assumptions:
- C_out = 2 × 22 µF X5R 0805/1206.
- After DC-bias derating at 5 V, effective capacitance is about **30 µF**.
- ESR is about 3 mΩ.

At worst case (ΔI_L = 1.15 A):
- Capacitive term: 1.15 / (8 × 650k × 30µ) = **7.4 mV**
- ESR term: 1.15 × 0.003 = **3.5 mV**
- **Total ≈ 11 mV p-p (≈ 0.2%)** of switching-frequency ripple

Board measurements will typically show **20–50 mV of HF spikes** from switch-node ringing. These come from layout and parasitic inductance, not from the LC filter. To reduce them:
- Keep the switch-node copper small.
- Place C_in tight to the VIN and GND pins.
- Optionally add an RC snubber (~2.2 Ω + 1 nF) or a 0402 100 nF at the load.
- Measure with a probe ground spring, not the long ground lead.

**Effect on the STM32H743 and sensors:**
- The H743's own LDO/SMPS rejects ~11 mV easily.
- For gyro and baro supplies, follow this 5 V rail with a 3.3 V LDO or ferrite + capacitor filter. 650 kHz and its harmonics can couple into the IMU.

---

## 6. Input Capacitor Ripple Current

I_Cin(RMS) = I_OUT × √(D(1−D))

- At 16.8 V: 3 × √(0.298 × 0.702) = **1.37 A**
- Worst case (D = 0.5, near 10 V): **1.5 A**

Use 2 × 10 µF, 25 V (or higher), X5R/X7R, placed at the VIN pins. Add a 100 nF high-frequency decoupler. On a drone, a 35 V rating is preferable so the capacitors keep useful capacitance under DC bias.

---

## Summary

| Item | Value |
|---|---|
| Inductor | **4.7 µH**, I_sat ≥ 5 A, DCR ≤ 25 mΩ |
| Inductor ripple (worst case) | 1.15 A p-p (38%) |
| Peak / RMS inductor current | 3.58 A / 3.02 A |
| C_out | 2 × 22 µF X5R (≈ 30 µF effective) |
| Output ripple (theoretical) | ~11 mV p-p |
| C_in | 2 × 10 µF ≥ 25 V + 100 nF |
| Feedback divider | R1 = 56.2 kΩ, R2 = 10 kΩ |
| Max battery | 4S only (17 V limit) |

If you share the actual inductor and capacitor part numbers from the Cycle 22 BOM, or the board's rated input range, I can recompute with exact values and derating curves.