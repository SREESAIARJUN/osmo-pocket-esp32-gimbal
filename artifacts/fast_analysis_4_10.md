# Thermal Via Array Design: QFN-20 Power MOSFETs at 20 A Continuous (Cycle 4)

I don't have the earlier cycles' data, so this design rests on the stated assumptions below. Substitute your actual part values into the calculations in sections 1 and 2.

## 0. Assumptions and Interpretation

| Parameter | Assumed value | Notes |
|---|---|---|
| Package | QFN-20, 4×4 mm body, 0.5 mm pitch | Exposed pad (EP) ≈ 2.7 × 2.7 mm, EP = Drain |
| R<sub>DS(on)</sub> @ T<sub>j</sub>=100 °C | 3.0 mΩ | Typical 30–40 V trench FET, ~2 mΩ at 25 °C |
| Duty | Continuous conduction (D≈1, worst case) | Switching loss is not in this cycle |
| Board | 1.6 mm, 4-layer, 1 oz outer / 1 oz inner | 2 oz inner layers are recommended for 20 A |
| Via | 0.30 mm finished drill, 25 µm (1 mil) plating | IPC Class 2 minimum is ~20 µm |
| "0.6 mm annular ring" | Interpreted as a **0.60 mm via pad diameter** (0.15 mm ring) | A literal 0.6 mm ring would give a 1.5 mm pad, which won't fit an array on a 2.7 mm EP |
| Max allowed T<sub>j</sub> | 125 °C at T<sub>A</sub> = 60 °C | Derated from 150 °C abs max |

## 1. Power Dissipation

P<sub>cond</sub> = I² · R<sub>DS(on),hot</sub> = 20² × 0.003 = **1.20 W per FET**

Allow a 1.5 W design target to cover R<sub>DS(on)</sub> spread and pad/trace I²R heating.

## 2. Single-Via Thermal Resistance

Plated barrel (OD 0.30 mm, ID 0.25 mm):

A<sub>Cu</sub> = π/4 × (0.30² − 0.25²) = 0.0216 mm²

R<sub>θ,via</sub> = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W per via (unfilled)**

| Fill option | R<sub>θ</sub>/via | Notes |
|---|---|---|
| Unfilled, tented | ~190 K/W | Cheapest. Solder wicking risk if left open. |
| Non-conductive epoxy plug + cap (VIPPO) | ~180 K/W | Flat pad, no wicking. **Recommended.** |
| Conductive (Ag) epoxy fill | ~120–140 K/W | Modest gain, higher cost |
| Solid Cu fill | ~35 K/W | Best performance, premium cost |

## 3. Array Sizing

Target: about 10–12 K/W total through the board (EP to bottom-side copper).

N ≥ 192 / 12 ≈ 16 vias. Only about 9 vias fit inside a 2.7 mm EP, so the design uses the **EP array plus an extended drain-copper array**.

### 3a. Under-Pad Array (inside the EP): 3 × 3 grid

- Pitch: **0.90 mm** (≥ 1.5 × pad diameter; leaves 0.30 mm web between 0.60 mm pads)
- EP-edge clearance: ≥ 0.40 mm from pad edge to via center
- Grid centered on the EP: via centers at x, y ∈ {−0.90, 0, +0.90} mm

```
 EP 2.7 x 2.7 mm  (drain)
 +-----------------------------+
 |                             |
 |   (o)      (o)      (o)     |   o = 0.30 drill / 0.60 pad
 |                             |   pitch 0.90 mm
 |   (o)      (o)      (o)     |
 |                             |
 |   (o)      (o)      (o)     |
 |                             |
 +-----------------------------+
```

Result: 9 vias → ~21 K/W unfilled.

### 3b. Extended Drain-Copper Array (outside the package)

- Place **8–12 additional vias** in the drain copper pour beyond the drain-pin side of the package.
- Use a 1.0 mm pitch staggered grid, starting ≥ 0.5 mm from the land-pattern edge.
- These vias also serve as the **current path** into the inner drain plane (see section 4).

Combined result: 9 + 10 = 19 vias → R<sub>θ,vias</sub> ≈ 10 K/W, or ~9 K/W with VIPPO.

## 4. Current-Carrying Check (EP = Drain at 20 A)

An unfilled 0.3 mm via carries about 1.0–1.5 A at a 10 °C rise (IPC-2152 approximation).

20 A / 1.2 A ≈ **≥ 17 vias** are needed if the drain current transfers layers. The 19-via design satisfies this. If drain current stays on the top layer only, the vias are thermal-only and 9 + 6 is sufficient.

Source (pins) side:
- Add a separate array of **≥ 12 vias** on the source pour.
- Do not route 20 A through only the QFN pin fingers into a single via.

## 5. Junction Temperature Budget (per FET, P = 1.5 W)

| Path element | R<sub>θ</sub> (K/W) |
|---|---|
| R<sub>θJC(bottom)</sub> (datasheet typ.) | 1.5 |
| Solder layer (EP, ~70 % coverage) | 0.5 |
| Via array (19, VIPPO) | 9 |
| Bottom Cu spreading → ambient (≈ 6 cm² 2 oz pour, natural convection) | 25 |
| **Total** | **≈ 36 K/W** |

T<sub>j</sub> ≈ 60 + 1.5 × 36 ≈ **114 °C**, which is below the 125 °C target. Top-side copper spreading runs in parallel and adds more margin.

If you need lower T<sub>j</sub>, act on the bottom spreading term, which dominates:
- Enlarge the bottom pour, or
- Add a heatsink or thermal pad on the bottom side.

## 6. Assembly / DFM Rules

1. **Via fill:** Specify IPC-4761 Type VII (filled and capped) for vias in the EP. If cost forbids it, tent the vias on the bottom side and use a windowpane stencil.
2. **Stencil:** EP aperture split into a 2×2 or 3×3 windowpane at **50–60 % paste coverage**. Keep windows off the via locations to limit voids and float.
3. **Solder mask:** EP is NSMD (mask clearance 0.05 mm). No mask web between vias inside the EP.
4. **Thermal relief:** Use **no thermal reliefs** (solid connect) on all thermal/power vias on every plane layer.
5. **Plating spec:** Call out a 25 µm minimum average barrel plating on fab notes.
6. **Drill-to-copper:** Keep ≥ 0.20 mm clearance to non-drain nets on inner layers.
7. **Paralleled FETs:** Keep ≥ 3 mm spacing, or interleave them, so the via fields don't share a hot spot.

## 7. Summary Spec for Cycle 4

- **Under-EP:** 3×3 array of 0.30 / 0.60 mm vias, 0.90 mm pitch, VIPPO
- **Adjacent drain pour:** 10 additional vias, 1.0 mm staggered pitch
- **Source pour:** ≥ 12 vias
- **Inner/bottom planes:** 2 oz Cu, solid connections
- **Stencil:** EP windowpane at 50–60 %
- **Predicted performance:** R<sub>θ,vias</sub> ≈ 9–10 K/W; T<sub>j</sub> ≈ 115 °C at 20 A, 60 °C ambient

Send me the actual MOSFET part number and EP dimensions (or the Cycle 3 results) and I'll tighten the grid coordinates and the thermal budget to match.