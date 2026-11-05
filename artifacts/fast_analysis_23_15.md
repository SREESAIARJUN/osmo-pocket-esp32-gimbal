# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

## 0. Assumptions

| Parameter | Value used | Change if different |
|---|---|---|
| Package | QFN-20, 5×5 mm body | Scale the array to your exposed pad (EP) |
| Exposed pad (EP) | 3.5 × 3.5 mm (drain) | **Check your datasheet land pattern** |
| R_DS(on) @ T_j = 100 °C | ~3.0 mΩ | Use the hot value, not the 25 °C value |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | |
| Via plating | 25 µm Cu (IPC Class 2/3) | |
| Drill / pad | 0.30 mm drill, **0.60 mm finished pad** (0.15 mm annular ring) | See note below |

> **Annular ring interpretation:** I read "0.6 mm annular ring" as a 0.6 mm pad diameter, which is the common usage. If you meant a 0.6 mm ring *width*, the pad would be 1.5 mm. That is not feasible inside a 3.5 mm EP at any useful via count, so you would need to clarify with your fab.

"Cycle 23" is treated as your design-iteration label. Nothing below depends on it.

---

## 1. Thermal Budget

**Conduction loss:**
P_cond = I² · R_DS(on) = 20² × 3.0 mΩ = **1.2 W**

**Total loss:** Add switching loss if the part is switched at high frequency. Budget **~1.5–2.0 W total** per FET for a hard-switched buck at 100–300 kHz.

**Target:** ΔT (pad → bottom plane) ≤ 15 °C at 2 W, so R_th(via array) ≤ **~7.5 K/W**.

---

## 2. Single-Via Thermal Resistance

Annular copper cross-section of the barrel:

A = π/4 × (0.30² − 0.25²) = **0.0216 mm²**

R_via = L / (k·A) = 1.6 mm / (0.385 W/mm·K × 0.0216 mm²) ≈ **192 K/W per via** (unfilled)

| Via count | R_th (parallel) | ΔT at 1.2 W | ΔT at 2.0 W |
|---|---|---|---|
| 9 (3×3) | 21 K/W | 26 °C | 43 °C ❌ |
| 16 (4×4) | 12 K/W | 14 °C | 24 °C ⚠️ |
| 16 + 10 perimeter | ~7.4 K/W | 9 °C | 15 °C ✅ |
| 16 filled (Cu paste, ~VIPPO) | ~6–8 K/W | 8 °C | 14 °C ✅ |

**Conclusion:** Use a 4×4 array in the pad plus perimeter vias in the drain pour, or fill the vias.

---

## 3. Array Geometry (in-pad)

**Pitch: 0.90 mm**

- Outer via centers sit at ±1.35 mm and outer pad edges at ±1.65 mm, leaving 0.10 mm inside the 1.75 mm EP edge. ✅
- Drill-to-drill web is 0.60 mm, above the typical fab minimum of 0.25–0.30 mm. ✅
- Pad-to-pad copper gap is 0.30 mm. This is fine because the pads are the same net and merge into the EP copper.

```
            EP 3.5 × 3.5 mm (drain)
   ┌───────────────────────────────────┐
   │                                   │
   │   ●       ●       ●       ●       │   ● = 0.30 drill / 0.60 pad
   │                                   │
   │   ●       ●       ●       ●       │   pitch = 0.90 mm (X and Y)
   │                                   │
   │   ●       ●       ●       ●       │   array span = 2.70 mm c-c
   │                                   │
   │   ●       ●       ●       ●       │   centered on EP
   │                                   │
   └───────────────────────────────────┘
```

**Via center coordinates** (relative to EP center, in mm):
X, Y ∈ {−1.35, −0.45, +0.45, +1.35} → 16 vias

---

## 4. Perimeter "Stitching" Vias (outside the package)

- Place them in the drain copper pour, beyond the package body edge. Keep at least 0.5 mm clearance from the solder-mask opening.
- Use the same 0.3/0.6 via, at 1.0–1.2 mm pitch.
- Put **8–12 vias** on the side(s) where the drain pour extends. Do not place them on the gate/source pin side.
- They are not under the solder joint, so tenting and wicking are not concerns.
- These vias also carry most of the layer-transition current if drain current must change layers.

---

## 5. Current-Carrying Check (if drain current transitions layers)

- An IPC-2152-style estimate for a 0.3 mm / 25 µm via is about **1.5–2 A at 10 °C rise**.
- 16 in-pad vias plus 10 perimeter vias give roughly 26 × 1.5 ≈ **39 A capacity**. That is about 2× margin for 20 A. ✅
- **Preferred approach:** keep the 20 A path on the 2 oz top layer (wide pour, ≥ 8–10 mm). Use the vias mainly for heat. Pour the same net on all layers so the vias are electrically parallel as well.

---

## 6. Solder & Manufacturing Treatment (critical)

Open 0.3 mm vias under a QFN EP will wick solder. This causes voids, starved joints, and possibly package tilt.

| Option | Thermal | Cost | Recommendation |
|---|---|---|---|
| **IPC-4761 Type VII (filled + capped, VIPPO)** | Best (filled with Cu or conductive epoxy) | $$ | ✅ Preferred for 20 A |
| Type VI (filled + covered) / plugged from bottom | Good | $ | ✅ Acceptable |
| Tented from bottom only | Fair, some wicking | ¢ | ⚠️ Prototype only |
| Open vias | Voids > 30% | — | ❌ |

**Paste stencil (EP):**
- Use a windowpane aperture: **3×3 or 4×4 sub-apertures, 50–60 % total coverage**.
- Place the apertures between via locations. This applies even with VIPPO, because it controls void percentage and float.
- Stencil thickness 0.10–0.12 mm.
- Target **< 25 % voiding** under the EP (IPC-7093).

---

## 7. Stack-Up & Plane Recommendations

| Layer | Content |
|---|---|
| L1 (2 oz) | Drain pour connected solidly to EP, **no thermal reliefs** |
| L2 (1 oz) | GND plane. Via antipads only, OR drain copper island if heat spreading is needed |
| L3 (1 oz) | Drain-net copper island under FET (≥ 10×10 mm) |
| L4 (2 oz) | Drain-net heat-spreading pour ≥ 15×15 mm. Optional heatsink/TIM pad here |

- **Connect the vias solidly on every drain-net layer.** Thermal reliefs on these vias defeat the purpose.
- If L4 drain copper is exposed for a heatsink, use an **electrically insulating TIM** (drain is at switch-node or bus voltage).

---

## 8. Summary Spec (drop into fab notes / CAD rules)

```
THERMAL VIA ARRAY – Q1..Qn (QFN-20 EP)
  Drill:            0.30 mm finished
  Pad:              0.60 mm (all layers), solid connect, no relief
  In-pad array:     4 × 4, 0.90 mm pitch, centered on EP  (16 vias)
  Perimeter:        8–12 vias, 1.0–1.2 mm pitch, in drain pour
  Plating:          ≥ 25 µm Cu barrel
  Treatment:        IPC-4761 Type VII (VIPPO, Cu/conductive fill, planar