# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

## 1. Assumptions (please verify against your datasheet)

| Parameter | Assumed value | Notes |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Exposed pad ≈ 3.6 × 3.6 mm |
| Exposed pad function | Drain (electrical + thermal) | Typical for power QFNs |
| R_DS(on) at T_j = 100 °C | ~2.5 mΩ | Typ. 1.6 mΩ at 25 °C × ~1.5 hot factor |
| PCB | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | |
| Via plating | 25 µm (IPC Class 2/3 minimum average) | |
| Via geometry | 0.30 mm drill, **0.60 mm pad diameter** | See note below |

**Note on "0.6 mm annular ring":** I've interpreted this as a 0.6 mm *finished pad diameter*, which gives a 0.15 mm annular ring. A literal 0.6 mm ring would mean a 1.5 mm pad, which won't fit usefully under a QFN. If you meant 1.5 mm pads, the array below must be reduced to about 2 × 2.

## 2. Power Dissipation

- **Conduction loss:** P = I² · R_DS(on) = 20² × 0.0025 = **1.0 W**
- **Design budget, with switching margin:** **1.5 W per FET**

## 3. Single-Via Thermal and Electrical Resistance

**Barrel copper cross-section** (outer diameter 0.30 mm, inner diameter 0.25 mm):

A = π/4 × (0.30² − 0.25²) = **0.0216 mm²**

**Thermal resistance per via:**

R_th = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W** (unfilled)

| Fill type | R_th per via |
|---|---|
| Unfilled / air | ~190 K/W |
| Non-conductive epoxy plug | ~185 K/W (negligible improvement) |
| Conductive or copper fill (VIPPO) | ~55–60 K/W |

**Electrical resistance per via:**

R = ρL/A = 1.72e-8 × 1.6e-3 / 2.16e-8 ≈ **1.3 mΩ**

## 4. Array Layout: 4 × 4 at 0.9 mm Pitch (16 vias)

```
        Exposed pad 3.6 × 3.6 mm (copper, NSMD)
   ┌─────────────────────────────────────┐
   │                                     │
   │   ◎        ◎        ◎        ◎      │  ← row y = +1.35
   │                                     │
   │   ◎        ◎        ◎        ◎      │  ← y = +0.45
   │                                     │
   │   ◎        ◎        ◎        ◎      │  ← y = −0.45
   │                                     │
   │   ◎        ◎        ◎        ◎      │  ← y = −1.35
   │                                     │
   └─────────────────────────────────────┘
     x = −1.35  −0.45   +0.45   +1.35 mm
```

**Placement coordinates** (relative to pad center): x, y ∈ {−1.35, −0.45, +0.45, +1.35} mm

**Clearance and fit checks:**

| Check | Value | Status |
|---|---|---|
| Outer via pad edge to exposed-pad edge | 1.8 − 1.35 − 0.30 = 0.15 mm | ✓ |
| Copper web between via pads | 0.9 − 0.6 = 0.30 mm | ✓ (most fabs require ≥ 0.10–0.15 mm) |
| Drill-to-drill wall spacing | 0.60 mm | ✓ (≥ 0.25 mm typical) |
| Via density | 16 vias / 12.96 mm² ≈ 1.2 per mm² | Good |

**Array performance:**

| Metric | Unfilled | VIPPO (Cu-filled) |
|---|---|---|
| R_th of array (16 in parallel) | ~12 K/W | ~3.7 K/W |
| ΔT top-to-bottom at 1.5 W | ~18 °C | ~5.5 °C |
| Electrical R of array | 0.08 mΩ | 0.03 mΩ |
| Via I²R loss at 20 A | ~32 mW | ~12 mW |
| Current per via | 1.25 A | 1.25 A |

Current per via is within IPC-2152 guidance (~1.5–2 A for a 0.3 mm via at a 10 °C rise).

## 5. Via Fill and Solder Control (Critical)

Open 0.3 mm vias will wick solder away from the pad, causing voids and lifting. Options, in order of preference:

1. **VIPPO (IPC-4761 Type VII):** filled, capped, and planarized. This is the best thermal option (~3× improvement) with no wicking. Adds cost.
2. **Type VI (plugged and covered):** epoxy plug with bottom-side mask. Good compromise.
3. **Bottom-side tenting only (Type I):** unreliable at 0.3 mm because outgassing pops the tent. Avoid if possible.

**Paste stencil, if vias are not VIPPO:**
- Windowpane apertures: 4 × 4 or 3 × 3 grid of squares
- Target 50–65% paste coverage
- Place apertures *between* vias, not over them
- Stencil thickness 0.10–0.125 mm
- Target voiding < 25% (IPC-7093)

## 6. Supporting Copper and Stackup

| Layer | Requirement |
|---|---|
| L1 (top) | Drain pour ≥ 2× the pad area; 2 oz copper. Add 6–12 extra 0.3 mm stitching vias in the pour just outside the pad perimeter at 1.0 mm pitch. |
| L2 / L3 | Solid planes connected to the drain net. **No thermal reliefs** on these vias (direct connect). |
| L4 (bottom) | Exposed copper area of at least 10 × 10 mm per FET, mirroring the pad. Optionally leave it unmasked for a heatsink or thermal pad. |

**Current path:** Size the drain and source pours for 20 A. With 2 oz outer copper, use roughly 8–10 mm width per IPC-2152 for a ~20 °C rise, or share the current across layers.

## 7. Thermal Budget Estimate (per FET, 1.5 W)

| Element | Unfilled | VIPPO |
|---|---|---|
| R_θJC (bottom) | 1.0 K/W | 1.0 K/W |
| Solder layer | 0.3 | 0.3 |
| Via array | 12 | 3.7 |
| Spreading + bottom copper to ambient (10 × 10 mm, natural convection) | ~25 | ~25 |
| Top-side copper parallel path (reduces total) | ≈ −8 | ≈ −4 |
| **Effective R_θJA** | **~30 K/W** | **~26 K/W** |
| **T_j at T_a = 50 °C** | **~95 °C** | **~89 °C** |

Both options are within a 150 °C T_j(max) with margin. If you have airflow or a heatsink on the bottom pad, the via array becomes the dominant resistance, and VIPPO then matters much more.

## 8. Checklist

