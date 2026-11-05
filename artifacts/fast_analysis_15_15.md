# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

I don't have the results from Cycles 1–14. This is a self-contained baseline built on the assumptions below. If earlier cycles fixed the pad size, stack-up, or a ΔT target, send them and I'll adjust the numbers.

---

## 1. Design Assumptions

| Parameter | Value | Notes |
|---|---|---|
| Package | QFN-20, 5 × 5 mm body | Typical power-stage / dual-FET package |
| Exposed pad (EP) | 3.4 × 3.4 mm | **Check against your datasheet land pattern** |
| EP net | Drain (or SW node) | Carries the 20 A **and** the heat |
| Via drill | 0.30 mm | Finished hole ≈ 0.25 mm after plating |
| Via pad | 0.60 mm diameter | I read "0.6 mm annular ring" as pad diameter, giving a 0.15 mm ring. If you meant 0.6 mm ring width (1.5 mm pad), the pitch must grow to ≥1.7 mm and only 2×2 vias fit. |
| Barrel plating | 25 µm (IPC Class 2/3 avg.) | |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1–2 oz inner | |
| R_DS(on) | 2 mΩ @ 25 °C → ~3 mΩ hot | Assumed |
| Dissipation | P = 20² × 3 mΩ = **1.2 W conduction** + ~0.3–0.8 W switching ≈ **1.5–2 W** | |

---

## 2. Via Array Layout (Under the Exposed Pad)

**Grid:** 4 × 4 = 16 vias at 0.85 mm pitch, centered on the EP.

- Array copper span: 3 × 0.85 + 0.6 = **3.15 mm**, leaving 0.125 mm margin to the EP edge.
- Drill-to-drill web: 0.85 − 0.30 = 0.55 mm, which meets the typical ≥0.25 mm fab minimum.
- Pad-to-pad gap: 0.85 − 0.60 = 0.25 mm. These are the same net, so no clearance rule applies.

**Via coordinates (mm, origin = EP center):**

| | X = −1.275 | X = −0.425 | X = +0.425 | X = +1.275 |
|---|---|---|---|---|
| **Y = +1.275** | V1 | V2 | V3 | V4 |
| **Y = +0.425** | V5 | V6 | V7 | V8 |
| **Y = −0.425** | V9 | V10 | V11 | V12 |
| **Y = −1.275** | V13 | V14 | V15 | V16 |

```
        ┌──────────── EP 3.4 mm ────────────┐
        │  ○      ○      ○      ○           │
        │                                   │
        │  ○      ○      ○      ○           │   ○ = 0.3 drill / 0.6 pad
        │                                   │   pitch 0.85 mm
        │  ○      ○      ○      ○           │
        │                                   │
        │  ○      ○      ○      ○           │
        └───────────────────────────────────┘
```

**Perimeter extension (strongly recommended):** Add 2 rows of vias at 1.0 mm pitch in the drain copper pour just outside the package outline, on the side(s) without signal pins. This adds about 12–20 vias. Keep them ≥0.5 mm from the body edge for rework and paste clearance. These carry most of the high-current path into the inner planes and lower spreading resistance.

---

## 3. Thermal Calculation

**Per via, unfilled (barrel only):**
- Copper annulus area: A = π/4 × (0.30² − 0.25²) = 0.0216 mm²
- R_via = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W**
- 16 in parallel ≈ **12 K/W**. At 2 W, that is a **24 °C drop** across the board. Too high.

**Per via, copper-filled (VIPPO):**
- A = π/4 × 0.30² = 0.0707 mm²
- R_via ≈ **59 K/W**
- 16 in parallel ≈ **3.7 K/W**. At 2 W, that is a **~7.4 °C drop**. Acceptable.

**Estimated junction temperature (2 W, VIPPO, 16 + 16 perimeter vias):**

| Path element | Thermal resistance |
|---|---|
| R_θJC (bottom) | ~1.5 K/W (datasheet) |
| Solder + EP | ~0.5 K/W |
| Via array, VIPPO, with perimeter vias | ~2.5 K/W |
| Spreading + convection, ≥ 20 cm² bottom pour, still air | ~20–25 K/W |
| **Total** | **≈ 25–30 K/W** |

- **ΔT_JA ≈ 50–60 °C**, so T_J ≈ 110–120 °C at T_A = 60 °C.
- The board-to-air term dominates. The via array is no longer the bottleneck.
- If T_J must stay below 110 °C, add a bottom-side heatsink through a thermal pad, or add airflow (≥1 m/s roughly halves the convection term).

---

## 4. Current Capacity Check

- 20 A over 16 vias = 1.25 A per via. With perimeter vias it drops to about 0.6 A per via.
- A 0.3 mm / 25 µm via handles about 1.5–2 A at 10 °C rise (IPC-2152). The design has comfortable margin.
- Inner-plane copper on the drain/SW net should be ≥ 2 oz, or use paralleled planes, so the planes are not the bottleneck at 20 A.

---

## 5. Fabrication and Assembly Rules

1. **Via fill:** Specify **VIPPO**: IPC-4761 Type VII, filled and capped, planarized flush (±10 µm).
   - Without VIPPO, open vias wick solder, causing voids and a starved EP joint.
   - Fallback if VIPPO is not available: tent the vias on the **bottom side only** with a ≤ 0.1 mm mask dam. Expect 25–40 % voiding and the 12 K/W figure above.
2. **Stencil (EP):** Windowpane 2 × 2 apertures, each 1.2 × 1.2 mm with a 0.4 mm web, giving **~50 % coverage**.
   - Target < 25 % voiding per IPC-7