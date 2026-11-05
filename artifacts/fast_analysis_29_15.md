# 6-Layer Thermal-Optimized Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC

I don't have context on what "Cycle 29" refers to, so I've treated it as an iteration label. Send me the previous cycle's stackup or thermal data and I'll turn this into a delta review.

---

## 1. Design Assumptions

| Parameter | Value |
|---|---|
| Board | 25.5 × 25.5 mm (≈650 mm² per side) |
| MOSFETs | 24 = 4 ESC channels × 3 half-bridges × (HS + LS), single N-ch DFN 3.3×3.3 or 2×2 |
| Input | 1–4S, ~12–20 A/motor burst |
| Estimated FET dissipation | 0.15–0.3 W each → **4–7 W total** worst case |
| Target thickness | 1.2 mm (whoop/toothpick weight class) |

The key observation is that each FET's drain tab is its primary heat path. Drains sit on different nets:

- **High-side drain = VBAT**, which can tie directly into a big plane.
- **Low-side drain = phase node** (switching, ~3 A/ns edges), which needs local islands.

The stackup is built around that split.

---

## 2. Layer Stack (1.20 mm nominal)

| # | Layer | Cu | Function | Thickness |
|---|---|---|---|---|
| L1 | **Top** | 1 oz + plating (~45 µm) | All 24 FETs, phase pours, motor pads, VBAT pads | 0.045 |
| — | Prepreg 1080/2116 (high-Tg, high-resin) | — | **Thin for L1→L2 thermal coupling** | 0.085 |
| L2 | **Power Islands** | **2 oz** | VBAT + 12 phase islands mirroring L1 under FETs; GND elsewhere | 0.070 |
| — | Core | — | | 0.200 |
| L3 | **GND (main)** | **2 oz** | Solid return plane + primary heat spreader | 0.070 |
| — | Prepreg (2× 2116) | — | Thick enough to fill 2 oz copper | 0.250 |
| L4 | **VBAT Plane / Signals** | **2 oz** | Solid VBAT under FET zone; inner-signal routing in logic zone | 0.070 |
| — | Core | — | | 0.200 |
| L5 | **GND (shield)** | 1 oz | Solid reference for L6 and gyro shield | 0.035 |
| — | Prepreg 1080 | — | | 0.085 |
| L6 | **Bottom** | 1 oz + plating | MCU, gyro, ESC MCU(s), gate drivers, regulators, connectors | 0.045 |
| | | | **Total ≈ 1.16–1.22 mm** | |

**Material:** High-Tg FR-4 (Tg ≥ 170 °C, e.g. Shengyi S1000-2M or ITEQ IT-180A). Consider a thermally enhanced prepreg (~1.0 W/m·K vs. 0.3 for standard) for the L1–L2 dielectric only. That layer is the biggest thermal bottleneck between the FETs and the planes.

**Copper total ≈ 10 oz equivalent.** This is the main heat-spreading mass. The stack is symmetric in copper weight (1/2/2/2/1 with plated outers) to avoid bow and twist.

---

## 3. Board Zoning

```
 ┌───────────────────────────────┐
 │ M1 pads   [FET ZONE A]  M2 pads│  ← FETs on L1, perimeter ring
 │  ┌─────────────────────────┐  │
 │  │                         │  │
 │FET│   LOGIC ZONE (L6):     │FET│
 │ D │   MCU, gyro (center,   │ B │
 │  │    farthest from FETs)  │  │
 │  │                         │  │
 │  └─────────────────────────┘  │
 │ M4 pads   [FET ZONE C]  M3 pads│
 └───────────────────────────────┘
```

- **FET zone:** a perimeter ring (~5 mm wide) carrying all 24 FETs on L1, grouped 6 per corner/edge next to each motor's pads. This puts them in the prop wash, which is the dominant cooling mechanism in flight.
- **Logic zone:** the center region, bottom side. The gyro sits at the geometric center on L6, opposite and as far as possible from FET hot spots.
- On L2/L4, the plane boundary between zones uses a **thermal choke**: a narrowed copper neck or stitched slot around the gyro footprint, so FET heat doesn't drive gyro temperature drift.

---

## 4. Thermal Via Strategy (the critical part)

**Via spec:** 0.25 mm drill / 0.45 mm pad, via-in-pad, **copper- or epoxy-filled and capped (VIPPO / IPC-4761 Type VII)**. Filling prevents solder wicking under DFN pads.

| Via type | R_th per via (1.2 mm long) |
|---|---|
| Plated 25 µm, unfilled | ~160 K/W |
| Conductive/Cu-filled | ~60 K/W |

**Per FET:** 6–9 filled vias under the drain pad gives roughly **7–10 K/W** to inner planes.

- **High-side FETs (12):** drain vias go straight into the **L2 VBAT + L4 VBAT planes**. These are the largest copper areas and the best heat sinks.
- **Low-side FETs (12):** drain vias go into the phase islands on L1 and L2 only. Keep the islands as large as switching-node EMI allows, since they are also current paths to the motor pads.
- **Low-side source pads:**