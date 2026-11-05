# 6-Layer Thermal-Optimized Stackup: 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

**Assumptions:** Since I don't have the earlier design cycles, I've assumed the following. Correct any that differ and I'll re-derive the numbers.

- 25.5 × 25.5 mm M2 mounting pattern on a ~29 × 29 mm outline.
- 2–6S input.
- 4 ESC channels × 3 half-bridges × 2 = 24 N-channel FETs in PDFN 3.3×3.3, Rds(on) of about 2–4 mΩ.
- STM32 MCU, SPI gyro, and BEC on the same board.

---

## 1. Stackup (1.2 mm finished, IPC-4101/126 high-Tg FR-4)

| Layer | Function | Cu (finished) | Dielectric below | Notes |
|---|---|---|---|---|
| **L1 Top** | FETs, phase/VBAT pours, FC signals | 2 oz (1 oz base + plate) | **PP 0.075 mm** (1080) | FET drain pads with VIPPO |
| **L2** | **Solid GND** (thermal spreader #1) | 2 oz | Core 0.20 mm | No splits under ESC zone |
| **L3** | **VBAT plane** (ESC zone) / signals (FC zone) | 2 oz | PP 0.20 mm (2×2116) | Region-split, see §3 |
| **L4** | **VBAT plane** (ESC zone, parallel to L3) / signals (FC zone) | 2 oz | Core 0.20 mm | Stitched to L3 with ≥40 vias |
| **L5** | **Solid GND** (thermal spreader #2) | 2 oz | **PP 0.075 mm** (1080) | Mirror of L2 |
| **L6 Bottom** | FETs or MCU/gyro, motor pads, battery pads | 2 oz | — | Exposed copper heat-sink zone |

**Thickness check:** 6 × 0.07 mm Cu + 0.75 mm dielectric + mask ≈ **1.20 mm**. Use a symmetric build to avoid bow and twist. Specify ≤0.75 % warp, because the board is thin and copper-heavy.

**Why this order:**
- **Thin outer prepreg (75 µm) between L1/L2 and L5/L6.** This is the dominant lateral heat path. FET heat crosses one thin dielectric into a full GND plane. Thermal resistance through 75 µm of FR-4 (k ≈ 0.3 W/m·K) is about 0.25 K·cm²/W, roughly 3× better than a standard 0.2 mm prepreg.
- **Four 2 oz planes act as heat spreaders.** Even when nominally "electrical," they spread heat laterally across the whole 8.4 cm² area.
- **VBAT is buried between two GNDs.** This gives low loop inductance for switching, a small amount of plane capacitance, and keeps the 25 V net off the outer surfaces.

**Material upgrade (recommended if budget allows):** Ventec VT-4B5 or similar thermally conductive prepreg (k ≈ 1.6–2.2 W/m·K) for the **two outer 75 µm prepregs only**. That gives about 5–7× better vertical conduction at about +15–25 % cost. Keep standard high-Tg FR-4 (Tg ≥ 170 °C, Td ≥ 340 °C) for the cores.

---

## 2. Thermal Via Strategy (the critical part)

**Via spec under each FET drain/tab pad:**
- 0.25 mm drill, 0.45 mm pad.
- **Copper-filled, planarized and capped (VIPPO / IPC-4761 Type VII).** Epoxy fill is inferior.
- 0.65 mm pitch, about **9–12 vias per 3.3×3.3 drain pad**.

**Per-via thermal resistance through 1.2 mm:**

| Via type | Cu cross-section | R_th per via | 10 vias in parallel |
|---|---|---|---|
| Plated only (25 µm wall) | 0.0177 mm² | ~174 K/W | ~17 K/W |
| **Cu-filled** | 0.049 mm² | **~63 K/W** | **~6.3 K/W** |

Copper fill alone is worth about a 2.7× improvement. It is mandatory at this density.

**Other via rules:**
- **Source pads** carry via arrays into L2/L5 GND (low-side) or into the phase pour (high-side).
- **Thermal relief:** use solid connections on all power vias. Do not use spoke reliefs on any FET, motor, or battery pad. Compensate for reflow with a longer soak profile.
- **Stitching field:** add a GND-to-GND via grid at 1.5 mm pitch across all free ESC-zone area. This ties L2 to L5 thermally into a single spreader.

---

## 3. Floorplan and Layer-Region Split

```
        ┌───────────────────────────────┐
        │ M1/M2 FETs ▓▓▓▓▓▓  ▓▓▓▓▓▓      │ ← board edge = prop-wash airflow
        │ ▓          ┌─────────┐       ▓ │
        │ ▓  drivers │ FC ZONE │ drivers▓ │   FETs on perimeter ring