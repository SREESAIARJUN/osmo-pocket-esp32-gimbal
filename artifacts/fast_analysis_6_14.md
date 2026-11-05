# Thermal Via Array Design: QFN-20 Power MOSFETs at 20 A Continuous

## 0. Assumptions

I don't have the earlier cycles of this design, so these are the baseline assumptions. Replace them with your values and the method still holds.

| Parameter | Assumed value |
|---|---|
| Package | QFN-20, 5 × 5 mm body, exposed pad (EP) 3.4 × 3.4 mm |
| R_DS(on) at T_J = 100 °C | ~5 mΩ (hot) |
| Conduction loss, 20 A | P = I²R = 20² × 0.005 = **2.0 W** |
| Switching and other losses (budget) | +0.5 W, so **P_total ≈ 2.5 W per FET** |
| PCB | 1.6 mm, 4-layer, 2 oz outer and 1 oz inner copper |
| Via plating | 25 µm Cu barrel |
| Via geometry | 0.3 mm finished drill, **0.6 mm pad diameter** |
| Ambient / target | T_A = 50 °C, T_J ≤ 110 °C |

**Note on "0.6 mm annular ring":** I interpret this as a 0.6 mm pad diameter, which gives a 0.15 mm ring width. A 0.6 mm ring *width* would make a 1.5 mm pad, which cannot form an array under a 3.4 mm EP. Please confirm.

---

## 1. Single-Via Thermal Resistance

Barrel cross-section, with OD 0.30 mm and ID 0.25 mm:

A = π/4 × (0.30² − 0.25²) = **0.0216 mm²**

Thermal resistance through the full board thickness, with k_Cu = 0.385 W/mm·K:

R_via = L / (k·A) = 1.6 / (0.385 × 0.0216) ≈ **192 K/W per via** (air or epoxy filled)

If the via is filled with conductive copper paste or solid Cu (VIPPO), the result is roughly **60–90 K/W per via**.

---

## 2. Array Under the Exposed Pad

**Fit check:** a 4 × 4 grid at 0.9 mm pitch spans 3 × 0.9 + 0.6 = 3.3 mm. That is under 3.4 mm, so it fits with 50 µm to the pad edge. A 5 × 5 grid at 0.8 mm pitch would need 3.8 mm and does not fit.

**Layout:** 4 × 4 grid, 0.9 mm pitch, centered on the EP. Coordinates are relative to the EP center.

```
          X = -1.35   -0.45   +0.45   +1.35
Y = +1.35    ●        ●       ●       ●
Y = +0.45    ●        ●       ●       ●
Y = -0.45    ●        ●       ●       ●
Y = -1.35    ●        ●       ●       ●
      EP outline: ±1.70 mm   |   via pad: Ø0.60 mm
```

Edge-to-edge spacing between via pads is 0.9 − 0.6 = 0.30 mm, which meets the fab minimum of 0.1–0.15 mm comfortably.

Resulting resistance:

R_array(16) = 192 / 16 = **12.0 K/W** (unfilled), or about 4.5 K/W with VIPPO.

---

## 3. Extended Array in the Drain/Thermal Copper Pour

The 16 under-pad vias alone are not enough for 2.5 W, so add vias in the drain copper just outside the package outline.

- Add **2 rows on each of the two non-signal sides** of the package (drain-tab side and opposite).
- Use the same 0.9 mm pitch.
- Start the first row 0.5 mm outside the body edge.
- Each side then holds about 2 × 6 = 12 vias, so 24 extra vias in total.

| Configuration | Via count | R_vias (unfilled) | R_vias (VIPPO) |
|---|---|---|---|
| EP only | 16 | 12.0 K/W | ~4.5 K/W |
| EP + perimeter | **40** | **4.8 K/W** | ~1.8 K/W |

The outer vias see some extra spreading resistance through the top copper, roughly 1–2 K/W with 2 oz copper. So the **effective resistance is about 6 K/W unfilled**.

---

## 4. Current-Carrying Check

This applies if the drain current transitions layers through these vias.

- A 0.3 mm via with 25 µm plating carries about **1.0–1.5 A** at 10 °C rise (IPC-2152, conservative).
- For 20 A, you need **at least 20 vias**, or 25 or more for margin.
- 40 vias gives about 0.5 A per via, so there is ample margin.
- The same array serves both the thermal and the current paths.

---

## 5. Junction Temperature Estimate (Per FET, 2.5 W)

| Path element | R_θ (K/W) |
|---|---|
| R_θJC (bottom, typical datasheet) | 1.5 |
| Solder layer, 70% EP coverage | 0.5 |
| Via array, 40 vias, unfilled, with spreading | 6.0 |
| Bottom copper pour to ambient (~15 cm² per FET, natural convection) | 18 |
| **Total** | **≈ 26 K/W** |

T_J = 50 + 2.5 × 26 ≈ **115 °C**. This is marginal against the 110 °C target.

To reach the target, apply one or more of these:
- **VIPPO filled vias:** drops the via contribution to about 2.5 K/W, giving T_J ≈ 106 °C.
- **More bottom-side copper, or a heatsink/thermal pad to the chassis:** the 18 K/W bottom-to-ambient term dominates, so this has the biggest effect.
- **Forced air at about 1 m/s:** reduces the bottom-to-ambient term to roughly 10 K/W, giving T_J ≈ 95 °C.

---

## 6. Manufacturing Rules

1. **Solder wicking.** 0.3 mm open vias under the EP will steal solder and cause voids. Use one of:
   - **Preferred:** IPC-4761 Type VII, filled and capped (VIPPO).
   - **Budget option:** Type VI, filled and covered, or bottom-side tenting only. Never tent the top under the EP, because trapped gas causes voids.
2. **Paste stencil.** Use a window-pane aperture at 60–70% EP coverage. Pane gaps should fall on the via rows: 4 × 4 vias give a 3 × 3 pane grid, with each pane about 0.75 × 0.75 mm.
3. **Plane connections.** Connect vias to inner and bottom planes **solid, with no thermal reliefs**. Thermal reliefs would add 5–10 K/W per via.
4. **Plating.** Specify a **minimum of 25 µm (Class 3)** barrel plating. At 20 µm, R_via rises about 20%.
5. **Aspect ratio.** 1.6 / 0.3 = 5.3:1, which is within standard fab capability (up to 8:1).
6. **Spacing between FETs.** Keep at least 3 mm between adjacent FET via fields so their heat-spreading areas do not overlap.

---

## Summary

- **Under the EP:** 16 vias in a 4 × 4 grid at 0.9 mm pitch.
- **Perimeter:** 24 more vias in the drain pour, for **40 vias per MOSFET**.
- **Resistance:** about 6 K/W effective with unfilled vias, about 2.5 K/W with VIPPO.
- **Current:** handles 20 A with about 0.5 A per via.
- **Recommendation:** VIPPO plus at least