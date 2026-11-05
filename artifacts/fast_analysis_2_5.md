# Thermal Via Array Design: QFN-20 MOSFET, 20 A Continuous (Cycle 2)

I don't have your Cycle 1 results, so this cycle is built from first principles. The assumptions below are flagged so you can swap in your actual datasheet and stackup values.

## 1. Design Basis and Assumptions

| Parameter | Assumed value | Replace with |
|---|---|---|
| Package | QFN-20, 5×5 mm body | Datasheet |
| Exposed pad (EP) | 3.3 × 3.3 mm | Datasheet land pattern |
| R_DS(on) at T_j ≈ 100 °C | 3.0 mΩ | Datasheet (hot value) |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | Fab stackup |
| Via plating | 25 µm Cu | Fab capability |
| Via geometry | 0.3 mm drill, **0.6 mm pad diameter** | — |

On "0.6 mm annular ring": I've interpreted this as a 0.6 mm finished pad diameter, which gives a 0.15 mm ring. If you literally mean a 0.6 mm ring width, the pad would be 1.5 mm. That size is not usable inside a 3.3 mm EP. Please confirm.

## 2. Power Dissipation

- Conduction loss: P_cond = I² × R_DS(on) = 20² × 0.003 = **1.2 W**
- Switching loss: add your estimate. A typical budget for 100–300 kHz is 0.3–0.8 W.
- **Design target: ~2.0 W per MOSFET.** This leaves margin.
- Thermal goal: T_j ≤ 110 °C at T_a = 50 °C. That means total R_θJA ≤ 30 K/W, and the via field should contribute **≤ 8 K/W**.

## 3. Single-Via Thermal Resistance

R = L / (k × A), with k_Cu = 385 W/m·K and L = 1.6 mm.

| Via type | Conducting area | R per via |
|---|---|---|
| Unfilled (25 µm barrel) | 0.0216 mm² | **~192 K/W** |
| Cu-filled / VIPPO | 0.0707 mm² | **~59 K/W** |
| Epoxy-filled, non-conductive | ≈ barrel only | ~185 K/W |

## 4. In-Pad Array Layout

With a 0.6 mm pad, the minimum practical pitch is 1.0 mm (0.4 mm copper web). A 4×4 array at 1.0 mm pitch spans 3.6 mm, which does not fit a 3.3 mm EP. The array is therefore a **3×3 grid at 1.2 mm pitch**:

```
        ◄──────── 3.3 mm EP ────────►
       ┌─────────────────────────────┐
       │                             │
       │    ●        ●        ●      │  ▲
       │                             │  │ 1.2 mm
       │    ●        ●        ●      │  ▼
       │                             │
       │    ●        ●        ●      │
       │                             │
       └─────────────────────────────┘
   Array span: 2.4 mm c-c + 0.6 pad = 3.0 mm
   Edge clearance to EP boundary: 0.15 mm
   ● = 0.3 mm drill / 0.6 mm pad
```

**Array resistance (9 vias in parallel):**
- Unfilled: 192 / 9 = **21 K/W**. At 2 W this is a 43 °C rise across the vias alone, which **fails** the target.
- VIPPO (Cu-filled, capped, planarized): 59 / 9 = **6.6 K/W**. At 2 W this is a 13 °C rise, which **passes**.

**Recommendation: VIPPO, filled and plated over, IPC-4761 Type VII.** It also eliminates solder wicking under the EP.

## 5. Extended Via Field (Outside the Package)

Add vias in the drain copper pour, adjacent to the package. These don't need VIPPO.

- Place 2 rows along each non-signal side of the drain pour, at 1.0 mm pitch, starting ≤ 0.5 mm from the package edge.
- That is about 16–20 additional unfilled vias, adding ~10–12 K/W in parallel. Effectiveness is about 50–70% because of lateral spreading resistance.
- Expected combined via path: **~4.5–5.5 K/W**.

```
   ○ ○ ○ ○ ○ ○ ○        ○ = unfilled stitching via in drain pour
   ○ ┌─────────┐ ○
   ○ │ ● ● ●   │ ○      ● = VIPPO in-pad
   ○ │ ● ● ●   │ ○
   ○ │ ● ● ●   │ ○
   ○ └─────────┘ ○
     (gate/source pins side — keep clear)
```

## 6. Current-Carrying Check

If the EP is the drain and the vias also carry load current between layers:

- Per IPC-2152, a 0.3 mm via carries ~1.5–2 A at ΔT = 10 °C.
- 20 A therefore needs **≥ 12–14 vias**. The 9 in-pad plus 16+ external vias gives 25+ vias, which is adequate with margin.
- Preferably route the main 20 A path on the top layer: a 2 oz pour at ≥ 6 mm width. The vias then mainly carry heat.

## 7. Plane Connections

- Use **solid connections (no thermal reliefs)** on all layers for these vias.
- Under the device, inner layers should be the same-net copper pour, or a GND plane if the EP is source/GND. Each plane should be ≥ 25 × 25 mm for spreading.
- Bottom layer: maximize copper and provide an exposed area for a heatsink or thermal pad if needed.

## 8. Assembly / Stencil

- EP paste: windowpane pattern, **50–60% coverage**, as 4 apertures of about 1.4 × 1.4 mm.
- With VIPPO, apertures can overlap vias. If the vias are unfilled, keep apertures off the vias and tent the bottom side.
- Target voiding < 25% (IPC-7093).

## 9. Cycle 2 Summary

| Item | Spec |
|---|---|
| In-pad array | 3×3, 1.2 mm pitch, VIPPO |
| External field | 16–20 vias, 1.0 mm pitch, unfilled, solid-connected |
| Via path R_θ | ~5 K/W |
| Est. ΔT across vias at 2 W | ~10 °C |
| Est. T_j at 50 °C ambient | ~95–105 °C (verify) |

**Items for Cycle 3 verification:**
1. Confirm the EP dimensions and R_DS(on) from the datasheet.
2. Confirm VIPPO cost and availability with your fab. If it's rejected, increase to 0.25 mm drill / 4×4 or enlarge the external field to 30+ vias.
3. Run a thermal simulation (e.g., Ansys Icepak or Celsius) with the actual stackup and neighboring MOSFET heat sources.
4. Validate with a thermocouple or IR on a prototype at 20 A.