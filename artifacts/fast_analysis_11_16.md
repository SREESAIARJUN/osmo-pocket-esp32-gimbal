# 6-Layer Stackup: 25.5×25.5 mm AIO FC, 4-in-1 ESC with 24 Discrete MOSFETs

> **Note on "Cycle 11":** I don't have your earlier cycle notes. This design uses the assumptions below. Send me what changed in Cycle 10 (FET part, current target, failures seen) and I'll adjust it.

**Assumptions**
- **Power:** 2–4S, about 12 A continuous and 20 A burst per motor.
- **FETs:** 24 × N-channel in DFN 3.3×3.3 or 2×2 packages, 6 per motor in 3 half-bridges. Rds(on) ≈ 5–7 mΩ when hot.
- **Thickness:** finished board ≈ 1.0–1.1 mm, which suits whoop and toothpick frames.
- **Placement:** all FETs on the top layer (L1). MCU, gyro and regulators on the bottom layer (L6), so the gyro is as far from the heat as possible.

---

## 1. Layer Stack

| # | Layer | Function | Cu (finished) | Dielectric below | Notes |
|---|---|---|---|---|---|
| L1 | Top | FETs, phase nodes, motor pads, bulk caps | **2 oz (70 µm)** | PP **0.10 mm** thermal-enhanced | Large pours, no thermal reliefs on FET pads |
| L2 | GND | Main heat spreader and return plane | **2 oz** | Core 0.15 mm | Solid, unbroken except for vias |
| L3 | VBAT | Power plane and second spreader | **2 oz** | PP **0.20 mm** (center) | Tight L2–L3 coupling acts as plane capacitance against ringing |
| L4 | GND | Shield and third spreader | **2 oz** | Core 0.15 mm | Isolates power layers from logic |
| L5 | Signal | Gate drive, SPI, UART, phase-sense | 1 oz | PP 0.10 mm | Referenced to L4 |
| L6 | Bottom | MCU, gyro, BEC/LDO, connectors | 1 oz | — | Gyro over a quiet GND island |

**Thickness budget**
- Copper: 4×70 + 2×35 = 350 µm
- Dielectric: 100 + 150 + 200 + 150 + 100 = 700 µm
- Plus mask and finish
- **Total ≈ 1.08 mm ±10%**

**Why this arrangement**
- **Three inner 2 oz planes (L2–L4)** do most of the lateral heat spreading. The tiny board has almost no free air surface, so heat must flow sideways into the whole copper mass.
- **Thin 0.10 mm prepreg directly under the FETs** minimizes the vertical thermal resistance from L1 to L2. This is the most important dielectric in the stack.
- **The thick 0.20 mm center layer** keeps the noisy power layers (L1–L3) apart from the logic layers (L4–L6).

---

## 2. Materials

- **L1–L2 prepreg:** thermally enhanced FR-4-class prepreg, k ≈ 1.0–2.0 W/m·K. Standard FR-4 is about 0.3 W/m·K, so this is roughly a 3–6× improvement in L1→L2 conduction. Ask your fab what they stock. Ventec and Isola, for example, both offer thermally conductive grades.
- **Other layers:** High-Tg FR-4 (Tg ≥ 170 °C). FET junctions near 125 °C will put the board well above 100 °C locally.
- **Resin fill for 2 oz inner copper:** Use high-resin prepreg, such as 2×1080 or 106+1080 constructions. Have the fab confirm that 0.10 mm pressed thickness will fill 2 oz copper without voids. If they can't, go to 0.12 mm rather than drop to 1 oz.
- **Finish:** ENIG, which gives flat DFN pads and reliable voiding control under exposed pads.

---

## 3. Thermal Via Strategy (the critical part)

**Via size and type**
- 0.20 mm drill, 0.45 mm pad, through L1 to L6.
- Use **VIPPO**: copper-filled and capped vias placed directly in the drain pads.

**Count per FET**
- DFN 3.3×3.3 drain pad (about 2.4×1.7 mm): **6 vias at 0.5–0.55 mm pitch** (3×2 grid).
- DFN 2×2: 3–4 vias.

**Per-via vertical resistance (1.08 mm length)**

| Via fill | Approx. R per via | 6 vias in parallel |
|---|---|---|
| Plated only (25 µm wall) | ~150 K/W | ~25 K/W |
| Copper-filled (VIPPO) | ~55 K/W | **~9 K/W** |

- Conductive-epoxy fill falls in between and is not worth the cost prem