# Thermal Via Array Design: QFN-20 MOSFET Exposed Pad, 20 A Continuous

> **Note on "Cycle 5":** I don't have your earlier design cycles, so I've stated every assumption explicitly. If you share your Cycle 4 numbers (package, R<sub>DS(on)</sub>, stack-up, thermal results), I can turn this into a delta review.

---

## 1. Assumptions

| Parameter | Value used | Notes |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Exposed pad (EP) = **3.4 × 3.4 mm** (typical; check the datasheet) |
| EP function | **Drain** | Typical for single N-FET QFN. The vias therefore carry current as well as heat. |
| R<sub>DS(on)</sub> @ T<sub>j</sub> = 100 °C | 3.5 mΩ | 25 °C value of about 2.5 mΩ × 1.4 hot factor |
| Conduction loss | 20² × 3.5 mΩ = **1.4 W** | Worst case, 100 % duty |
| Switching and other losses | +0.4 W | Placeholder |
| **P<sub>total</sub> per FET** | **≈ 1.8 W** | |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | |
| Via plating | 25 µm Cu (IPC Class 2/3) | |
| Via geometry | 0.30 mm drill, **0.60 mm pad diameter** | See the clarification below |

**Annular ring clarification:** I've interpreted "0.6 mm annular ring" as a **0.6 mm pad diameter**, which gives a 0.15 mm annular ring.
- This is a standard via-in-pad geometry and passes IPC Class 2 and 3 (external ring ≥ 0.05 mm).
- A literal 0.6 mm ring would mean a 1.5 mm pad. Only about 4 of those fit on the EP, so that reading is not practical.

---

## 2. Array Layout

- **Pitch:** 0.80 mm, which leaves 0.20 mm copper web between 0.6 mm pads. This is the minimum recommended to avoid drill breakout and plating voids.
- **Grid:** 4 × 4 = **16 vias**.
- **Grid span:** 3 × 0.8 = 2.4 mm. Add one pad diameter (0.6 mm) and the outer extent is 3.0 mm.
- **Edge margin:** (3.4 − 3.0) / 2 = **0.20 mm** from via pad edge to EP edge.

```
        ←────────── 3.40 mm EP ──────────→
   ┌──────────────────────────────────────┐  ↑
   │  0.2                                 │  │
   │    ◉───0.8───◉──────◉──────◉         │  │
   │    │                                 │  │
   │   0.8   ┌───────┐    ┌───────┐       │  │
   │    │    │ paste │    │ paste │       │
   │    ◉    │  win  │◉   │  win  │◉      │ 3.40
   │          └───────┘    └───────┘      │  mm
   │    ◉      ┌───────┐    ┌───────┐  ◉  │
   │            │ paste │    │ paste │    │  │
   │    ◉──────◉──────◉──────◉            │  │
   │                                      │  │
   └──────────────────────────────────────┘  ↓
   ◉ = 0.30 mm drill / 0.60 mm pad, 16 places, 0.80 mm grid, centred on EP
```

**Via centre coordinates** (relative to EP centre, mm):

X, Y ∈ {−1.2, −0.4, +0.4, +1.2}, giving 16 positions.

---

## 3. Thermal Resistance of the Array

**Single via (barrel conduction through 1.6 mm):**
- Plated wall cross-section: A = π × (0.30 − 0.025) × 0.025 ≈ 0.0216 mm²
- R<sub>via</sub> = L / (k·A) = 1.6×10⁻³ / (385 × 2.16×10⁻⁸) ≈ **192 K/W** (unfilled)

**Comparison of fill options (16 vias):**

| Fill option | R per via | 16-via array | ΔT at 1.8 W |
|---|---|---|---|
| Unfilled / tented | 192 K/W | **12.0 K/W** | 21.6 °C |
| Non-conductive epoxy (VIPPO) | ~185 K/W | 11.6 K/W | 20.9 °C |
| Conductive (Ag) epoxy, k ≈ 5 | ~150 K/W | 9.4 K/W | 16.9 °C |
| **Solid copper fill** | ~59 K/W | **3.7 K/W** | **6.7 °C** |

**Estimated junction temperature (unfilled array):**

| Path element | Thermal resistance |
|---|---|
| R<sub>θJC</sub> | ~1.5 K/W |
| Solder layer | ~0.5 K/W |
| Via array | 12 K/W |
| Spreading in bottom/inner planes plus convection | ~20–25 K/W (copper-area dependent) |

This gives ≈ **35–40 K/W total**, so ΔT ≈ 65–70 °C. **T<sub>j</sub> ≈ 135 °C at 70 °C ambient**, which is marginal.

**Conclusion:** the 16-via array alone is not the bottleneck. Spreading copper on the far side matters as much. The required actions are in Section 6.

---

## 4. Current Capacity (EP = Drain)

If drain current must transfer between layers through these vias:

- IPC-2152 estimate for a 0.3 mm via with 25 µm plating: **about 1.2–1.5 A per via** for a 10 °C rise.
- 16 vias give about **19–24 A**. That is **marginal for 20 A continuous**, and it adds heat inside the barrels exactly where you're trying to remove it.

**Recommendation:**
- Route the 20 A drain current on **top-layer 2 oz copper**, fanning out from the drain leads and EP sides.
- Use the EP vias primarily for heat.
- If a layer change is unavoidable, add **≥ 12 more 0.3 mm vias outside the package body** in the drain pour. These are not under the part, so they don't affect solder.

---

## 5. Manufacturing / Assembly Specification

**Via treatment:**

| Option | Verdict |
|---|---|
| IPC-4761 **Type VII (filled and capped, VIPPO)** | **Recommended.** Flat pad, no solder wicking, enables full paste coverage. |
| Type VI (filled and covered, bottom tented) | Acceptable for cost reduction, but expect 10–20 % voiding. |
| Open vias | **Not allowed.** Solder wicks away, causing EP voids above 30 % and package tilt. |

**Paste stencil (for a 3.4 × 3.4 mm EP):**
- Use a windowpane pattern: **4 apertures of 1.2 × 1.2 mm** in a 2 × 2 layout, with 0.4 mm webs, aligned to sit *between* via rows.
- This gives ≈ 50 % paste coverage (5.76 / 11.56 mm²). The target range is 50–65 %.
- With VIPPO, you can raise coverage to about 65 % (for example, 3 × 3 apertures of 0.9 mm).
- Stencil thickness: 0.12–0.125 mm.

**Solder mask:**
- EP: SMD-free (NSMD) opening matching the EP plus 0.05 mm.
- Bottom-side via caps: open if VIPPO; tented if Type VI.

**Fabrication notes for the drawing:**
- Plating: minimum 25 µm average, 20 µm minimum at any point (Class 3 recommended for thermal cycling).
- Drill-to-copper: ≥ 0.20 mm on inner layers not connected to drain.
- Inner and bottom layers: connect vias to the **solid drain plane with direct connection (no thermal reliefs)**.

---

## 6. Recommended