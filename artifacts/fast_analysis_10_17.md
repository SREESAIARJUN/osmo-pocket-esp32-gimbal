# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have your Cycle 1–9 history, so this design is self-contained and the assumptions are stated up front. Change any of them and I'll rework the numbers.

## 0. Design Assumptions

| Parameter | Assumed value |
|---|---|
| Board | 25.5 × 25.5 mm, 4× mounting holes on 20 × 20 mm pattern, soft-mount grommets |
| Battery | 1–4S (max 16.8 V), 15 A continuous / 25 A burst per motor |
| MOSFETs | 24× single N-ch, PDFN 3.3×3.3 (or DFN2×2 for 1–2S), R<sub>DS(on)</sub> ≈ 2–4 mΩ |
| ESC topology | 4 × (3 half-bridges); BLHeli_S/Bluejay EFM8 or AM32 AT32F421 |
| FC | STM32F405/F722/G473 QFN/LQFP, gyro (ICM-42688-P / BMI270) |
| Cooling | Prop downwash (forced air), no heatsink, board edge free |

---

## 1. The Stackup (1.20 mm finished, symmetric)

| # | Layer | Function | Cu (finished) | Dielectric to next layer |
|---|---|---|---|---|
| **L1** | Top | FC MCU, gyro, ESC MCUs, gate drivers, signals | 1 oz base + plating ≈ **45 µm** | — |
| | PP | 1080 ×1, Dk≈4.2 | | **0.075 mm** |
| **L2** | Inner | **Solid GND** (reference for L1, gyro shield) | **2 oz (70 µm)** | — |
| | Core | FR-4 High-Tg (Tg≥170 °C) | | **0.20 mm** |
| **L3** | Inner | **VBAT plane + phase island copies** | **2 oz** | — |
| | PP | 2116 ×2, high resin (fills 2 oz) | | **0.20 mm** |
| **L4** | Inner | **GND plane + phase island copies** | **2 oz** | — |
| | Core | FR-4 High-Tg | | **0.20 mm** |
| **L5** | Inner | **VBAT plane** (heat spreader directly under FETs) | **2 oz** | — |
| | PP | 1080 ×1 (or thermally conductive PP, k≈1–2 W/m·K) | | **0.075 mm** |
| **L6** | Bottom | **24 MOSFETs**, phase pads, motor pads, battery pads | 1 oz base + plating ≈ **45 µm** (selective plate-up to ~70 µm on power pads if fab allows) | — |

**Total thickness:** 0.045 + 0.075 + 0.07 + 0.20 + 0.07 + 0.20 + 0.07 + 0.20 + 0.07 + 0.075 + 0.045 ≈ 1.12 mm, plus solder mask and finish ≈ **1.20 mm ±10%**.

**Why this arrangement:**
- **Symmetry.** Copper is 1/2/2/2/2/1 oz, and the dielectrics mirror each other (0.075/0.20/0.20/0.20/0.075). That prevents bow and twist on a thin, copper-heavy board, which matters for the BGA/QFN gyro and for reflow of 24 power pads.
- **Thin 0.075 mm PP between L6 and L5.** This is the thermal key. Heat from the MOSFET pads crosses only 75 µm to reach a 2 oz VBAT plane. That plane spreads the heat across the whole board even where vias can't be placed.
- **Thin PP between L1 and L2.** Gives a tight GND reference for gyro SPI and the 50 Ω-ish signals, and shields the FC from the switching layer.
- **Power planes on the inside.** All four inner layers carry heavy copper. That gives 280 µm of total copper cross-section for current *and* lateral heat spreading.

---

## 2. Component Side Split

**L6 (bottom), power side:**
- All 24 MOSFETs, arranged as 4 clusters of 6, one per corner/motor. Motor pads sit at the board edge next to their cluster.
- Battery pads go in the middle of one edge, with bulk-cap pads beside them.
- The bottom faces downwash on most frames, which gives the best convection.

**L1 (top), logic side:**
- FC MCU in the center, gyro on the soft-mounted centroid, away from phase nodes.
- 4 ESC MCUs and gate drivers placed *directly above* their FET cluster. This keeps gate loops short (gate trace ≤ 4 mm through a single via).
- No power pours on L1 except local LDO/BEC copper. L1 stays cool and quiet for the gyro.

---

## 3. Plane Strategy for the Three Node Types

A half-bridge has three thermal/electrical nodes. Each one is handled differently:

| Node | Where the heat comes from | Plane treatment |
|---|---|---|
| **VBAT** (high-side drains) | 12 high-side FETs | Large continuous pour on **L3 + L5**, plus a ring on L6. This is the best heat spreader because it is one big net. |
| **GND** (low-side sources) | 12 low-side FETs | **L2 + L4** solid; L6 GND pour around clusters. |
| **Phase** (low-side drain = high-side source) | Worst case: small isolated islands | **Replicate each of the 12 phase islands on L3 and L4** as cut-outs in the planes, stitched with via arrays. That is 3 layers of copper per phase node. |

**Phase-island rules:**
-