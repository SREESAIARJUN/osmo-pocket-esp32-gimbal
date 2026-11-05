# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

## 0. Assumptions (Cycle 3 baseline)

I don't have your Cycle 1–2 notes, so these are my assumptions. Correct any that are wrong.

| Parameter | Assumed |
|---|---|
| Board | 25.5 × 25.5 mm, 4× M2 holes, ~650 mm² per side |
| Battery | 1–4S (≤ 17 V nominal, ≤ 25 V transients) |
| ESC | 4 motors × 3 half-bridges × 2 FETs = 24 single N-ch FETs, DFN 3.3×3.3 (or 2×2) |
| Current | ~12 A burst / ~6 A continuous per motor |
| Placement | MCU, gyro and OSD on **top**; FETs and gate drivers on **bottom** |
| Fab | Mainstream 6L (JLC/PCBWay/NextPCB class), through vias with resin/Cu-filled via-in-pad (VIPPO) |

---

## 1. Stackup (1.20 mm nominal, symmetric)

```
 Layer  Function                    Cu (finished)  Dielectric below
 ─────────────────────────────────────────────────────────────────────
 L1 TOP  Signals, MCU, gyro, OSD     1 oz + plate (~50 µm)
                                                    PP 1080, 0.075 mm  (Dk≈3.9)
 L2      GND (solid, unbroken)       2 oz (70 µm)
                                                    Core 0.20 mm
 L3      VBAT plane (+ 5V/3V3 island 2 oz (70 µm)
         under MCU area only)
                                                    PP 2×2116, 0.20 mm
 L4      PGND plane (solid)          2 oz (70 µm)
                                                    Core 0.20 mm
 L5      Phase pours / VBAT duplicate 2 oz (70 µm)
                                                    PP 106+1080, 0.10 mm (thin = thermal path)
 L6 BOT  FETs, drivers, phase pads   1 oz + plate (~50 µm)
 ─────────────────────────────────────────────────────────────────────
 Total ≈ 1.13 mm Cu+dielectric → ~1.18–1.20 mm with mask
 Total copper ≈ 380 µm
```

### Why this arrangement

- **Four 2 oz inner planes.** These hold most of the copper, so they do most of the lateral heat spreading. Keeping the outer layers at 1 oz keeps fine pitch possible: 0.4 mm MCU BGA/QFN on top, 0.5 mm driver QFN on bottom.
- **Symmetric copper weights (1/2/2/2/2/1).** This prevents bow and twist. Thin whoops warp easily, and warp cracks FET joints during reflow.
- **Thin L5–L6 prepreg (0.10 mm)** gives the lowest thermal resistance from the FET drains to the first plane. I used 106+1080 rather than a single 1080 so the resin fills the 2 oz L5 copper. Confirm the resin content with your fab.
- **L2 GND sits directly under the top signals (0.075 mm).** This gives tight return paths and shields the gyro/MCU from the power layers below.
- **L3 VBAT and L4 PGND are adjacent across 0.20 mm.** This forms a distributed decoupling capacitor and a low-inductance commutation loop for the half-bridges.

### Material

- **Default: