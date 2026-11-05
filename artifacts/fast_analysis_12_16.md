# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have context for "Cycle 12" (prior requirements, MOSFET part, or cell count). This design assumes **24 × DFN3×3 / PDFN3.3 MOSFETs (4 ESCs × 3 half-bridges), 2–4S, about 12–15 A continuous and 25 A burst per ESC**. Adjust copper weights if your current targets differ.

---

## 1. Stackup (≈1.15 mm finished, symmetric)

| # | Layer | Cu (finished) | Dielectric below | Function |
|---|---|---|---|---|
| L1 | **Top** | **2 oz (70 µm)** | — | MOSFETs, gate drivers, phase and VBAT pours, motor pads |
| | Prepreg | | **0.09 mm** (2×1080 or 106+1080, high resin) | Thin for thermal coupling L1→L2 |
| L2 | **GND plane** | **2 oz** | — | Solid return and main heat spreader. No splits under the MOSFETs. |
| | Core | | 0.20 mm | |
| L3 | Signal + GND fill | 1 oz | — | SPI (gyro), DShot, UART, USB. Referenced to L2. |
| | Prepreg | | 0.15 mm | Wide gap isolates signals from L4 noise |
| L4 | Power (split) | 1 oz | — | 5 V / 3V3 / 9 V VTX islands and a second VBAT pour |
| | Core | | 0.20 mm | |
| L5 | **VBAT plane** | **2 oz** | — | Battery bus to the high-side drains. Second heat spreader. |
| | Prepreg | | **0.09 mm** | Thin for thermal coupling L5→L6 |
| L6 | **Bottom** | **2 oz** | — | MCU, gyro, regulators, phase-node copper mirrors, GND pours |

**Thickness:** 0.07+0.09+0.07+0.20+0.035+0.15+0.035+0.20+0.07+0.09+0.07 ≈ **1.08 mm**, plus mask ≈ 1.12–1.15 mm.

The 2/2/1/1/2/2 copper pattern with mirrored dielectrics prevents bow and twist. This matters on a small, heavily populated board going through reflow twice.

**Material:**
- High-Tg FR-4 (Tg ≥ 170 °C, e.g. Shengyi S1000-2 or ITEQ IT-180A). MOSFET pads will cycle above 100 °C.
- If budget allows, use thermally enhanced prepreg (k ≈ 1.0–1.6 W/m·K) on the L1–L2 and L5–L6 gaps only.
- Surface finish: ENIG, for flat DFN pads.

**Fab note:** 2 oz inner copper next to a 0.09 mm prepreg needs high-resin-content glass to fill properly. Confirm with your fab. Many will require ≥ 0.1 mm here or limit inner copper to 1.5 oz.

---

## 2. Thermal Strategy

### 2a. The phase-node problem
The two MOSFETs in each half-bridge need different thermal paths:
- **High-side FET drain = VBAT.** Thermal vias can dump heat straight into the L5 VBAT plane (and the L4 VBAT pour). This is ideal.
- **Low-side FET drain = phase node.** It **cannot** connect to any plane. Instead, build a **"phase copper stack"**:
  - A local island on L1, L3 (where routing allows), and **L6**.
  - Stitch the islands with a filled via array.
  - The L6 island doubles as an exposed-copper heatsink surface. Use a solder-mask opening, or tin it.
- **Low-side source = GND.** Via the source pins down into L2.

### 2b. Thermal vias (VIPPO under each drain pad)
- 0.20 mm drill / 0.40 mm pad, 0.5 mm pitch.
- **Copper- or epoxy-filled and capped (via-in-pad plated over).** Open vias wick solder away from the DFN pad and cause voids.
- Fit 6–9 vias per 3×3 drain pad.

**Estimated thermal resistance of one via (1.1 mm long):**

| Via type | Conducting area | Resistance per via |
|---|---|---|
| Hollow, 25 µm plating | 0.0137 mm² | ≈ 190 K/W |
| Copper-filled | 0.0314 mm² | ≈ 80 K/W |

So **9 copper-filled vias ≈ 9 K/W per FET**, versus about 21 K/W hollow. Copper fill is worth the cost.

**Estimated conduction through the dielectric under a 9 mm² pad:**

| Prepreg | Resistance |
|---|---|
| 0.09 mm, k = 0.5 (standard) | ≈ 20 K/W |
| 0.09 mm, k = 1.5 (enhanced) | ≈ 7 K/W |

This path runs in parallel with the vias. The thin outer prepregs are therefore the second most important thermal feature, after the via fill.

### 2c. Spreading
- Four 2 oz layers (L1, L2, L5, L6) give about 0.28 mm of total copper across a 650 mm² board. That is roughly 3× the lateral spreading of a standard 1 oz six-layer board.
- Keep L2 and L5 **unbroken** beneath the MOSFET rings. Every slot forces heat and current around it.
- Stitch all GND regions L1↔L2↔L3↔L6 with a 1–1.5 mm via grid along the board edges and between ESC quadrants.

---

## 3. Placement (25.5 mm constraint)

```
 ┌───────────────────────────┐   TOP (L1): 4 ESC quadrants
 │ [ESC1 6×FET]  [ESC2 6×FET]│   -