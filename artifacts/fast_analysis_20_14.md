# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

## 0. Assumptions (verify against your datasheet)

| Parameter | Assumed value | Notes |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Common for integrated power stages and dual MOSFETs |
| Exposed pad (EP) | 3.4 × 3.4 mm | **Replace with your datasheet land pattern** |
| EP function | Drain (power + thermal) | If the EP is thermal-only, the current-sharing section relaxes |
| Via drill | 0.30 mm finished | |
| Via pad | 0.60 mm diameter | I read "0.6 mm annular ring" as the pad diameter, which gives a 0.15 mm ring. If you meant a 0.6 mm ring width (1.5 mm pad), the pitch changes and fewer vias fit in the pad. |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | |
| Plating | 25 µm (IPC Class 2/3 average) | |
| Rds(on) @ 100 °C | ~3 mΩ | |

I don't have context for "Cycle 20," so this is a standalone design. Share the previous iteration's constraints if you want them folded in.

---

## 1. Array Geometry: 4 × 4 Grid in Pad

- **Pitch:** 0.80 mm. This leaves a 0.20 mm copper web between via pads, which is DFM-safe and keeps the solder plane continuous.
- **Array span:** 3 × 0.8 = 2.4 mm.
- **Edge margin to EP boundary:** (3.4 − 2.4)/2 = 0.50 mm from via center, so 0.20 mm from the via pad edge.

```
        EP 3.4 x 3.4 mm  (centre = 0,0)
   +-------------------------------------+
   |                                     |
   |    o        o        o        o     |  y = +1.2
   |                                     |
   |    o        o        o        o     |  y = +0.4
   |                                     |
   |    o        o        o        o     |  y = -0.4
   |                                     |
   |    o        o        o        o     |  y = -1.2
   |                                     |
   +-------------------------------------+
      x=-1.2   -0.4     +0.4     +1.2
```

**Via coordinates (mm, relative to EP center):** x, y ∈ {−1.2, −0.4, +0.4, +1.2}, giving 16 vias.

**Supplemental vias outside the package** (required for current, see section 3): add 8–12 more vias, same 0.3/0.6 size, at 1.0 mm pitch in the drain copper pour. Place them on the EP side where the drain pour exits, within 1–2 mm of the body edge.

---

## 2. Thermal Analysis

**Single via barrel resistance (unfilled, plated):**
- Copper annulus area: A = π/4 · (0.30² − 0.25²) = 0.0216 mm²
- R_via = L / (k·A) = 1.6×10⁻³ / (385 × 0.0216×10⁻⁶) ≈ **192 K/W**

**Array of 16 in parallel:** ≈ **12 K/W**

**Copper-filled (VIPPO) alternative:**
- A = π/4 · 0.30² = 0.0707 mm², giving ≈ 59 K/W per via
- 16 vias ≈ **3.7 K/W**

**Power dissipation estimate:**
- Conduction loss: P = I²R = 20² × 0.003 = **1.2 W**
- Add switching losses for your fsw; budget about 1.5–2.5 W total.

**Temperature rise across the via array alone, at 2 W:**

| Via type | ΔT across vias |
|---|---|
| Unfilled | ~24 °C |
| Copper-filled | ~7 °C |

Spreading resistance in the planes and convection come on top of this. Target a total junction-to-ambient below ~25–30 K/W through the board.

**Recommendation:** Use **VIPPO (IPC-4761 Type VII, Cu or conductive-epoxy fill, capped and plated over)** if fsw is high or ambient is above 50 °C. Otherwise, 16 unfilled vias plus the supplemental vias is acceptable.

---

## 3. Current Capacity (EP = Drain)

- A 0.3 mm via with 25 µm plating carries roughly **1.0–1.5 A at ΔT ≈ 10 °C** (IPC-2152 equivalent cross-section of about 33 mil²).
- 20 A ÷ 1.2 A ≈ 17 vias minimum. Use **≥ 24 total** for margin and uneven current sharing.
- **16 in-pad + 8–12 outside = 24–28 vias.** ✔
- Current crowds toward the vias nearest the load path, so bias the supplemental vias toward the current exit direction.

---

## 4. Solder and Assembly Considerations

**Solder wicking:** Open 0.3 mm vias under the EP will drain paste and cause voids or tilt.

| Option | Cost | Result |
|---|---|---|
| VIPPO (filled + capped) | $$ | Best: flat pad, no wicking |
| Plugged / tented from bottom (Type VI) | $ | Acceptable; possible outgassing voids |
| Open vias | – | Avoid at 20 A |

**Stencil:**
- Use a windowpane aperture, 4 × 4 segments placed *between* via rows. Shift the segments off-via if the vias are not capped.
- Target **50–65 % paste coverage** of the EP.
- Stencil thickness: 0.12 mm (5 mil).

**Voiding target:** < 25 % EP voiding (IPC-7093). Verify by X-ray on first articles.

---

## 5. Stackup and Plane Connections

- **No thermal reliefs on these vias.** Use solid connections on all layers.
- **L1 (top):** EP pad with a solid 2 oz drain pour extending beyond the body. Aim for ≥ 150 mm² of copper per FET.
- **L2:** Solid copper. Use a GND plane if the EP is thermal-only, or a drain-net plane island if the EP is the drain. The L1-to-L2 dielectric should be thin (≤ 0.1 mm prepreg) for vertical heat flow.
- **L3:** Spreading plane.
- **L4 (bottom):** Exposed copper pad, mirror size ≥ 10 × 10 mm. Leave it solder-mask-free if you will attach a heatsink or thermal pad, using an electrically isolating TIM if the pad is the drain net.

---

## 6. DFM Checklist

| Check | Value | Status |
|---|---|---|
| Aspect ratio (1.6 / 0.3) | 5.3 : 1 | ✔ (< 8 : 1) |
| Annular ring | 0.15 mm | ✔ (≥ 0.10 mm Class 2) |
| Via-to-via web | 0.20 mm | ✔ |
| Via pad to EP edge | 0.20 mm | ✔ |
| Fill spec on fab drawing | IPC-4761 Type VII | Add note |

---

## 7. Summary

**Per MOSFET:**
- 16 in-pad vias at 0.8 mm pitch (4 × 4), plus 8–12 supplemental vias at 1.0 mm pitch in the drain pour.
- 24–28 vias total, VIPPO preferred.
- Solid plane connections and a 2 oz outer layer.
- Expected via-array thermal resistance: about 4 K/W (filled) or about 12 K/W (unfilled).

If you send the actual EP dimensions, Rds(on), and fsw, I can recompute the via count