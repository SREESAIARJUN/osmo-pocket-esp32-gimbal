# 6-Layer Stackup: 25.5 × 25.5 mm AIO Flight Controller, 24-MOSFET 4-in-1 ESC (Cycle 24)

## Design Assumptions

Please correct any of these that don't match your design:

- **Board outline:** 25.5 × 25.5 mm, about 650 mm² per side. This is whoop/toothpick class, 1–2S or 2–4S.
- **Power stage:** 24 single N-channel FETs (4 motors × 3 phases × high/low side) in 3×3 or 3.3×3.3 mm DFN packages with an exposed drain pad.
- **Current:** about 12–15 A continuous and 20–25 A burst per motor. Total FET dissipation is about 6–10 W.
- **Topology:** single-sided FET placement on the bottom. The MCU, gyro, OSD, and RX go on top. This keeps the gyro thermally and electrically quiet.

## Stackup (1.0 mm nominal, symmetric)

| # | Layer | Cu (finished) | Dielectric below | Function |
|---|---|---|---|---|
| L1 | Top | 1 oz base + plating ≈ 1.5 oz (50 µm) | — | MCU, gyro, BEC, logic routing, small GND/VBAT pours |
| | Prepreg | — | 2116, 0.11 mm | Thin, so L1 references L2 tightly |
| L2 | GND | 2 oz (70 µm) | — | Solid GND. Signal reference, gyro shield, heat spreader |
| | Core | — | 0.10 mm | |
| L3 | VBAT | 2 oz | — | High-side drain plane. Main heat-spreading plane |
| | Prepreg | — | 2116, 0.11 mm | VBAT–GND plane-pair capacitance |
| L4 | GND (power) | 2 oz | — | Low-side source return plane |
| | Core | — | 0.10 mm | |
| L5 | VBAT / Phase | 2 oz | — | VBAT plane plus phase-node relief under FETs |
| | Prepreg | — | 2116, 0.11 mm | |
| L6 | Bottom | 1 oz base + plating ≈ 1.5 oz | — | 24 FETs, phase pours, motor pads, battery pads |

**Thickness:** about 0.91 mm of copper and dielectric, plus solder mask, gives about 1.0 mm ±10%.

**Copper symmetry:** L1/L6, L2/L5, and L3/L4 are mirrored. This limits warpage, which matters a lot on a small board with heavy copper.

## Why This Arrangement

1. **Four 2 oz inner planes.** Total copper is about 380 µm, or roughly 38% of board thickness. Copper (k ≈ 385 W/m·K) is about 1000× more conductive than FR-4 (≈ 0.3–0.4). Lateral spreading through these planes is the dominant way to move heat from the FET cluster to the whole board area.
2. **Thin dielectrics (0.10–0.11 mm).** These minimize vertical thermal resistance between planes. The L3/L4 VBAT–GND pair also forms a distributed bypass capacitor of about 15–25 pF/cm² and a very low-inductance commutation loop. That reduces switching ringing, which reduces switching loss, which reduces heat.
3. **FETs on the bottom, gyro on top.** L2 GND acts as a thermal and EMI barrier. Gyro drift with temperature is a real problem on AIOs, so keep the IMU in the corner farthest from the hottest FETs.
4. **1.5 oz finished outer layers.** This is a compromise that still allows fine-pitch MCU fan-out (0.4–0.5 mm pitch QFN/LQFP).

## Thermal Via Strategy

**Under each FET drain pad:**
- 6–9 vias, 0.25–0.30 mm drill, 0.50–0.55 mm pad.
- Use via-in-pad, filled and capped (VIPPO, IPC-4761 Type VII). This prevents solder wicking and voiding.
- Specify copper-filled vias if the fab offers them. Epoxy-filled is the minimum.

**Per-via thermal resistance through 1.0 mm:**

| Via type | Calculation | Result |
|---|---|---|
| Plated (25 µm wall, 0.3 mm drill