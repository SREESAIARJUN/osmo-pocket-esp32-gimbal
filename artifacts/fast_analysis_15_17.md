# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC with 24-MOSFET 4-in-1 ESC

I don't have the details from earlier cycles, so this design assumes:
- 4 motors × 3 half-bridges × 2 FETs, in PDFN 3×3 or 2×2 packages
- 2–4S whoop or toothpick class, about 15–25 A burst per motor
- 20 × 20 mm M2 mounting holes
- A target board thickness of about 1.2 mm

Correct any of these and I'll re-tune the stackup.

---

## 1. Stackup Definition (target 1.20 mm ±10%)

| Layer | Function | Cu (finished) | Dielectric below | Notes |
|---|---|---|---|---|
| **L1 Top** | 12 FETs (M1/M2), FC MCU, gyro, BEC | **2 oz (70 µm)** | PP 0.075 mm, thermal grade | Phase and VBAT pours around FETs |
| **L2** | **GND plane (solid)** | **2 oz** | Core 0.20 mm | Primary heat spreader and reference for L1/L3 |
| **L3** | Signals: SPI, DShot, gate drive, UARTs, plus GND fill | 1 oz (35 µm) | PP 0.25 mm (center) | Fill copper above 70% for balance |
| **L4** | **VBAT plane** plus phase-node thermal islands | **2 oz** | Core 0.20 mm | High-side drain tabs via directly into this plane |
| **L5** | **GND plane (solid)** | **2 oz** | PP 0.075 mm, thermal grade | Mirror of L2 |
| **L6 Bottom** | 12 FETs (M3/M4), ESC MCU(s), drivers | **2 oz** | — | Symmetric with L1 |

- **Total:** 385 µm Cu + 0.80 mm dielectric + mask ≈ **1.22 mm**
- **Copper total:** 11 oz, which is heavy for this board size. Confirm the fab can do 2 oz inner layers on a 6L build.

### Material
- **Base laminate:** High-Tg FR-4 (Tg ≥ 170 °C), such as IT-180A or S1000-2M. FET junctions near 120 °C push board temperature into the zone where standard FR-4 softens.
- **Outer prepregs (L1–L2, L5–L6):** Thermally conductive prepreg, about 1.5–3 W/m·K (e.g., Ventec VT-4B series, or ceramic-filled). Use it only on these outer pairs to control cost.
- **Finish:** ENIG, for flat pads on fine-pitch QFN and gyro parts and good FET solder joints.

---

## 2. Why This Arrangement

**1. Thin outer dielectrics.** Heat leaves the FET tab through L1 and must cross into L2. Through a 3 × 3 mm pad footprint:

| Dielectric (0.1 mm) | Thermal resistance |
|---|---|
| Standard FR-4 (0.3 W/m·K) | ~37 K/W |
| Thermal prepreg (2.2 W/m·K) | ~5 K/W |

Using 0.075 mm improves this further. This is the single biggest stackup lever.

**2. Two solid GND planes plus one VBAT plane in 2 oz.** Lateral spreading scales with copper thickness. Three heavy planes turn the whole 650 mm² board into the heatsink rather than the local FET area.

**3. Symmetric construction.** Both outer pairs are 2 oz with thin thermal prepreg, so the build stays mirrored. This avoids bow and twist, which matters on a 1.2 mm board with heavy copper. L3, the only 1 oz layer, is offset by dense GND fill.

**4. Signals buried on L3, sandwiched next to L2 GND.** This keeps gyro SPI and DShot away from the switching nodes on L1/L6.

---

## 3. Thermal Via Strategy

Thermal resistance of a single via through 1.2 mm: