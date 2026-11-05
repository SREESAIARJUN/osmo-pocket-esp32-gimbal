# Thermal Via Array Design: QFN-20 Power MOSFET Exposed Pad (Cycle 10)

## 0. Assumptions to Verify

| Parameter | Assumed value | Action |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Confirm against the datasheet land pattern |
| Exposed pad (EP) | 3.6 × 3.6 mm, Drain node | Confirm EP size and net |
| R<sub>DS(on)</sub> @ T<sub>J</sub> = 100 °C | ~3 mΩ | Use the datasheet hot value |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | Confirm the stackup with the fab |
| Via plating | 25 µm (IPC Class 2/3) | Confirm |
| "0.6 mm annular ring" | Interpreted as a **0.6 mm pad diameter** (0.15 mm ring) | Clarify. A literal 0.6 mm ring gives a 1.5 mm pad, which won't fit an array on a 3.6 mm EP. |

## 1. Via Array Geometry

**Grid:** 4 × 4 = 16 vias at 0.90 mm pitch, centered on the EP.

- Array span (center to center): 3 × 0.9 = 2.70 mm
- Outer extent including pads: 2.70 + 0.60 = 3.30 mm
- Margin from via pad to EP edge: (3.60 − 3.30) / 2 = **0.15 mm**
- Web between adjacent via pads: 0.90 − 0.60 = **0.30 mm** of copper (meets the typical ≥0.25 mm fab minimum)

```
      EP 3.60 mm
 ┌──────────────────────┐
 │  ○    ○    ○    ○    │   ○ = 0.3 mm drill / 0.6 mm pad
 │                      │   pitch = 0.90 mm (X and Y)
 │  ○    ○    ○    ○    │   16 vias total
 │                      │
 │  ○    ○    ○    ○    │
 │                      │
 │  ○    ○    ○    ○    │
 └──────────────────────┘
   ↑ 0.15 mm to EP edge (via pad edge)
```

**Optional extension (recommended):** Extend the top copper pour beyond the EP on the drain side, if the pinout allows. Add a second row of 4–6 vias outside the package outline at 1.0 mm pitch. These can be tented and unfilled, which is cheaper, and they add spreading capacity.

**Keep-outs:**
- No vias under gate or source pins.
- Keep ≥0.2 mm from the EP edge to the pin pads.

## 2. Thermal Calculation

**Single via (plated barrel, unfilled):**

- Barrel cross-section: A = π(0.15² − 0.125²) = 0.0216 mm²
- R<sub>via</sub> = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W**

**Array of 16 in parallel:**

| Fill option | R per via | R for 16 vias |
|---|---|---|
| Unfilled / non-conductive fill | 192 K/W | **~12 K/W** |
| Conductive epoxy (~5 W/m·K) | ~185 K/W | ~11.5 K/W (marginal gain) |
| Solid copper fill | ~59 K/W | **~3.7 K/W** |

**Dissipation per FET at 20 A:**

- Conduction loss: P = I²R = 20² × 0.003 = **1.2 W**
- Allowing for switching loss: budget about **1.5 W per FET**

**Temperature drop across the via array:**
- Non-conductive fill: ΔT ≈ 1.5 × 12 = 18 °C
- Copper fill: ΔT ≈ 1.5 × 3.7 = 5.5 °C

Spreading in the top 2 oz pour runs in parallel with the vias, so the real system θ will be lower. Still, if the via path is the primary route to a bottom-side heatsink, **copper fill is justified at 20 A**.

## 3. Electrical Check (EP carries drain current)

- R per via: ρL/A = 1.72×10⁻⁸ × 1.6 mm / 0.0216 mm² ≈ 1.27 mΩ
- 16 in parallel: ≈ 0.08 mΩ, giving P ≈ 20² × 0.08 mΩ ≈ 32 mW (negligible)
- Current per via: 20 / 16 = 1.25 A, within the ~1.5–2 A per-via guideline for a 10 °C rise.

**Margin is adequate.** Note that only 2 vias are lost if a row is removed. That still leaves 14 vias at about 1.43 A each, which is acceptable.

## 4. Fabrication Spec

- **Via-in-pad:** IPC-4761 **Type VII** (filled and capped, planarized), also called VIPPO.
  - Use non-conductive fill as the baseline, or copper fill for best thermal performance.
  - This is mandatory. Open vias wick solder, leave the EP joint starved, and cause voids over 50%.
- **Fallback if VIPPO is unavailable:** Bottom-side tenting only, with vias at the array edge. Expect worse voiding. This is not recommended at 20 A.
- **Inner planes:** Solid connection to the drain net/thermal plane. **No thermal relief spokes.**
- **Bottom side:** An exposed copper pad at least the EP size (ideally 6 × 6 mm), with mask opened for TIM or heatsink if used. If the pad is at drain potential, use an electrically insulating TIM.

## 5. Stencil / Paste (EP)

- Windowpane: 3 × 3 apertures, each about 1.0 × 1.0 mm, with 0.2 mm webs.
- Coverage: about **60–65%** of the EP area.
- Stencil: 0.12 mm (5 mil).
- **Target voiding <25%**, verified by X-ray on the first article.

## 6. Multiple MOSFETs

- **Minimum spacing between packages:** ≥3 mm edge-to-edge so the spreading zones don't overlap heavily.
- **Pour sizing:** Give each FET at least about 4 cm² of dedicated drain pour per layer.
- **Paralleled FETs:** Keep the via arrays and layouts identical so current and heat share evenly.

## 7. Cycle 10 → 11 Checklist

1. Confirm the EP dimensions and net. If the EP is larger (e.g., 3.9 mm), move to a 4 × 4 grid at 1.0 mm pitch.
2. Get the fab's quote for copper versus resin fill, and decide based on the thermal target.
3. Run a thermal simulation (or a 1.5 W/FET bench test) to confirm T<sub>J</sub> stays below 125 °C at your maximum ambient.
4. Do X-ray voiding inspection on the prototype build.