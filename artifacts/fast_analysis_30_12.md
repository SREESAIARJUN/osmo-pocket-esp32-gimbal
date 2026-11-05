# Thermal Via Array Under QFN-20 MOSFET Exposed Pad, 20 A Continuous

> **Note on "Cycle 30":** I don't have context from earlier iterations, so this design is self-contained. If Cycle 29 had specific constraints (pad size, stackup, Rds(on), ambient), send them and I'll revise. The assumptions below are stated so you can swap in your real numbers.

---

## 1. Assumptions

| Parameter | Value used | Notes |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Exposed pad (EP) = **drain** |
| Exposed pad | 3.30 × 3.30 mm | Check your datasheet land pattern |
| Rds(on) @ Tj = 100 °C | 3.0 mΩ | ~1.4–1.5× the 25 °C value |
| Continuous current | 20 A DC (or RMS) | |
| Board | 1.6 mm FR-4, 4-layer, 2 oz outer / 1 oz inner | |
| Via drill | 0.30 mm finished | |
| Via pad | **0.60 mm diameter** | See note below |
| Plating | 25 µm (IPC Class 2 min. average) | |
| Ambient | 50 °C | |

**Interpretation of "0.6 mm annular ring":** I've treated 0.6 mm as the via **pad diameter**, which gives a 0.15 mm annular ring. That is a standard, manufacturable pairing. If you literally mean a 0.6 mm ring width, the pad would be 1.5 mm diameter. Only one such via would fit sensibly under a 3.3 mm EP, so that reading is almost certainly not intended.

---

## 2. Power Dissipation

P_cond = I² × Rds(on) = 20² × 0.003 = **1.2 W**

Add switching loss if the device is in a switching leg. Budget **1.5–2.0 W** for a synchronous buck at a few hundred kHz. The thermal design below targets **2.0 W** for margin.

---

## 3. Single-Via Thermal Resistance

Barrel copper cross-section:
- r_out = 0.150 mm, r_in = 0.150 − 0.025 = 0.125 mm
- A = π(0.150² − 0.125²) = **0.0216 mm²**

R_via = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W per via** (unfilled; epoxy fill adds a negligible ~3%)

---

## 4. Array Layout: 4 × 4 Grid, 0.85 mm Pitch

**Fit check**
- Array outer extent = 3 × 0.85 + 0.60 = **3.15 mm**, which fits inside the 3.30 mm EP with 0.075 mm per side.
- Copper web between via pads = 0.85 − 0.60 = **0.25 mm**. That is acceptable for standard fabs; confirm the minimum with yours.

```
         Exposed pad 3.30 x 3.30 mm (Drain)
   ┌─────────────────────────────────────────┐
   │                                         │
   │    ◉        ◉        ◉        ◉         │  ◉ = 0.30 drill / 0.60 pad
   │                                         │
   │    ◉        ◉        ◉        ◉         │  Pitch X = Y = 0.85 mm
   │                 +  (EP center)          │  Array span: 2.55 mm c-c
   │    ◉        ◉        ◉        ◉         │  16 vias total
   │                                         │
   │    ◉        ◉        ◉        ◉         │
   │                                         │
   └─────────────────────────────────────────┘
   Via centers at ±0.425, ±1.275 mm from EP center (X and Y)
```

**Array barrel resistance:** 192 / 16 ≈ **12 K/W** in parallel.

**Alternative if your fab requires a ≥0.3 mm web:** use a 3 × 3 grid at 1.0 mm pitch (9 vias, ~21 K/W). Then add 6–10 extra vias in the drain copper just outside the package outline to recover the lost conductance.

---

## 5. Current Capacity of the Array

Because the EP is the drain, the vias may also carry the 20 A to inner or bottom layers.

- One 0.3 mm via with 25 µm plating carries roughly **1.0–1.5 A** at a 10 °C rise (IPC-2152-style estimate).
- 16 vias give **16–24 A** capacity, which is marginal for 20 A if all current goes through them.

**Recommendations:**
- Bring the main drain current in on the **top-layer 2 oz pour** directly to the pins/EP. Let the vias primarily handle heat and share current with the planes.
- If the drain current *must* transfer between layers, add **8–12 more vias in the drain pour outside the package footprint** (same 0.3/0.6 geometry). That brings the total to ≥24 vias and about 1 A or less per via.

---

## 6. Junction Temperature Estimate (2.0 W)

| Path element | R_th (K/W) |
|---|---|
| Rθjc (bottom, typical PQFN) | 1.0 |
| Solder layer (~60% coverage, 75 µm) | 0.5 |
| Via array (16×) | 12 (spreading in 2 oz top copper reduces the effective value to ~8) |
| Bottom/inner plane spreading + plane-to-ambient (≥ 25 cm² of 2 oz copper, natural convection) | ~20 |
| **Effective total (with top-side lateral spreading in parallel)** | **~22–25** |

**Tj ≈ 50 + 2.0 × 24 ≈ 98 °C**, which is within a 150 °C rating with good margin. At pure conduction (1.2 W), Tj is about 80 °C.

The dominant term is **plane-to-air**, not the vias. Adding more vias beyond about 16 gives diminishing returns. Adding copper area or airflow helps far more.

---

## 7. Manufacturing Rules (Via-in-Pad)

1. **Preferred: IPC-4761 Type VII** (epoxy-filled and copper-capped, "VIPPO"). This gives a flat pad and no solder wicking, so you get the best voiding and thermal performance. It adds cost.
2. **Budget option: open vias.** Tent the vias on the **bottom side only** with solder mask (≥0.1 mm overlap). Never tent on the component side, because trapped flux and gas cause voids.
3. **Stencil for the EP:** use a windowpane pattern, 4 apertures (2 × 2), with about **50–65% paste coverage**. Place the apertures *between* via rows so paste doesn't sit directly over open holes.
4. **Thermal relief:** use **solid connections** (no spokes) for every via on every layer it touches.
5. **Planes:** connect the vias to a solid drain pour on L2/L3/L4 if the net allows. Put a large exposed bottom copper area under the device if a heatsink or gap pad can be used.

---

## 8. Summary

| Item | Spec |
|---|---|
| Vias under EP | **16 (4 × 4), 0.85 mm pitch** |
| Drill / pad | 0.30 mm / 0.60 mm (0.15 mm ring) |
| Fill | VIPPO (preferred) or bottom-tented |
| Extra drain vias outside pkg | 8–12 for current sharing |
| Copper | 2 oz outer, solid fills, ≥25 cm² per device |
| Est. Tj at 2 W, 50 °C ambient | ~95–100 °C |

**Next steps:** send the actual MOSFET part number, EP dimensions, stackup, and switching frequency, and I'll tighten the grid and the thermal numbers for this iteration.