# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC with 4-in-1 ESC (24 MOSFETs)

I don't have records of the earlier design cycles, so this is a self-contained proposal. If Cycle 21 had specific findings, such as hotspot temperatures, warpage, or fab rejections, share them and I'll adjust.

**Assumptions:** 1S–2S whoop or toothpick class, 6 single N-channel FETs per motor (DFN2x2 or DFN3x3), MCU QFN 0.4 mm pitch, LGA gyro, USB on board, finished thickness ≈ 1.0 mm.

---

## 1. Stackup

| Layer | Function | Cu (finished) | Dielectric to next | Notes |
|---|---|---|---|---|
| **L1 Top** | FETs, gate drivers, phase pours, motor pads | 1 oz (0.5 oz base + plating) | **PP 1080, 0.075 mm** | Fine-pitch capable; thin PP couples heat to L2 |
| **L2** | **Solid GND** (reference for L1) | 1 oz | Core 0.20 mm | No splits under FETs or gyro |
| **L3** | **VBAT plane** + heavy power distribution | **2 oz** | **PP 1080, 0.075 mm** | Primary heat spreader for high-side drains |
| **L4** | **GND plane** (power return) | **2 oz** | Core 0.20 mm | L3/L4 tight pair gives a low-inductance supply |
| **L5** | Signals (SPI, UART, gate drive, ADC) + GND fill | 1 oz | **PP 1080, 0.075 mm** | Route under the MCU region, away from FETs |
| **L6 Bottom** | MCU, gyro, baro, regulators, connectors | 1 oz | — | Exposed-copper thermal islands under the FET zones |

**Finished thickness:** about 0.91 mm of copper and dielectric, plus solder mask, for roughly **1.0 mm ± 10 %**.

**Symmetry:** L1/L6, L2/L5 and L3/L4 have matching copper weights. This prevents bow and twist, which matters on a thin 6-layer board with heavy inner copper.

**Material:** High-Tg FR-4 (Tg ≥ 170 °C, e.g. S1000-2 or IT-180A). The ESC zone can reach 100 °C or more locally in a hot hover. ENIG finish suits the flat LGA and QFN pads.

---

## 2. Thermal Strategy

### The key constraint: drain nodes differ
The DFN drain tab is the main heat path, and the two FET types have different drain nets:

- **High-side FETs (12):** drain = VBAT. Via the tab straight into the **2 oz L3 VBAT plane**. This is the best heat sink on the board, so put most of the via budget here.
- **Low-side FETs (12):** drain = phase node. These **cannot** tie to a plane. Heat spreads only through:
  - large phase pours on L1, mirrored on L6 directly beneath, with via-stitched copper islands;
  - the source pins into L2/L4 GND, which carry about 20–30 % of the heat.

  Keep phase islands as large as possible but bounded, since they are switching nodes and radiate EMI.

### Via-in-pad (VIPPO)
- Drill **0.20 mm**, pad 0.40 mm, **copper-filled and capped** so the voids don't wick solder.
- Pitch 0.5 mm: about **4–6 vias per DFN2x2 tab** and **9 per DFN3x3**.
- Rough per-via thermal resistance for a 1.0 mm long via with a 25 µm wall is about 160 K/W. Six vias in parallel give about **27 K/