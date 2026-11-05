# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have context for "Cycle 21," so I treated this as a standalone design. Share that cycle's constraints (FET part, current target, fab house) and I'll adjust the numbers.

## 1. Design Assumptions

| Parameter | Assumed value |
|---|---|
| Battery | 2–4S (up to 17 V) |
| ESC current | 4 × 15 A continuous, 25 A burst |
| MOSFETs | 24 × N-ch, 3×3 mm DFN (PDFN3333), R<sub>DS(on)</sub> ≈ 4–6 mΩ, 6 per motor (3 half-bridges) |
| Board thickness | ~1.0 mm |
| Fab | HDI-capable (filled/capped via-in-pad) |

**Estimated loss per FET (15 A):**
- Conduction: I²·R·duty ≈ 15² × 5 mΩ × 0.5 ≈ 0.56 W
- Switching: ≈ 0.15 W
- Total ≈ 0.7 W per FET, about **4–6 W for the whole board** (only 2 of 6 FETs per motor conduct at once). This is the heat budget the stackup must spread.

## 2. Stackup

| # | Layer | Cu (finished) | Function | Dielectric below |
|---|---|---|---|---|
| L1 | **Top – Power** | 1 oz base + plating (~50 µm) | 24 FETs, phase pours, motor pads, gate drivers, bulk caps | **PP 75 µm** (1080, thermal-grade optional) |
| L2 | **PGND plane** | **2 oz (70 µm)** | Solid ground; primary heat spreader; tight commutation-loop return | **Core 100 µm** |
| L3 | **VBAT plane** | **2 oz** | Solid battery plane; second heat spreader | **PP ~250 µm** (2×2116) |
| L4 | **Mixed** | **2 oz** | Low-speed signals (PWM, telemetry, current sense, LEDs); remaining area poured GND | **Core 100 µm** |
| L5 | **DGND plane** | **2 oz** | Solid reference for L6; shields MCU/gyro from power switching | **PP 75 µm** |
| L6 | **Bottom – Logic** | 1 oz base + plating | MCU, gyro, OSD, baro, flash, RX, 5V/9V regulators | — |

**Total thickness:** ≈ 0.60 mm dielectric + 0.38 mm Cu + mask ≈ **1.0–1.05 mm**

**Material:** High-Tg FR-4 (Tg ≥ 170 °C, e.g. Shengyi S1000-2 or ITEQ IT-180A).
- Optional upgrade: a thermally conductive prepreg (~2–3 W/m·K) for the L1–L2 and L5–L6 gaps. This cuts vertical thermal resistance about 5–8× versus standard FR-4 (0.3 W/m·K).

**Symmetry:** The stack is symmetric about the center (outer layers 1 oz, inner layers 2 oz), which prevents bow/twist. Warp matters on a thin 25.5 mm board carrying heavy copper.

## 3. Why This Arrangement Maximizes Heat Dissipation

1. **Three thick planes stack under the FETs.** Three 2 oz layers (L2, L3, L4 pour) plus L5 give about 280 µm of effective copper. Spreading resistance drops from ~37 K/W per square (single 2 oz layer) to under 10 K/W per square.
2. **Thin 75 µm prepreg between L1 and L2.** The FET drain/source tabs couple to the first plane almost directly.
3. **The whole board becomes the heatsink.** Heat spreads laterally across about 650 mm² per layer instead of staying concentrated under the FET rows.
4. **Electrical benefit.** The 100 µm L2–L3 core forms a distributed PGND/VBAT capacitor. Together with the tight L1–L2 spacing, this minimizes commutation-loop inductance, so ringing and switching loss both drop. That is a thermal win too.

## 4. Thermal Via Strategy (Critical)

**Via spec:** 0.25–0.30 mm drill, **copper-filled and capped (VIPPO)**, placed directly in the FET exposed pads.

| Via type | Approx. R<sub>th</sub> per via (1 mm length) |
|---|---|
| Plated only (25 µm wall) | ~120 K/W |
| **Copper-filled** | **~35 K/W** |

**Per-FET array:** 6–9 filled vias in the drain pad, giving about **4–6 K/W per FET** into the plane stack.

**Connection rules:**
- Drain-tab vias tie to the appropriate net plane: VBAT for high-side FETs, phase-node pour for low-side.
- Add **thermal-only via stitching** around each FET row into the L2/L5 GND planes.
- Use **no thermal reliefs** on power-plane connections. Make them solid connections, and accept the harder hand rework.

## 5. Placement and Zoning

**Top side (L1):**
- Arrange the FETs in four L-shaped clusters, one per corner/motor, along the board edges.
- Corner placement keeps them n