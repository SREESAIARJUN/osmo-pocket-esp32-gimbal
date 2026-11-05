# 6-Layer Thermal Stackup: 25.5×25.5 mm AIO (4-in-1 ESC + FC)

I don't have context from earlier cycles, so this design assumes:
- **24 MOSFETs** = 4 motors × 3 phases × high/low side, in PDFN 3×3 or 2×2 packages
- MOSFETs **split 12 top / 12 bottom**, since 24 FETs plus the MCU won't fit on one side of a 25.5 mm board
- MCU and gyro use 0.4–0.5 mm pitch, which limits outer-layer copper weight
- 1S–6S input, roughly **0.2–0.4 W dissipation per FET** in hard use, or 5–10 W total

---

## 1. Layer Stack (≈1.0 mm finished, symmetric)

| # | Layer | Function | Cu (finished) | Dielectric below | Notes |
|---|---|---|---|---|---|
| **L1** | Top | MOSFETs (12), gate drivers, MCU, gyro | 1 oz base + plating ≈ **45 µm** | **PP 75 µm, thermally enhanced** | Thin prepreg is the key to heat transfer |
| **L2** | Inner 1 | **Solid GND plane**, low-side source spreader | **2 oz (70 µm)** | Core 150 µm | No splits under FETs |
| **L3** | Inner 2 | **VBAT plane**, high-side drain spreader | **2 oz (70 µm)** | PP 150 µm (2×1080) | Bulk-cap return path |
| **L4** | Inner 3 | Signals (SPI, UART, DShot, current sense) plus GND pour fill | **2 oz (70 µm)** | Core 150 µm | Min 0.15/0.15 mm; balances L3 copper |
| **L5** | Inner 4 | **Solid GND plane**, spreader for bottom FETs | **2 oz (70 µm)** | **PP 75 µm, thermally enhanced** | Mirror of L2 |
| **L6** | Bottom | MOSFETs (12), phase pads, BEC, bulk caps | ≈ **45 µm** | — | Mirror of L1 |

**Total:** ≈ 0.97 mm of copper and dielectric, plus soldermask and ENIG, giving about **1.0 mm ±10%**.

---

## 2. Thermal Rationale

**Heat path for each FET:** drain pad → L1/L6 copper → thin prepreg plus filled vias → L2/L5 GND and L3 VBAT planes → whole-board spreading → convection from prop wash.

1. **Thin outer prepreg (75 µm).** This dielectric dominates the vertical thermal resistance. For a 3×3 mm pad:
   - Standard FR-4 (0.3 W/m·K): R ≈ 75 µm / (0.3 × 9 mm²) ≈ **28 K/W**
   - Thermally enhanced prepreg (~1.0 W/m·K): **≈ 8 K/W**
   - Ask the fab which thermally enhanced FR-4 prepreg they stock. Keep high Tg (≥170 °C).
2. **2 oz inner planes (×4).** Lateral spreading scales with copper thickness. Four 70 µm planes give the equivalent of about 280 µm of copper, so the whole board acts as a heatsink.
3. **Solid planes adjacent to both FET sides.** L2 and L5 sit one thin dielectric away from each FET group. L3 carries the high-side drain current and heat directly through vias.
4. **Symmetric copper (45/70/70/70/70/45).** This prevents bow and twist, which matters for gyro mounting and BGA/QFN coplanarity.

---

## 3. Thermal Via Specification

| Parameter | Value |
|---|---|
| Type | Through via, **filled and capped (VIPPO, IPC-4761 Type VII)** |
| Fill | Copper-filled preferred; conductive epoxy as fallback |
| Drill / pad | 0.25 mm / 0.45 mm |
| Pitch | 0.5–0.6 mm grid under drain/source pads |
| Count | 6–9 per PDFN 3×3 drain pad; 4–6 per source group |
| Plating | ≥25 µm barrel |

**Estimated per-via thermal resistance** over a 1.0 mm length:
- Unfilled (25 µm barrel): ≈ 120 K/W
- Copper-filled (