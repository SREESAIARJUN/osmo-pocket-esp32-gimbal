# Thermal Via Array Under a QFN-20 Power MOSFET, 20 A Continuous (Cycle 16)

I don't have the earlier cycles' data, so this uses the stated assumptions below. Substitute your datasheet values. The calculations are parametric.

## 1. Assumptions

| Parameter | Value used | Note |
|---|---|---|
| Package | QFN-20, 5 × 5 mm, 0.65 mm pitch | Common power-QFN footprint |
| Exposed pad (drain) | 3.6 × 3.6 mm | Check the datasheet land pattern |
| R<sub>DS(on)</sub> | 3.0 mΩ at 25 °C, about 4.5 mΩ at T<sub>J</sub> = 110 °C | |
| Via | 0.30 mm drill, **0.60 mm pad diameter** | See note below |
| Plating | 25 µm barrel copper, so finished hole ≈ 0.25 mm | |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | |

**Note on "0.6 mm annular ring":** I've read this as a 0.6 mm *pad diameter* (0.15 mm ring width). A 0.6 mm ring *width* would make a 1.5 mm via pad, and only about 4 of those fit under the exposed pad. That would defeat the purpose.

## 2. Power Budget

- Conduction loss: P = I²·R = 20² × 4.5 mΩ = **1.8 W**
- Allowance for switching and margin: design for **P<sub>D</sub> = 2.5 W per FET**

## 3. Per-Via Thermal Resistance

Barrel copper cross-section:

A = π/4·(0.30² − 0.25²) = **0.0216 mm²**

R<sub>via</sub> = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W per via (unfilled)**

| Fill option | R per via |
|---|---|
| Unfilled / tented | ~192 K/W |
| Non-conductive epoxy plugged and capped (IPC-4761 Type VII) | ~185 K/W, with no solder wicking |
| Copper-filled (VIPPO) | ~59 K/W |

## 4. Array Geometry

**Pitch selection**
- 0.6 mm pads plus at least 0.2 mm pad-to-pad web gives a minimum pitch of 0.8 mm.
- Choose **0.9 mm pitch**. It keeps the web robust (0.3 mm) and gives a clean 4 × 4 fit.
- Via centers sit at ±0.45 and ±1.35 mm from the pad center.
- Outer via pad edge is at 1.65 mm, leaving **0.15 mm inset** from the 1.8 mm pad edge.

**Inner array: 4 × 4 = 16 vias under the exposed pad**

```
        ←────────── 3.6 mm exposed pad (DRAIN) ──────────→
   ┌───────────────────────────────────────────────────────┐
   │   ○          ○          ○          ○                  │  y = +1.35
   │                                                       │
   │   ○          ○          ○          ○                  │  y = +0.45
   │                                                       │
   │   ○          ○          ○          ○                  │  y = −0.45
   │                                                       │
   │   ○          ○          ○          ○                  │  y = −1.35
   └───────────────────────────────────────────────────────┘
     x=−1.35   −0.45      +0.45      +1.35       pitch 0.9 mm
```

**Outer stitching ring: 12–20 additional vias**
- Place them in the drain copper pour, starting about 0.5 mm outside the package outline.
- Keep the same pitch.
- Put them on the drain-pin side and the two flanking sides only. Keep them away from the source and gate pins.

## 5. Thermal Result

| Configuration | R<sub>vias</sub> (parallel) | ΔT across board at 2.5 W |
|---|---|---|
| 16 vias, plugged | ~12 K/W | ~30 °C (marginal) |
| 16 + 16 ring, plugged | ~6.5 K/W (ring is less effective) | ~16 °C ✔ |
| 16 VIPPO copper-filled | ~3.7 K/W | ~9 °C ✔✔ |

**Junction temperature, recommended config (16 + ring, plugged):**

T<sub>J</sub> ≈ T<sub>A</sub> + P·(R<sub>θJC</sub> + R<sub>solder</sub> + R<sub>vias</sub> + R<sub>bottom-to-ambient</sub>)
= 50 + 2.5 × (1.2 + 0.5 + 6.5 + ~15)
≈ **108 °C**

This is under a 125 °C design limit. It requires at least 6.5 cm² of 2 oz bottom copper per FET, or a heatsink or thermal pad on the bottom.

## 6. Current-Carrying Check (if vias also transfer drain current between layers)

- IPC-2152 estimate for a 0.3 mm via with 25 µm plating: about 1.5 A per via at ΔT ≤ 10 °C.
- 20 A / 1.5 A = 14 vias minimum. 32 vias gives about 2.3× margin ✔.
- Preferred approach: carry the main 20 A on the top-layer 2 oz pour, and use the vias primarily for heat. Use the inner planes as parallel drain conductors.

## 7. Manufacturing Rules

1. **Via fill:** IPC-4761 Type VII (epoxy plugged and copper capped) is the baseline. Use VIPPO if T<sub>J</sub> margin is needed. **Do not leave vias open**, or 16 × 0.25 mm holes will wick solder and cause voids or lift the part.
2. **Stencil:** windowpane apertures on the exposed pad, 50–65% paste coverage.
   - Use a 3 × 3 or 4 × 4 grid of squares about 0.7 mm each.
   - Place them *between* via sites.
   - Stencil thickness 0.12 mm.
3. **Solder mask:** if vias are not filled, tent them from the bottom side only. Never tent on the component side.
4. **Planes:** connect vias solidly to inner and bottom drain copper with **no thermal reliefs**. Accept that this makes rework harder.
5. **Voiding target:** under 25% of exposed pad area, verified by X-ray on first articles.
6. **Multi-FET layout:** space FETs at least 8 mm apart, or stagger them, so their bottom-side copper areas don't overlap thermally.

## 8. Items to Verify Before Cycle 17

- Actual exposed-pad dimensions and the recommended land pattern from the datasheet.
- The fab's minimum annular ring and plug/cap capability and cost.
- Measured or simulated R<sub>θJA</sub>, using an IR camera at 20 A for 30 min. Target T<sub>case</sub> ≤ 95 °C at 50 °C ambient.