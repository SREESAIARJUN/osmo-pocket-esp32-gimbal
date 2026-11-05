# Thermal Via Array Design: QFN-20 Power MOSFETs at 20 A Continuous

## 0. Assumptions

I don't have earlier cycles' context, so these values are assumed. Swap in your datasheet numbers where they differ; the method stays the same.

| Parameter | Assumed value |
|---|---|
| Package | QFN-20, 4×4 mm, exposed drain pad 2.7 × 2.7 mm |
| R_DS(on) | 2.0 mΩ @ 25 °C, ×1.5 at T_j = 110 °C → **3.0 mΩ** |
| Conduction loss | 20² × 3.0 mΩ = **1.2 W** |
| Switching and other losses | ~0.3 W |
| **Design dissipation** | **P ≈ 1.5 W per FET** |
| PCB | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner, 25 µm via plating |
| Target | T_j ≤ 110 °C at T_amb = 50 °C (≤ 60 °C rise) |

**Note on "0.6 mm annular ring":** a true 0.6 mm ring would give a 1.5 mm via pad, which is wider than the usable pitch under a 2.7 mm pad. I've interpreted this as a **0.6 mm finished pad diameter** (0.15 mm ring), which is standard for a 0.3 mm drill. If you really need 0.6 mm ring width, the array shrinks to a 2×2 and thermal performance drops by about 2×.

---

## 1. Thermal Resistance per Via

The barrel cross-section, for plating t = 25 µm on a 0.3 mm drill:

A = π/4 × (0.30² − 0.25²) = 0.0216 mm²

| Via type | Calculation | R_th per via |
|---|---|---|
| Unfilled, plated only | 1.6 mm / (385 W/m·K × 0.0216 mm²) | **≈ 190 K/W** |
| Solder-wicked (uncontrolled) | variable | 100–150 K/W (not reliable) |
| Conductive epoxy filled (k ≈ 8) | small gain | ≈ 170 K/W |
| **Copper-filled, capped (VIPPO)** | full 0.0707 mm² Cu | **≈ 60 K/W** |

---

## 2. Array Layout Under the Exposed Pad

### Pitch selection
- Via pad 0.6 mm plus 0.4 mm web gives **1.0 mm pitch**. This preserves copper continuity and is within normal fab capability.
- Keep a 0.35 mm edge margin from the via-pad edge to the exposed-pad edge so solder fillet and paste registration aren't disturbed.

### Grid: 3 × 3 = 9 vias

```
 Exposed pad 2.7 × 2.7 mm (origin = pad center)
 ┌─────────────────────────────┐
 │                             │
 │   ●(-1,+1)  ●(0,+1)  ●(+1,+1)│
 │                             │
 │   ●(-1, 0)  ●(0, 0)  ●(+1, 0)│   ● = 0.3 mm drill / 0.6 mm pad
 │                             │   pitch = 1.00 mm
 │   ●(-1,-1)  ●(0,-1)  ●(+1,-1)│   edge margin ≈ 0.35 mm
 │                             │
 └─────────────────────────────┘
 Coordinates in mm: x, y ∈ {−1.0, 0, +1.0}
```

### Array thermal resistance (vias only)

| Via type | R_th of 9 vias in parallel | ΔT at 1.5 W |
|---|---|---|
| Unfilled | 190 / 9 ≈ **21 K/W** | ~32 °C |
| VIPPO | 60 / 9 ≈ **6.7 K/W** | ~10 °C |

With R_θJC ≈ 1.5 K/W and bottom-side spreading plus convection on a ~25 cm² 2 oz pour (≈ 20–25 K/W):

- **Unfilled:** T_j ≈ 50 + 1.5 × (1.5 + 21 ∥ top-spreading + 22) ≈ **~95–100 °C**. This passes, with marginal headroom.
- **VIPPO:** T_j ≈ **~85 °C**. This is the recommended option.

---

## 3. Current Capacity: the Real Constraint

The exposed pad is the **drain**. If the 20 A drain current has to transfer between layers, 9 vias aren't enough.

- A 0.3 mm via with 25 µm plating carries about 1.0–1.5 A each for a 10 °C rise (IPC-2152 estimate).
- 20 A ÷ 1.0 A gives **≥ 20 vias** for current transfer, and 25 is better for margin.

**Solution:** add a **perimeter "stitching" array** in the drain copper pour just outside the package outline:

```
        ○ ○ ○ ○ ○ ○        ○ = additional 0.3/0.6 vias in drain pour
      ┌───────────────┐       1.0 mm pitch, ≥0.5 mm from package
   ○  │  ●   ●   ●    │  ○    body, on sides with NO signal pins
   ○  │  ●   ●   ●    │  ○    (typically the drain-pin side + ends)
   ○  │  ●   ●   ●    │  ○
      └───────────────┘
        (gate/source pin side kept clear for routing)
```

- Add 12–16 perimeter vias, for **~21–25 total** on the drain net.
- This also cuts thermal resistance by about another 30–40%, since the vias are in parallel.
- If the drain current stays on the top layer (wide 2 oz pour to the bus), the 9 under-pad vias are purely thermal and current capacity doesn't apply to them.

---

## 4. Assembly and Solder-Voiding Controls

Open vias under a QFN pad wick solder away from the joint, which causes voids and tilt. Choose one of these options, in order of preference:

1. **VIPPO (IPC-4761 Type VII)**: filled and capped. This gives the best thermal and assembly result and adds roughly 10–20% to PCB cost.
2. **Plugged plus bottom-side tented (Type VI)**: an acceptable compromise.
3. **Bottom tenting only**: the cheapest option, but expect 20–35% voiding.

**Stencil:**
- Windowpane the paste into 4 panes (2×2), totaling **50–60% of pad area**.
- Place the panes *between* vias if they aren't filled.
- Use a 0.10–0.127 mm stencil.
- Keep voiding below 25% per IPC-7093.

---

## 5. Stack-Up and Copper Recommendations

| Layer | Function |
|---|---|
| L1 (2 oz) | Drain pour ≥ 4 cm² per FET, unbroken around the pad |
| L2 (1 oz) | Solid GND or drain plane. Connect vias **without thermal reliefs** |
| L3 (1 oz) | Drain/power plane, also solid-connected |
| L4 (2 oz) | Bottom heat-spreading area ≥ 6 cm², exposed (no mask) if you're using a heatsink or TIM pad |

**FET spacing:** keep ≥ 8 mm center-to-center between adjacent MOSFETs (half-bridge pairs) to avoid overlapping thermal footprints. Alternatively, alternate them on top and bottom.

---

## 6. Summary Spec

| Item | Value |
|---|---|
| Under-pad vias | 9 