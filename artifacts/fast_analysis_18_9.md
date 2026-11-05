# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

"Cycle 18" isn't defined here, so this is a self-contained design. Adjust the inputs if your previous iteration fixed different values.

## 1. Design Inputs and Assumptions

| Parameter | Value used | Notes |
|---|---|---|
| Package | QFN-20, 5×5 mm body | Exposed pad (EP) ≈ 3.6 × 3.0 mm, typically **drain** |
| Drill | 0.30 mm (finished ≈ 0.25 mm) | |
| Via pad | **0.60 mm diameter** (0.15 mm ring) | See note below |
| Plating | 25 µm Cu (IPC Class 2/3) | |
| Board | 1.6 mm, 4-layer, 2 oz outer and inner | |
| R<sub>DS(on)</sub> | 2 mΩ @ 25 °C → ~3 mΩ @ 100 °C | Swap in your datasheet value |

**Note on "0.6 mm annular ring":** I've read this as a 0.6 mm via pad diameter. A literal 0.6 mm ring *width* would give a 1.5 mm pad, which won't fit an array under a 3 mm EP. If you really meant the ring width, the array collapses to 2×2 and the thermal numbers below roughly triple.

## 2. Array Layout: 4 × 3 Grid, 0.9 mm Pitch, 12 Vias

```
         Exposed pad 3.6 mm (X) × 3.0 mm (Y)
   ┌──────────────────────────────────────────┐
   │                                          │
   │   ●         ●         ●         ●        │  y = +0.90
   │                                          │
   │   ●         ●         ●         ●        │  y =  0.00
   │                                          │
   │   ●         ●         ●         ●        │  y = -0.90
   │                                          │
   └──────────────────────────────────────────┘
     x=-1.35   -0.45     +0.45     +1.35   (mm, from EP center)
```

**Clearance checks:**
- **X margin:** 1.35 + 0.30 = 1.65 mm, against an EP half-width of 1.80 mm. That leaves 0.15 mm, which is OK.
- **Y margin:** 0.90 + 0.30 = 1.20 mm, against 1.50 mm. That leaves 0.30 mm, which is OK.
- **Via-pad to via-pad gap:** 0.9 − 0.6 = 0.30 mm. Copper-to-copper on the same net is fine, and the drill-to-drill web is 0.6 mm, which is within standard fab capability.

**Supplementary vias outside the EP:** add 8–12 more vias (same size, 1.0 mm pitch) in the drain copper pour right next to the package. These go on the drain-pin side and outside the package outline. They take most of the current-carrying duty off the in-pad array and add thermal spreading.

## 3. Thermal Calculation

**Per-via conduction (1.6 mm barrel, k<sub>Cu</sub> = 390 W/m·K):**

| Via type | Cu cross-section | R<sub>θ</sub> per via | 12 in parallel |
|---|---|---|---|
| Plated only (25 µm) | 0.0216 mm² | ~190 K/W | **~16 K/W** |
| Epoxy-filled (non-conductive) | ~same | ~180 K/W | ~15 K/W |
| Cu-filled / conductive fill | 0.0707 mm² | ~58 K/W | **~4.8 K/W** |

**Conduction loss at 20 A:** P = I²·R = 400 × 0.003 = **1.2 W**. Add your switching loss if the device is in a switching leg.

**Temperature rise across the vias:**
- Plated: about 19 °C.
- Cu-filled: about 6 °C.
- The supplementary vias and 2 oz plane spreading reduce the effective rise by roughly another 30–40%.

**Recommendation:**
- **Plated, filled and capped vias are adequate for conduction-only operation at 1.2 W.**
- **If total dissipation exceeds about 2 W, specify Cu-filled vias** (or conductive fill).

## 4. Current Capacity Check

- A 0.3 mm via with 25 µm plating carries about 1.5 A at a 10 °C rise (conservative, IPC-2152-equivalent).
- 12 in-pad vias give about 18 A. That is marginal on its own.
- Adding the 8–12 supplementary vias raises total capacity to 30–36 A, which gives about 1.6× margin. ✔

Route the main 20 A path primarily on the top-layer pour. Treat the vias as a thermal path plus a parallel current path, not the sole conductor.

## 5. Fabrication and Assembly Spec

1. **Via-in-pad:** IPC-4761 **Type VII** (filled and planarized-capped). This prevents solder wicking and voiding under the EP.
   - Budget fallback: tent from the bottom side only (Type II). Expect 20–30% more voiding and specify X-ray inspection.
2. **Stencil:** windowpane EP aperture with 4 panes, ~55–60% paste coverage, and webs placed over the via rows. Use a 0.12 mm stencil.
3. **Plane connections:** use solid (no thermal relief) connections on all drain-net layers. Use thermal reliefs only on gate and source signal pins if hand rework matters.
4. **Bottom side:** place an exposed copper pour (≥ 10 × 10 mm) under the array for heatsinking or a TIM pad. Solder mask can be opened there if you'll use a thermal pad to the chassis.
5. **Keep-outs:** no vias under the gate or Kelvin-source pins. Keep the gate loop on the top layer, away from the drain array.

## 6. Verification Checklist

- [ ] Confirm EP dimensions and net assignment (drain vs. source) against the specific MOSFET datasheet.
- [ ] Fab confirms the 0.3/0.6 via-in-pad Type VII capability and aspect ratio (1.6/0.3 = 5.3:1, which is fine).
- [ ] Thermal sim or IR test: T<sub>J</sub> < 125 °C at 20 A and the worst-case ambient.
- [ ] X-ray voiding < 25% of EP area after first reflow build.

If you share the exact MOSFET part number and the prior cycle's results, I can re-fit the grid to its actual EP footprint and update the loss budget.