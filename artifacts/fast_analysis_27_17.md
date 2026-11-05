# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have context on your earlier design cycles, so this is a self-contained proposal. Adjust it to your actual FET package, current rating and fab house limits.

## 1. Design Assumptions

| Parameter | Assumed value |
|---|---|
| Board | 25.5 × 25.5 mm, 20 × 20 mm M2 / M3-grommet mounting |
| Power stage | 4 ESCs × 3 half-bridges × 2 FETs = 24 single N-ch FETs, DFN 3.3×3.3 or 2×2 |
| Current | ~25–35 A burst per ESC, 2–6S |
| FC side | MCU (QFN/LQFP, 0.5 mm pitch), SPI gyro, OSD, BEC |
| Placement | **All FETs and gate drivers on the top side.** MCU, gyro and BEC on the bottom side. This thermally separates the gyro from the FETs. |

## 2. Layer Stackup (~1.3 mm finished, symmetric)

| # | Layer | Cu weight | Function | Thickness |
|---|---|---|---|---|
| — | Solder mask | — | Top | ~0.02 mm |
| **L1** | Top | 1 oz base + plating (~45–50 µm) | FETs, gate drivers, phase pads, motor pads, local phase pours | 0.048 |
| | Prepreg | 2116 / 2×1080 | Thin, for strong thermal coupling L1→L2 | 0.10 |
| **L2** | GND | **2 oz** | Solid GND, main heat spreader, return for low-side sources | 0.070 |
| | Core | FR-4, high-Tg (≥170 °C) | | 0.20 |
| **L3** | VBAT | **2 oz** | Solid VBAT plane, heatsink for all 12 high-side drain tabs | 0.070 |
| | Prepreg | 2×2116 | Thick, for 2 oz resin fill | 0.22 |
| **L4** | PWR mix | **2 oz** | Phase-node islands under each half-bridge, plus 5V/9V/3V3 pours and a few slow signals in the FC zone | 0.070 |
| | Core | FR-4, high-Tg | | 0.20 |
| **L5** | GND | **2 oz** | Solid GND, second heat spreader, reference plane for L6 signals, shields the FC from the power stage | 0.070 |
| | Prepreg | 2116 / 2×1080 | | 0.10 |
| **L6** | Bottom | 1 oz base + plating | MCU, gyro, BEC, USB, UART pads | 0.048 |
| — | Solder mask | — | Bottom | ~0.02 mm |

**Total:** ≈ 1.24 mm copper and dielectric, ≈ 1.28–1.30 mm with mask. Spec it to the fab as **1.3 mm ±10%**.

### Why this arrangement

- **Four 2 oz inner planes (~280 µm of copper)** form the main heat spreader. Lateral spreading in planes dominates thermal performance on a board this size.
- **Thin 0.1 mm prepreg under L1** puts L2 GND very close to the FETs. This gives:
  - low thermal resistance from the FET pads into the plane, and
  - tight high-current loop inductance, which reduces switching ringing and switching loss (and therefore heat).
- **VBAT on L3, directly between two GND planes (L2/L5)**:
  - VBAT–GND plane capacitance helps decoupling.
  - High-side drain tabs dump heat straight into it.
- **L5 solid GND** gives the gyro and MCU a quiet reference and keeps phase-node dV/dt off the FC side.
- **Symmetric copper weights and dielectrics** (1/2/2/2/2/1 oz) prevent bow and twist. This is critical for reflowing 24 thermal pads and the MCU on a thin board.
- **Outer layers stay at 1 oz base** so that 0.1 mm / 0.1 mm trace/space remains manufacturable for the MCU fan-out. 2 oz outer copper would break fine-pitch routing.

## 3. Thermal Via Strategy (the most important part)

The FET exposed pad is the **drain**, so where the vias connect depends on which FET it is:

| FET | Exposed pad net | Connect thermal vias to |
|---|---|---|
| High-side (×12) | VBAT | L3 VBAT plane. Ideal, because it is a huge heatsink. |
| Low-side (×12) | **Phase node** | Phase islands on L4. Do **not** connect to GND. |
| Low-side source pins | GND | Via fence next to the source pins into L2 and L5 GND. |

**Via specification:**
- 0.20 mm drill / 0.40