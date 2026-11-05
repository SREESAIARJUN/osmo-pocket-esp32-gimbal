# Thermal Via Array Under QFN-20 Power MOSFETs (20 A Continuous)

*I don't have context for "Cycle 26." I've treated it as a design-iteration label. If it refers to a prior constraint set, such as stackup or a specific MOSFET part number, share it and I'll adjust the numbers.*

---

## 1. Assumptions (verify against your datasheet)

| Parameter | Assumed value |
|---|---|
| Package | QFN-20, 5 × 5 mm body |
| Exposed pad (EP) | 3.2 × 3.2 mm, drain-connected |
| R_DS(on) at T_j = 100 °C | ~2.5 mΩ |
| Conduction loss | 20² × 2.5 mΩ = **1.0 W** |
| Switching and other losses | ~0.5 W |
| **Design power per FET** | **1.5 W** |
| Board | 1.6 mm, 4-layer. Outer layers 2 oz finished, inner layers 1 oz |
| Via plating | 25 µm (IPC Class 3 minimum average) |

**Annular ring interpretation:** I'm reading "0.6 mm annular ring" as a **0.6 mm via pad diameter**, which gives a 0.15 mm ring. A literal 0.6 mm *ring* would give 1.5 mm pads, and only about 4 would fit under the EP. That would defeat the purpose.

---

## 2. Via Array Geometry

**Inside the EP: 4 × 4 grid, 16 vias**

```
        ←────── 3.20 mm EP ──────→
   ┌──────────────────────────────┐ ↑
   │   ●       ●       ●       ●  │ │
   │                              │ │   ● = 0.30 mm drill / 0.60 mm pad
   │   ●       ●       ●       ●  │ │   Pitch = 0.75 mm (X and Y)
   │                              │ 3.20 Via centers: ±0.375, ±1.125 mm
   │   ●       ●       ●       ●  │ │   Via pad edge to EP edge = 0.175 mm
   │                              │ │
   │   ●       ●       ●       ●  │ │
   └──────────────────────────────┘ ↓
```

| Check | Value | Typical fab minimum | Status |
|---|---|---|---|
| Drill-to-drill wall spacing | 0.75 − 0.30 = 0.45 mm | ≥ 0.25–0.30 mm | OK |
| Pad-to-pad gap (same net) | 0.15 mm | n/a, merged into EP | OK |
| Aspect ratio | 1.6 / 0.3 = 5.3 : 1 | ≤ 8–10 : 1 | OK, good plating |
| Pad edge to EP edge | 0.175 mm | ≥ 0.1 mm | OK |

**Outside the EP: 12–20 additional "stitch" vias**

Place a row of the same vias in the drain copper pour, starting about 0.5 mm from the package body, on 1.0 mm pitch. These lie outside the solder joint, so they need no plugging. They add about 50–70 % more vertical heat-transfer capacity.

---

## 3. Thermal Resistance Calculation

**Single via barrel** (25 µm plating, finished ID ≈ 0.25 mm):

- A = π/4 × (0.30² − 0.25²) = 0.0216 mm²
- R_via = L / (k·A) = 1.6×10⁻³ / (385 × 2.16×10⁻⁸) ≈ **192 K/W**

**Arrays:**

| Configuration | R_θ (vias only) |
|---|---|
| 16 in-pad vias | 192 / 16 ≈ **12 K/W** |
| + 16 stitch vias (32 total, with spreading penalty) | ≈ **7–8 K/W** |

Via fill does not change these figures much. Non-conductive epoxy (~0.3 W/m·K) adds nothing. Conductive fill (3–9 W/m·K) improves R_θ by only about 5–10 %. Fill is for **assembly reasons**, not thermal ones.

**Estimated junction temperature** (per FET, 1.5 W, T_amb = 50 °C):

| Path element | R_θ (K/W) |
|---|---|
| Junction → case (EP) | ~1.5 |
| Solder layer | ~0.3 |
| Top copper spreading | ~2 |
| Via array (32 vias) | ~7.5 |
| Bottom copper → ambient (≈ 600 mm² pour, still air) | ~25 |

Heat also spreads directly from the top copper to ambient, in parallel with the via path. With that included, the effective total is about **22–28 K/W**. That gives:

**T_j ≈ 50 + 1.5 × 25 ≈ 88 °C**

This leaves good margin to 150 °C. The dominant term is the **bottom-copper-to-air** path, not the vias. More vias than this gives diminishing returns. If you need a lower T_j, add copper area, a heatsink or thermal pad on the bottom side, or airflow.

---

## 4. Electrical Check (if vias carry drain current)

- Single via resistance: R = ρL/A = 1.72×10⁻⁸ × 1.6×10⁻³ / 2.16×10⁻⁸ ≈ **1.27 mΩ**
- 16 vias in parallel: **0.08 mΩ**, dissipating 20² × 0.08 mΩ = **32 mW**. This is negligible.
- Current per via is about 1.25 A. This is within IPC-2152 guidance for 0.3 mm vias (about 2 A at 10 °C rise).

---

## 5. Via-in-Pad Treatment (Critical for Assembly)

Open vias under the EP will **wick solder**. This causes voids, EP lift, and solder balls on the bottom side.

**Option A, recommended: VIPPO (IPC-4761 Type VII)**
- Vias filled with non-conductive epoxy, planarized, and copper cap-plated.
- Allows full paste coverage and the best voiding performance.
- Adds roughly 10–20 % to board cost.

**Option B, budget: bottom-side tented / plugged (Type II or III)**
- Use a solder mask plug from the bottom only. Do not tent the top, which outgasses and causes voids.
- Pair this with a windowpane stencil whose apertures avoid the via locations.

---

## 6. Stencil (Paste) Design for the EP

- **Target:** 50–65 % paste coverage of the EP.
- **Layout:** 3 × 3 windowpanes, each about 0.85 × 0.85 mm, with 0.25 mm webs.
  - With **Option A**, panes may sit over the vias.
  - With **Option B**, shift the panes so they sit between via rows. A 4 × 4 via grid naturally leaves 3 × 3 clear cells, which line up with these panes.
- **Stencil:** 0.12–0.125 mm thick, matching the fine-pitch signal pins.
- **Void target:** below 25 % (IPC-7093). Check