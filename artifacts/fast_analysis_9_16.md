# 6-Layer Thermal-Optimized Stackup: 25.5 × 25.5 mm AIO Flight Controller

I don't have context for "Cycle 9," so the design below is built from stated assumptions. Swap in your real numbers where they differ.

## 1. Assumptions

| Parameter | Assumption |
|---|---|
| Board | 25.5 × 25.5 mm, 20 × 20 mm M2 mounting |
| ESC | 4-in-1, 6 FETs per motor (3 half-bridges) = 24 FETs |
| FET package | 3.3 × 3.3 mm PDFN (exposed drain pad), ~3–6 mΩ |
| Battery | 2–4S, about 20–25 A burst per motor |
| Dissipation | ~0.3 W per FET sustained hover/cruise, ~0.8 W per FET during punch-outs |
| Fab class | Standard HDI-lite: VIPPO (via-in-pad, plated over), no blind/buried vias. Keeps cost sane. |

## 2. Stackup (≈1.0 mm finished)

A thinner board shortens thermal vias, which cuts their thermal resistance. Thin outer dielectrics couple surface heat into the heavy inner copper.

| # | Layer | Cu (finished) | Thickness | Function |
|---|---|---|---|---|
| — | Solder mask | — | 0.015 | Opened over power pads and phase/battery pads |
| **L1** | Top | 1 oz base + plating (~45 µm) | 0.045 | 12 FETs, gate drivers, MCU and FC logic |
| | Prepreg (thermal) | — | 0.075 | **Thermally conductive prepreg** (k ≈ 1.5–2.2 W/m·K, e.g. Ventec VT-4B/VT-5A2 class) or high-resin 1080 FR4 |
| **L2** | GND (solid) | **2 oz** (70 µm) | 0.070 | Primary heat spreader, low-side source return, reference for L1 |
| | Core | — | 0.150 | High-Tg FR4 (Tg ≥ 170 °C, e.g. S1000-2 / IT-180A) |
| **L3** | VBAT / signal hybrid | **2 oz** | 0.070 | ESC zone: VBAT pour. FC zone: low-speed signals (UART, LED, current sense) |
| | Prepreg (center) | — | 0.170 | 2 × 1080, symmetric center |
| **L4** | GND (solid) | **2 oz** | 0.070 | Second spreader, reference for L5/L6 |
| | Core | — | 0.150 | High-Tg FR4 |
| **L5** | VBAT / phase | **2 oz** | 0.070 | VBAT pour plus wide phase-node copper to motor pads |
| | Prepreg (thermal) | — | 0.075 | Same thermal prepreg as L1–L2 |
| **L6** | Bottom | 1 oz + plating | 0.045 | 12 FETs, bulk caps, motor/battery pads |
| — | Solder mask | — | 0.015 | |
| | **Total** | | **≈1.03 mm** | |

**Why this arrangement**

- **Symmetry** (L2/L4 GND, L3/L5 power, matched prepregs) prevents bow and twist. A 1 mm board with 2 oz inners will warp badly if the stack is unbalanced.
- **Each outer FET layer sits 75 µm from a 2 oz GND plane.** Low-side FETs dump heat straight into GND, which is also their electrical source. High-side drains sit on VBAT copper.
- **Total copper ≈ 370 µm across 6 layers.** Copper is your heat spreader and your thermal mass.
- **Fab note:** 2 oz copper against 75 µm prepreg risks resin starvation. Specify high-resin-content prepreg, and ask the fab to confirm press-out thickness. Accept 0.09 mm if they push back.

## 3. FET Placement Strategy

- **Split 12 top / 12 bottom, mirrored.** Each motor's 3 high-side FETs go on one side and its 3 low-side FETs directly beneath on the other. Both share one via field, and heat exits through both surfaces into prop wash.
- **Put the four ESC quadrants at the board corners** (outside the 20 × 20 hole pattern where possible). Corners get the most airflow and sit closest to the motor/phase pads.
- **Keep the gyro/IMU at center, ≥ 5 mm from any FET**, with a GND moat on L1. MEMS gyro bias drifts with temperature gradients.
- **Use MCU current as a mini heat source.** Place the MCU on top, centered, and don't stack it directly over FETs.

## 4. Thermal Via Design

| Location | Spec |
|---|---|
| Under FET exposed pad | **VIPPO**: 0.25 mm drill, 0.45 mm pad, epoxy-filled, plated over (capped). 6–9 vias per 3.3 mm pad on a 0.6 mm pitch |
| Around FET (outside pad) | 0.3 mm drill tented stitching, 0.8 mm grid, connecting L1↔L6 GND/VBAT pours |
| Power connections | **No thermal