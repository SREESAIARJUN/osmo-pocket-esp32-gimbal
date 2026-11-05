# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have the history behind "Cycle 30," so I've stated my assumptions explicitly. Adjust them if they differ from your previous iterations.

## Design Assumptions

| Parameter | Assumption |
|---|---|
| Mounting / outline | 25.5 mm hole pattern, about 29 × 29 mm outline |
| Power | 1S–4S, about 12–15 A continuous per motor, 20 A+ burst |
| MOSFETs | 24 single N-channel in DFN 2×2 / 3×3 (12 high-side, 12 low-side) |
| MCU / IMU | STM32 (QFN/LQFP, 0.5 mm pitch) and an LGA gyro (ICM-42688 / BMI270 class) |
| Target thickness | About 1.0 mm (whoop weight class) |

## 1. Layer Stack

```
 Layer  Function                         Cu (finished)   Dielectric below
 ─────  ───────────────────────────────  ─────────────   ──────────────────────────
 L1     TOP: MCU, gyro, gate drivers,    1 oz + plating   Prepreg 1×1080/106
        BEC, USB, signal routing         (~45 µm)         90 µm, HIGH-Tk (≥1.0 W/mK)
 L2     GND – solid plane (ref + heat    2 oz (70 µm)     Core 100 µm
        spreader)
 L3     VBAT plane (+ phase pours under  2 oz (70 µm)     Prepreg 300 µm
        FET area)                                         (mechanical bulk, isolation)
 L4     VBAT / phase-node pours          2 oz (70 µm)     Core 100 µm
        (motor current paths)
 L5     GND – solid plane (heat          2 oz (70 µm)     Prepreg 1×1080/106
        spreader + return)                                90 µm, HIGH-Tk
 L6     BOTTOM: all 24 MOSFETs, phase    1 oz + plating
        & VBAT pours, exposed Cu         (~45 µm)
 ─────────────────────────────────────────────────────────────────────────────
 Copper ≈ 0.37 mm | Dielectric ≈ 0.68 mm | + mask ≈ 1.05 mm total
```

### Why this arrangement

- **Four 2 oz inner planes** give about 280 µm of internal copper. That is the main heat spreader, and it adds no weight to the outer layers or fine-pitch constraints.
- **Thin high-Tk prepreg (90 µm) on both outer layers** couples the FET pads into the inner planes. Through standard FR-4, a 4 mm² pad sees about 75 K/W to the next layer. At 1.5 W/mK the figure drops to about 15 K/W. Candidate materials are Ventec VT-4B series and Isola thermal grades; ask your fab what they stock.
- **The L2/L3 and L4/L5 GND–VBAT pairs sit 100 µm apart.** This creates two distributed decoupling capacitors with very low loop inductance, which damps switching ringing and the gyro noise it causes.
- **The thick central prepreg (300 µm)** provides bulk and stiffness while keeping both power-plane pairs tight.
- **Outer layers stay at 1 oz plus plating** so you can hold 0.1 mm / 0.1 mm trace and space for the MCU and gyro. 2 oz outer copper forces about 0.15 mm rules.
- **The stack is symmetric**, which keeps a 1 mm board from warping through reflow.

## 2. Placement Strategy

- **All 24 FETs go on the bottom (L6)**, facing prop-wash airflow. The gyro and MCU sit on top, in the board centre, with no FET directly beneath the gyro footprint.
- **Group each motor's 6 FETs in its own corner quadrant.** This spreads the four heat sources across the full board area instead of forming a central hotspot.
- **Place high-side and low-side FETs of each phase side by side**, with the phase node shared on a pour. Keep the input caps within 2 mm of the high-side drains.
- **Put gate drivers on top**, directly over their quadrant, and drop gate traces through via-in-pad.

## 3. Thermal Via Design

| Item | Specification |
|---|---|
| Thermal vias | 0.20 mm drill, 0.40 mm pad, via-in-pad, copper- or epoxy-filled and capped (VIPPO) |
| Density | 0.5 mm pitch grid under every exposed drain/source pad. Expect 6–9 vias per DFN 3×3 and 4 per DFN 2×2 |
| Plane connection | Direct connection, no thermal reliefs, on every plane carrying that net |
| Field stitching | GND-to-GND stitching vias (L2/L5) on a 1.0–1.5 mm grid across all FET quadrants |

**Per-via thermal resistance, 1.05 mm barrel:**

- Hollow, 25 µm wall: about 185 K/W
- Copper-filled: about 80 K/W

With 8 filled vias per pad, the pad-to-plane path is about 10 K/W.