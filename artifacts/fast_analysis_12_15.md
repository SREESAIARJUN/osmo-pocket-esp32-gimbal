# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

> **Note on "Cycle 12":** I don't have the outputs from previous design cycles, so this design states its assumptions explicitly. Replace them with your locked values, especially the exact part number, stackup, and loss budget, and the calculations will scale directly.

---

## 1. Baseline Assumptions

| Parameter | Assumed value | Why it matters |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Sets the exposed pad (EP) area |
| Exposed pad (drain) | 3.6 × 3.0 mm | Sets the via count |
| R<sub>DS(on)</sub> @ 25 °C / hot | 2.0 mΩ / ~3.0 mΩ (×1.5 at T<sub>J</sub> ≈ 110 °C) | Sets conduction loss |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | Sets via length and spreading |
| Via plating | 25 µm Cu (IPC Class 2/3 typical) | Sets barrel conductance |
| T<sub>ambient</sub> / T<sub>J,max</sub> | 60 °C / 125 °C design limit (150 °C abs) | Sets the thermal budget |

**Dimension clarification:** "0.6 mm annular ring" is interpreted here as a **0.6 mm via pad diameter**, which gives a 0.15 mm ring. That is a standard, manufacturable pair. If you actually mean a 0.6 mm ring width (1.5 mm pad), the array cannot fit under a 3 mm EP. Please confirm which you intend.

---

## 2. Power Dissipation Budget

- Conduction loss: P = I²·R<sub>DS(on),hot</sub> = 20² × 0.003 = **1.2 W**
- Switching loss allowance (synchronous buck, ~200–300 kHz): **~0.3–0.6 W**
- **Design target: P<sub>D</sub> = 1.8 W per MOSFET**

Allowable junction-to-ambient resistance:
Δ<sub>T</sub> = 125 − 60 = 65 °C → **R<sub>θJA</sub> ≤ 36 °C/W**

Thermal budget split:
- R<sub>θJC</sub> (die to EP, datasheet typical): ~1.5 °C/W
- Solder joint (with ≤25 % voiding): ~1 °C/W
- **Via array: target ≤ 5–8 °C/W**
- Spreading plus convection from bottom and inner copper: the remainder (~20–25 °C/W)

---

## 3. Single-Via Thermal Resistance

**Plated (unfilled) via, 0.3 mm drill, 25 µm wall:**
- Barrel cross-section: A = π(0.150² − 0.125²) = **0.0216 mm²**
- R<sub>via</sub> = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 °C/W**

**Copper-filled via (VIPPO):**
- A = π(0.15²) = 0.0707 mm² → **≈ 59 °C/W**

**Epoxy-filled (non-conductive) via:** ≈ 185 °C/W. The fill adds almost nothing thermally; its only benefit is solder control.

---

## 4. Array Layout

### Pitch selection
- Pad Ø 0.6 mm + 0.2 mm copper web gives a **0.8 mm pitch**.
- Drill-to-drill wall is 0.5 mm, which meets typical fab rules (≥ 0.25–0.3 mm hole-to-hole).
- The minimum 0.2 mm edge margin from the via pad to the EP edge is satisfied.

### Grid: 4 × 4 = 16 vias, 0.8 mm pitch, centered on the EP

```
          Exposed Pad 3.6 mm (X) × 3.0 mm (Y)
 ┌───────────────────────────────────────────┐
 │                                           │
 │     ◎ ─0.8─ ◎ ─0.8─ ◎ ─0.8─ ◎            │  ◎ = 0.3 mm drill
 │     │                                     │      0.6 mm pad
 │    0.8                                    │
 │     ◎       ◎       ◎       ◎            │  Array span: 2.4 × 2.4 mm
 │                                           │  Margin X: 0.60 mm/side
 │     ◎       ◎       ◎       ◎            │  Margin Y: 0.30 mm/side
 │                                           │
 │     ◎       ◎       ◎       ◎            │
 │                                           │
 └───────────────────────────────────────────┘
   Via centers (relative to EP center), mm:
   X = −1.2, −0.4, +0.4, +1.2
   Y = −1.2, −0.4, +0.4, +1.2
```

**Optional 5 × 4 = 20 vias:** use 0.75 mm pitch in X (span 3.0 mm, 0.3 mm margin). This is worthwhile only if you use VIPPO. With open vias it increases solder wicking.

### Array thermal resistance

| Construction | 16 vias in parallel | With ~1.3× spreading/non-uniformity derate | Δ<sub>T</sub> across board @ 1.8 W |
|---|---|---|---|
| Plated, open/tented | 12.0 °C/W | **~15.6 °C/W** | ~28 °C |
| Cu-filled VIPPO | 3.7 °C/W | **~4.8 °C/W** | ~9 °C |
| 20× VIPPO | 3.0 °C/W | ~3.8 °C/W | ~7 °C |

**Conclusion:** Plated vias alone use about 40 % of the total budget and leave thin margin at 60 °C ambient. **VIPPO (IPC-4761 Type VII) is recommended** for 20 A continuous operation.

### Supplementary perimeter vias (cheap, high value)
- Add **8–12 more vias** (same 0.3/0.6 mm) in the drain copper pour just outside the package outline, within 1–2 mm of the body.
- They need no fill because they sit outside the solder area, so there is no wicking risk.
- They lower the effective R<sub>θ</sub> by roughly another 20–30 %.

---

## 5. Current-Carrying Check (Drain Path)

The vias carry drain current to inner and bottom copper, not just heat.
- IPC-2152: one 0.3 mm plated via carries about 1.5–2 A for a 10 °C rise.
- 16 vias give **≥ 24 A capacity**, so 20 A spreads across them comfortably.
- Copper fill adds substantial headroom.
- Keep the drain pour on **all 4 layers**, connected solidly to the vias. **Do not use thermal-relief spokes** on these vias.

---

## 6. Solder and Assembly Rules

**Option A: VIPPO (preferred)**
- Cu-filled, planarized, and capped vias.
- Use a full-coverage stencil with a windowpane pattern at ~65–75 % aperture to control voiding.

**Option B: Open plated vias (cost-reduced)**
- Tent or plug the vias from the **bottom side only** with soldermask (IPC-4761 Type VI-b).
- Stencil: a **windowpane of 4 apertures** placed between via rows, with about 50–60 % paste coverage and **no paste over vias**.
- Expect voiding of 20–35 %. Verify with X-ray (target <25 % per IPC-7093).
- Avoid top-side tenting. Trapped air outgasses during reflow and produces voids or solder balls.

**Option C: Plain through-holes with no tenting.** Not acceptable. Solder wicks through to the bottom side, causing tilt, starved joints, and bottom-side shorts.

---

## 7. Copper