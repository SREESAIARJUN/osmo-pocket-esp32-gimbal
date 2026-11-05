# Thermal Via Array Design: QFN-20 MOSFET, 20 A Continuous (Cycle 11)

I don't have the outputs from Cycles 1–10, so this design uses the stated assumptions below. Replace them with your actual package drawing, R_DS(on), and stackup, then re-run the calculations in Sections 3 and 4.

## 1. Baseline Assumptions

| Parameter | Value assumed |
|---|---|
| Package | QFN-20, 5 × 5 mm body, exposed pad (EP) 3.4 × 3.4 mm (drain) |
| R_DS(on) | 2.0 mΩ at 25 °C, ×1.5 hot → 3.0 mΩ |
| Conduction loss | P = I²R = 20² × 0.003 = **1.2 W** per FET |
| Board | 1.6 mm FR-4, 4-layer, 2 oz outer / 1 oz inner |
| Via | 0.30 mm finished drill, **0.60 mm pad diameter** (0.15 mm annular ring), 25 µm min Cu plating (IPC Class 3) |

"0.6 mm annular ring" is interpreted here as the 0.6 mm pad diameter. If you meant a 0.6 mm ring width (1.5 mm pad), the array won't fit the EP.

## 2. Array Geometry

**Under the EP: 4 × 4 grid, 0.90 mm pitch = 16 vias**

- Array span = 3 × 0.90 + 0.60 = 3.30 mm, which fits inside the 3.4 mm EP with 0.05 mm margin per side.
- Copper web between via pads = 0.90 − 0.60 = 0.30 mm. This is above the typical fab minimum of about 0.1 mm and preserves lateral spreading.
- Via center coordinates relative to the EP center (mm): X, Y ∈ {−1.35, −0.45, +0.45, +1.35}

```
        EP 3.4 x 3.4 mm
   +---------------------------+
   |  o      o      o      o   |   o = 0.3 drill / 0.6 pad
   |                           |   pitch 0.90 mm
   |  o      o      o      o   |
   |                           |
   |  o      o      o      o   |
   |                           |
   |  o      o      o      o   |
   +---------------------------+
```

**Outside the package: perimeter drain-pour vias, 12–16 more**

- Same 0.3/0.6 via, 1.0 mm pitch.
- Place them in the drain copper pour, starting 0.5 mm beyond the package outline, on the sides where current enters and exits.
- These carry most of the current to inner and bottom layers. They also need no solder-wicking mitigation.

## 3. Thermal Resistance

Barrel Cu area (unfilled) = π(0.15² − 0.125²) = 0.0216 mm²

- R_via = L / (k·A) = 1.6e-3 / (385 × 0.0216e-6) ≈ **192 K/W per via**

| Option | R per via | 16 vias in parallel | ΔT at 1.2 W |
|---|---|---|---|
| Unfilled, 25 µm plating | 192 K/W | 12.0 K/W | 14.4 °C |
| Non-conductive epoxy plug + cap (IPC-4761 Type VII) | ~185 K/W | ~11.6 K/W | ~14 °C |
| Cu-filled (solid) | ~59 K/W | 3.7 K/W | 4.4 °C |

Adding the perimeter vias and 2 oz pours lowers the effective board-path resistance by roughly another 20–30%.

## 4. Current Capacity

- With 16 EP vias only: 20 A / 16 = 1.25 A per via. That is marginal. Per IPC-2152, a 0.3 mm via with 25 µm plating handles about 1–1.5 A at a 10 °C rise.
- With 16 EP + 14 perimeter vias = 30 vias: about 0.67 A per via, which is comfortable.
- Via-field resistance: R_via,elec ≈ 0.25 mΩ each, so 30 vias in parallel ≈ 8 µΩ. Loss is about 3 mW, which is negligible.

**Recommendation:** use at least 28 total vias per FET for the 20 A drain path.

## 5. Via-in-Pad Treatment

Unfilled vias in the EP will wick solder, causing voids and possible FET tilt.

| Treatment | Cost | Verdict |
|---|---|---|
| Open vias | lowest | ❌ Not acceptable at 20 A (voiding over 30%) |
| Tented bottom only | low | ⚠ Outgassing voids; avoid |
| **Epoxy-filled + planarized + capped (VIPPO, IPC-4761 Type VII)** | moderate | ✅ **Baseline** |
| Cu-filled + capped | high | ✅ Use if T_J margin is under 15 °C |

## 6. Paste Stencil (EP)

- Stencil thickness: 0.125 mm (5 mil).
- Windowpane aperture: 3 × 3 panes, about 0.95 × 0.95 mm each, 0.25 mm webs. This gives about 50–60% EP coverage.
- With VIPPO, panes may overlap the vias. With unfilled vias, keep apertures at least 0.15 mm away from via pads.
- Target voiding below 25% (IPC-7093).

## 7. Stackup and Copper Connections

- **L1:** solid drain pour, no thermal reliefs on any drain via.
- **L2:** GND plane. Leave it solid if drain vias are not connected to it; it still spreads heat through the dielectric.
- **L3:** drain pour duplicate. This is the parallel current path and spreads heat.
- **L4:** at least 2 oz drain pour, at least 15 × 15 mm, as the heatsink/TIM landing area. Leave it soldermask-open if a heatsink or gap pad attaches.

## 8. Junction Temperature Check (T_A = 50 °C)

- R_θJC(bottom) ≈ 1.0 K/W
- Solder layer ≈ 0.3 K/W
- Via array (Type VII, with perimeter vias) ≈ 9 K/W
- Bottom spreading + convection (15 × 15 mm, no heatsink) ≈ 25 K/W

T_J ≈ 50 + 1.2 × (1.0 + 0.3 + 9 + 25) ≈ **92 °C**

This is within a 150 °C rating with about 55 °C margin. Adding a bottom-side gap pad to a chassis drops T_J to roughly 65–70 °C.

## 9. Cycle 11 Deliverables / Open Items

1. Confirm the actual EP size from the datasheet. If the EP is ≥ 3.6 mm, consider a 4 × 4 grid at 1.0 mm pitch for a wider copper web.
2. Confirm that the fab supports a 0.15 mm annular ring with Type VII capping (some fabs require ≥ 0.125 mm after drill wander).
3. Run thermal simulation with the parallel-FET spacing. Keep at least 8 mm between FETs to avoid thermal coupling.
4. Verify with an IR camera / thermocouple at 20 A, 30 min soak, and compare against the 92 °C prediction.

Send the Cycle 10 results (package drawing, measured T_J, stackup) and I'll recompute these numbers.