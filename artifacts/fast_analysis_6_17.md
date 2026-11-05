# 6-Layer Stackup: 25.5 mm AIO Flight Controller (4-in-1 ESC, 24 MOSFETs)

## 0. Assumptions

These assumptions drive the numbers below. Adjust them if your Cycle 6 targets differ.

| Parameter | Assumed value |
|---|---|
| Mounting pattern | 25.5 × 25.5 mm (M2), board outline ≈ 29 × 29 mm with corner cutouts |
| ESC topology | 4 channels × 3 half-bridges × 2 FETs = 24 FETs |
| FET package | 3.3 × 3.3 mm DFN (PowerPAK/PDFN 3x3), or 2 × 2 mm DFN if space-limited |
| Input | 2–4S (up to 17 V), 15–20 A/channel burst, ~10 A continuous |
| Thickness target | ~1.0 mm (weight vs. rigidity for 1S–4S whoop/toothpick) |
| Placement | 12 FETs top, 12 FETs bottom, mirrored |

## 1. Stackup Table

| # | Layer | Function | Cu (finished) | Dielectric to next | Thickness |
|---|---|---|---|---|---|
| L1 | **Top** | FET bank A (ch 1–2), MCU, gyro, gate drivers, phase pours | 1 oz base + plating ≈ **1.5 oz** (~50 µm) | Prepreg 1080, high-Tg | **0.075 mm** |
| L2 | **GND (PGND/AGND)** | Solid reference plane, primary heat spreader | **2 oz** (70 µm) | Core | 0.20 mm |
| L3 | **VBAT plane** | Power plane, high-side FET drain heatsink | **2 oz** (70 µm) | Prepreg 2116 | 0.11 mm |
| L4 | **GND plane** | Second solid GND, forms VBAT/GND plane capacitor with L3 | **2 oz** (70 µm) | Core | 0.20 mm |
| L5 | **Signal / phase islands** | Low-speed routing (UART, PWM, ADC), phase-node relief pours | 1 oz (35 µm) | Prepreg 1080 | **0.075 mm** |
| L6 | **Bottom** | FET bank B (ch 3–4), regulators, connectors, pads | ≈ **1.5 oz** | — | — |

**Total ≈ 1.05–1.10 mm** including copper and mask. The stack is symmetric (2 oz pair L2/L4 around the L3 center, 1080 prepregs mirrored), which helps warpage control.

**Material:** high-Tg FR-4 (Tg ≥ 170 °C, Td ≥ 340 °C, e.g. IT-180A / S1000-2 / 370HR). If budget allows, use a thermally enhanced prepreg (~1.0–1.5 W/m·K vs. 0.3) on the two 0.075 mm outer prepregs only. These are the main bottleneck between the FET pads and the inner planes.

## 2. Why This Arrangement

1. **Thin outer prepreg (75 µm) to a 2 oz GND plane.** Heat from L1/L6 FETs reaches a large inner copper spreader through the thinnest dielectric possible, even where vias can't go. It also gives the lowest loop inductance for the half-bridge commutation loop, which means less ringing and lower switching loss (less heat at the source).
2. **2 oz inner planes rather than 2 oz outer.** True 2 oz outer copper limits trace/space to about 6/6 mil, which breaks 0.4 mm-pitch MCU and gyro fan-out. Using 1 oz base plus plating on the outer layers keeps 3.5–4/4 mil capability. The bulk copper for current and heat goes inside.
3. **The VBAT plane (L3) is the best heatsink.** Every high-side FET drain tab sits at VBAT, so 12 of the 24 FETs can dump heat straight into a full-board 2 oz plane through via-in-pad.
4. **L3/L4 plane pair (110 µm).** This adds distributed bulk-capacitance-like decoupling and contains the VBAT current return.
5. **The gyro is shielded** from the power layers by the L2 GND plane directly beneath L1.

## 3. Thermal Via Strategy

The FET tabs are where most of the heat must leave.

**Via spec:** 0.20 mm drill, 0.40 mm pad, **resin- or copper-filled and capped (VIPPO, IPC-4761 Type VII)** in all exposed FET pads. Open vias would wick solder and cause voiding.

**Per-via thermal resistance through 1.05 mm:**
- Plated only (25 µm wall): ≈ 145 K/W
- Copper-filled: ≈ 50 K/W. Specify copper fill on FET pads if the fab supports it.

**Per FET pad (3.3 × 3.3 DFN, ~2.4 × 1.8 mm tab):** 3 × 3 array at 0.6 mm pitch = 9 vias, giving ≈ 5.5 K/W (Cu-filled) or ≈ 16 K/W (resin-filled) to the inner planes.

### Connection rules by node

| FET pad | Electrical node | Via connects to | Heat path quality |
|---|---|---|---|
| High-side drain tab | VBAT | L3 plane (full board) + L1/L6 VBAT pour | Excellent |
| Low-side drain tab | Phase (switch node) | Phase islands on L1/L6 + L5 relief pour only | Limited: keep the islands compact (dV/dt EMI) |
| Low-side source pins | GND | L2 + L4 via 4–6 vias beside pins | Good |
| High-side source pins | Phase | Same phase island | — |

**Low-side FETs are the thermal weak point** because their tab is the noisy phase node, which can't be connected to a big plane. Mitigations:
- Pick the low-side FET with the lowest Rds(on). It carries current during freewheel and at low duty, where most whoop operation lives.
- Size the L5 phase island to roughly 2× the FET footprint, sitting directly under the L1/L6 phase pour. It acts as a stacked spreader coupled through the thin 75 µm prepreg.
- Use a dual-FET half-bridge package (e.g. 3×3 or 5×6 "power stage") if available. It reduces the phase-node area while moving more heat out through the GND source pad.

**Stitching:**