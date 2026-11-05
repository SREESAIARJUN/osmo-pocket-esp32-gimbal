# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous (Cycle 14)

I don't have the Cycle 1–13 history or your exact part number. The assumptions below are stated so you can substitute your datasheet values. If any differ, the via count and thermal numbers scale as shown.

## 1. Baseline Assumptions

| Parameter | Assumed value | Replace with |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Datasheet |
| Exposed pad (EP) | 3.25 × 3.25 mm, electrically = **Drain** | Datasheet land pattern |
| R<sub>DS(on)</sub> @ T<sub>j</sub> = 110 °C | 4.5 mΩ (≈3 mΩ @ 25 °C × 1.5) | Datasheet curve |
| R<sub>θJC(bottom)</sub> | 1.5 K/W | Datasheet |
| PCB | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | Stack-up |
| Via | 0.30 mm finished drill, **0.60 mm pad diameter** (0.15 mm ring width), 25 µm plating | — |

> **Check the via spec:** I read "0.6 mm annular ring" as a 0.6 mm pad *diameter*. If you meant a 0.6 mm ring *width* (1.5 mm pad), only about 4 vias fit on the EP. That option is not recommended.

**Power to dissipate (conduction only):**
P = I² · R<sub>DS(on),hot</sub> = 20² × 0.0045 = **1.8 W per FET**
Add switching loss from your previous cycle's calculation if the FET is hard-switched.

## 2. Array Layout

**Recommended: 4 × 4 grid, 0.80 mm pitch, centered on the EP.**

```
        EP edge (±1.625 mm)
   ┌─────────────────────────────┐
   │   ◯      ◯      ◯      ◯    │   via centers: ±0.40, ±1.20 mm (X and Y)
   │                             │   via pad Ø0.60 → outer edge at ±1.50 mm
   │   ◯      ◯      ◯      ◯    │   EP-edge margin: 0.125 mm
   │                             │   copper web between via pads: 0.20 mm
   │   ◯      ◯      ◯      ◯    │   pitch: 0.80 mm
   │                             │
   │   ◯      ◯      ◯      ◯    │   16 vias total
   └─────────────────────────────┘
```

**Rules:**
- **Pitch ≥ 0.80 mm** keeps a 0.20 mm copper web. Most fabs need at least 0.15 mm for drill registration and breakout.
- **Pitch ≤ 1.2 mm.** Beyond this, lateral spreading in the 35–70 µm EP copper becomes the bottleneck.
- **Via-to-EP edge.** Keep the via pad at least 0.1 mm inside the EP. Otherwise solder mask registration can expose the barrel to the signal-pin solder.
- **Fallback for a smaller EP (e.g. 2.6 mm):** use a 3 × 3 grid at 1.0 mm pitch (9 vias). Then add more vias outside the footprint (Section 5).

## 3. Thermal Resistance Calculation

Per-via conduction resistance through a 1.6 mm board:

| Via type | Conducting area | R per via | 16 in parallel | 9 in parallel |
|---|---|---|---|---|
| Plated, open/tented (25 µm wall) | π/4·(0.30² − 0.25²) = 0.0216 mm² | **≈192 K/W** | 12.0 K/W | 21.3 K/W |
| Conductive epoxy fill (k ≈ 8) | + small | ≈180 K/W | 11.3 K/W | 20 K/W |
| **Copper-filled (VIPPO)** | π/4·0.30² = 0.0707 mm² | **≈59 K/W** | **3.7 K/W** | 6.5 K/W |

R = L / (k·A), with k<sub>Cu</sub> = 385 W/m·K and L = 1.6 mm.

**Temperature drop across the via field at 1.8 W:**
- 16 plated vias: 1.8 × 12 = **21.6 °C**
- 16 Cu-filled vias: 1.8 × 3.7 = **6.7 °C**

**Junction estimate** (T<sub>A</sub> = 85 °C, bottom pour ~25 cm² 2 oz ≈ 15 K/W to ambient):

T<sub>j</sub> ≈ 85 + 1.8 × (1.5 + 12 + 15) ≈ **136 °C** with plated vias. This is too close to a 150 °C rating.
T<sub>j</sub> ≈ 85 + 1.8 × (1.5 + 3.7 + 15) ≈ **121 °C** with VIPPO. This is acceptable.

In practice, the top-layer copper pour and inner planes run in parallel and reduce these numbers by roughly 20–35%. Treat the figures above as conservative.

## 4. Current Capacity

If the drain current must transfer to an inner or bottom plane through these vias:
- A 0.3 mm via with 25 µm plating carries ≈1.2–1.5 A for ΔT ≈ 10 °C (IPC-2152 derived).
- 16 vias ≈ 19–24 A. This is **marginal at 20 A**.
- **Fix:** route the main drain current on the top layer through a wide pour off the EP. Alternatively, add the perimeter vias below so ≥ 24 vias share the current.

## 5. Perimeter Via Field (Strongly Recommended)

Place an extra **2 rows of 0.3/0.6 mm vias** in the drain copper pour immediately outside the package outline:
- 1.0 mm pitch, starting 0.5 mm from the body edge
- Only on sides with no gate or source pins (usually the drain-tab side and the ends)
- Adds ~12–20 vias, which cuts total via R<sub>θ</sub> by about 40% and solves the current margin
- These vias do not affect solder joints, so they can stay open and unfilled

## 6. Assembly / DFM

| Option | IPC-4761 | Cost | Solder voiding | Recommendation |
|---|---|---|---|---|
| Open via-in-pad | — | Low | Severe wicking, 30–50% voids | ❌ |
| Tented bottom side only | Type II | Low | Outgassing voids | ⚠ Prototype only |
| Plugged + mask both sides | Type III/VI | Med | OK, but mask on EP is bad | ❌ |
| **Filled + capped/planarized (VIPPO)** | **Type VII** | +10–20% | **< 25% voids** | ✅ **Production** |

**Stencil for the EP:**
- Use a windowpane aperture, **4 panes (2 × 2), 50–60% paste coverage**.
- With VIPPO, a 3 × 3 pane grid is fine. Without VIPPO, place the webs over via rows so paste avoids the holes.
- Stencil thickness 0.10–0.125 mm.
- Target ≤ 25% EP voiding, verified by X-ray.

## 7. Cycle 14 Deliverable Checklist

- [ ] Confirm EP dimensions from datasheet. Choose 4 × 4 @ 0.8 mm (≥ 3.2 mm EP) or 3 × 3 @ 1.0 mm.
- [ ] Specify **IPC-4761 Type VII copper-filled, plan