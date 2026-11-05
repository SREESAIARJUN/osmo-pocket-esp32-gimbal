# Thermal Via Array Design: QFN-20 Power MOSFETs, 20 A Continuous (Cycle 21)

## 0. Assumptions (confirm before release)

| Parameter | Assumed value | Why it matters |
|---|---|---|
| Package | 5 × 5 mm QFN-20, exposed pad (EP) 3.5 × 3.5 mm, EP = drain | Sets how many vias fit |
| R_DS(on) at T_J = 100 °C | ~4 mΩ | Sets power dissipation |
| Board | 1.6 mm, 4-layer. Outer layers 2 oz finished; inner layers 2 oz | Sets via length and spreading |
| Via plating | 25 µm (IPC Class 2/3 minimum average) | Sets via thermal resistance |
| "0.6 mm annular ring" | Interpreted as **0.6 mm pad diameter**, giving a 0.15 mm ring | If you meant a 0.6 mm ring width (1.5 mm pad), the array won't fit under the EP. See §7. |

"Cycle 21" is treated as the design revision tag. Substitute the real EP dimensions from your MOSFET datasheet; the method below scales directly.

---

## 1. Thermal Budget

**Conduction loss per FET:**

P = I² × R_DS(on) = 20² × 0.004 = **1.6 W**

Add switching loss if the FET is in a switching leg. Size for **≥2 W per device** to keep margin.

**Single via thermal resistance (barrel only):**

- Copper annulus area: A = π/4 × (0.30² − 0.25²) = 0.0216 mm²
- R_via = L / (k × A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W per via**

| Via fill | R per via | 16 vias in parallel | ΔT at 2 W |
|---|---|---|---|
| Unfilled, plated only | 192 K/W | 12.0 K/W | ~24 °C |
| Non-conductive epoxy fill + cap (IPC-4761 Type VII) | ~185 K/W | 11.6 K/W | ~23 °C |
| Conductive (Ag) epoxy fill | ~120 K/W | 7.5 K/W | ~15 °C |
| **Solid copper fill** | ~59 K/W | 3.7 K/W | **~7 °C** |

**Recommendation:** Use copper-filled VIPPO if cost allows. Otherwise use epoxy-filled and capped vias, plus the additional perimeter vias in §3.

---

## 2. Under-Pad Array: 4 × 4, 0.90 mm Pitch

```
        EP 3.50 mm × 3.50 mm (drain)
  ┌─────────────────────────────────┐
  │   ●       ●       ●       ●     │   ● = 0.30 drill / 0.60 pad
  │                                 │   pitch = 0.90 mm (X and Y)
  │   ●       ●       ●       ●     │   via centers at ±0.45, ±1.35 mm
  │                                 │   edge clearance: 1.75 − 1.35 − 0.30
  │   ●       ●       ●       ●     │                 = 0.10 mm to EP edge
  │                                 │
  │   ●       ●       ●       ●     │   16 vias total
  └─────────────────────────────────┘
```

Geometry checks:

- **Pad-to-pad gap:** 0.90 − 0.60 = 0.30 mm. This meets typical fab minimum (≥0.2 mm) and leaves drill web ≥0.6 mm.
- **Copper area occupied by vias:** about 26% of the EP. This is acceptable when the vias are filled and capped.
- **Via current capacity:** roughly 1–1.5 A each at a 10 °C rise (IPC-2152, 25 µm barrel). 16 vias give over 16 A of vertical capacity. That is sufficient, because most of the 20 A flows laterally in the top-layer pour anyway.

---

## 3. Perimeter Extension Array

The EP alone is thermally marginal at 20 A. Add vias in the drain copper pour immediately outside the footprint:

- 0.3 / 0.6 mm vias at 1.0 mm pitch, in 2 rows along the drain-side edges of the package.
- Keep them ≥0.5 mm from the source and gate pads.
- Place them where the solder mask is closed, so they can be tented and need no fill.
- Add roughly 12–20 more vias. Combined with the EP array, the vertical path drops to **~5–8 K/W** even when unfilled.

---

## 4. Layer Stack Connections

| Layer | Treatment |
|---|---|
| L1 (top) | Solid drain pour, 2 oz, extending ≥10 × 10 mm per FET. Gate and source routed out on the opposite side. |
| L2 / L3 | Drain-net plane islands under each FET. **Solid connection, no thermal reliefs.** If L2 is a GND plane, use L3 for drain spreading. Do not short nets. |
| L4 (bottom) | Exposed copper pad ≥ 8 × 8 mm, solder mask opened or ENIG. Interface to the heatsink or chassis through a TIM (gap pad ≥3 W/m·K, electrically isolating if the drain is live). |

---

## 5. Solder Paste and Assembly

**Filled and capped (VIPPO, Type VII):**
- Use a standard windowpane stencil: 4 apertures (2 × 2) at 50–60% EP coverage.
- Use a 0.12 mm stencil.

**Unfilled vias (fallback only):**
- Tent from the bottom (Type I/II) to stop solder wicking.
- Shape the paste windowpane so apertures **avoid via locations**. A 3 × 3 grid of small squares placed between the via columns works.
- Expect voiding and some loss of the via's thermal contribution. Target total voids <30% per IPC-7093.

---

## 6. Performance Estimate (per FET, 2 W)

| Path segment | R_th |
|---|---|
| Junction to case (bottom), typical | 1.0–1.5 K/W |
| Solder layer | ~0.3 K/W |
| Via field (EP + perimeter, Cu-filled) | ~3 K/W |
| Spreading plus TIM to heatsink | ~4–6 K/W |
| **Total junction to heatsink** | **~9–11 K/W, giving ΔT ≈ 18–22 °C** |

With a 60 °C heatsink, T_J ≈ 80–85 °C. That is a comfortable margin against a 150 °C T_J(max). Without a heatsink, relying on the board alone, expect R_θJA of about 25–35 K/W and T_J ≈ 110–130 °C at 50 °C ambient. That is marginal, so add copper area or airflow.

---

## 7. If "0.6 mm Annular Ring" Meant Ring Width

That would give a 1.5 mm pad diameter. Only a 2 × 2 array at about 1.9 mm pitch would fit under a 3.5 mm EP. That is 4 vias, roughly 48 K/W unfilled, which is **not adequate** for 20 A. In that case, either use the 0.6 mm pad diameter above, or move the vias outside the EP onto the drain pour.

---

## 8. Verification Checklist (Cycle 21)

1. Confirm the EP dimensions and drain assignment from the datasheet land pattern.
2. Fab note: "Vias in pad: IPC-4761 Type VII, Cu-filled (or epoxy-filled), planarized and capped, ≥25 µm barrel plating."
3. DRC: check via-to-pad clearance and that no thermal reliefs exist on the drain net in the FET region.
4. Run a thermal simulation or IR measurement at 20 A DC for ≥15 minutes. P