# Thermal Via Array Design: QFN-20 Power MOSFETs, 20 A Continuous (Cycle 8)

I don't have the data from Cycles 1–7. This design states its assumptions explicitly, so you can substitute your actual package, R_DS(on), and stackup values.

---

## 1. Assumptions

| Parameter | Assumed value | Notes |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Exposed pad (EP) = **3.4 × 3.4 mm** (drain). Adjust the grid if your EP differs. |
| R_DS(on) | 2.0 mΩ at 25 °C, ~3.0 mΩ at T_J = 110 °C | |
| Conduction loss | I²R = 20² × 0.003 = **1.2 W** | |
| Switching and other losses | +0.3 W | Design budget: **P_D = 1.5 W per FET** |
| Board | 1.6 mm FR-4, 4 layers, 2 oz outer and 1 oz inner | |
| Via plating | 25 µm (1 mil) Cu | IPC Class 2 |
| Via geometry | 0.3 mm drill, **0.6 mm pad diameter** | I read "0.6 mm annular ring" as the pad diameter, giving a 0.15 mm ring width. A 0.6 mm ring *width* would make a 1.5 mm pad, which does not fit a dense in-pad array. |
| Thermal target | ΔT across the via field ≤ 10 °C | |

---

## 2. Per-Via Thermal and Electrical Performance

**Plated barrel cross-section:**
A = π(0.150² − 0.125²) = **0.0216 mm²**

**Thermal resistance per via (unfilled):**
R_via = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W**

| Fill option | R per via | Comment |
|---|---|---|
| Unfilled / tented | ~192 K/W | Solder wicking risk in the pad |
| Non-conductive epoxy fill + cap (IPC-4761 Type VII) | ~185 K/W | **Recommended.** Flat pad, no wicking |
| Conductive (Ag) epoxy fill | ~150–170 K/W | Marginal gain, higher cost |
| Solid Cu fill | ~59 K/W | Best performance, highest cost |

**Current capacity:** about 1 A per via for roughly 10 °C rise in a 1.6 mm barrel. This is conservative and follows IPC-2152 trends.

---

## 3. Required Via Count

**Thermal requirement:**
R_total ≤ 10 °C / 1.5 W = 6.7 K/W, so N ≥ 192 / 6.7 ≈ **29 vias**

**Electrical requirement** (if the drain current changes layers through the vias):
At 20 A and 1 A/via, N ≥ **20 vias**. Use **≥ 28** for margin and to keep each via at about 0.7 A.

**Design choice: 32 vias per FET**
- 16 in the exposed pad (via-in-pad)
- 16 in the adjacent drain copper pour

This gives:
- R_array ≈ 192 / 32 = **6.0 K/W**
- ΔT ≈ 9 °C at 1.5 W
- Current ≈ 0.63 A per via

---

## 4. Layout: Exposed-Pad Array (4 × 4)

**Pitch: 0.85 mm**

- Array span: 3 × 0.85 = 2.55 mm between centers, plus 0.6 mm pad = 3.15 mm. This fits inside the 3.4 mm EP with 0.125 mm margin per side.
- Hole-to-hole web: 0.85 − 0.3 = **0.55 mm**, which is well above the typical fab minimum of 0.25 mm.
- Copper web between via pads: 0.25 mm, which keeps lateral spreading intact.

**Via center coordinates** (mm, origin at EP center):

```
X, Y ∈ { −1.275, −0.425, +0.425, +1.275 }

   (-1.275,+1.275) (-0.425,+1.275) (+0.425,+1.275) (+1.275,+1.275)
   (-1.275,+0.425) (-0.425,+0.425) (+0.425,+0.425) (+1.275,+0.425)
   (-1.275,-0.425) (-0.425,-0.425) (+0.425,-0.425) (+1.275,-0.425)
   (-1.275,-1.275) (-0.425,-1.275) (+0.425,-1.275) (+1.275,-1.275)
```

```
        ┌──────────── QFN-20 body 5.0 mm ────────────┐
        │  ▫  ▫  ▫  ▫  ▫   (source/gate pins)         │
        │  ┌───────── EP 3.4 mm ─────────┐            │
        │  │   ◎     ◎     ◎     ◎       │            │
        │  │   ◎     ◎     ◎     ◎       │  pitch     │
        │  │   ◎     ◎     ◎     ◎       │  0.85 mm   │
        │  │   ◎     ◎     ◎     ◎       │            │
        │  └─────────────────────────────┘            │
        └─────────────────────────────────────────────┘
   ◎ = 0.3 mm drill / 0.6 mm pad, filled + capped
```

---

## 5. Layout: Drain-Pour Satellite Array (16 vias)

- Place the vias on the drain copper extension on the **side(s) without signal pins**. Drain pins are typically fused along one edge; extend the pour there.
- Use **2 rows × 8 vias at 1.0 mm pitch**, starting **≥ 0.5 mm from the package edge** to clear the solder-mask dam and the inspection fillet.
- These vias can be **tented on both sides** and unfilled, since they are outside the pad. This saves cost.
- Keep the vias in **the current path** between the FET drain and the bus/inductor, so they share current evenly.

---

## 6. Layer Stack Connections

| Layer | Connection |
|---|---|
| L1 (top, 2 oz) | EP plus drain pour. Solid connection to the vias; **no thermal relief spokes**. |
| L2 (GND, 1 oz) | Keep as a solid reference. Drain vias get clearance antipads (0.3 mm annular clearance). Alternatively, use L2 as a drain heat-spreader if the topology allows. |
| L3 (1 oz) | Drain plane copy or power pour. Connect solidly to aid spreading. |
| L4 (bottom, 2 oz) | **Drain heat-spreader pad ≥ 10 × 10 mm per FET**, solid connection. Optional TIM or heatsink contact (exposed copper or ENIG). Solder mask off if contacting a heatsink. |

---

## 7. Assembly Details

- **Via-in-pad:** Specify IPC-4761 **Type VII** (filled and capped/planarized). This prevents solder wicking, voids, and FET tilt.
- **Stencil for EP:** Use a windowpane pattern, 3 × 3 or 4 × 4 apertures, totalling **50–65% paste coverage**. Apertures should