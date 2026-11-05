# 6-Layer Stackup: 25.5 × 25.5 mm AIO (F4/F7 FC + 4-in-1 ESC, 24 MOSFETs)

I don't have your Cycle 1–3 results, so this is a self-contained proposal. The assumptions I used are listed at the end so you can adjust any that conflict with earlier decisions.

## 1. Design Philosophy

On a ~30 × 30 mm board carrying 24 FETs, **the copper is the heatsink**. Dielectric is the thermal bottleneck: FR-4 conducts about 0.3 W/m·K, while copper conducts about 390 W/m·K. The strategy therefore has five parts:

1. Use as much heavy copper as the fab allows, on as many layers as possible.
2. Keep dielectrics as thin as possible, especially directly under the FET layers.
3. Use dense filled via arrays to tie every layer together thermally under each FET.
4. Use thermally enhanced, high-Tg dielectric.
5. Split the FETs 12 top / 12 bottom so both faces see prop airflow.

## 2. Stackup (≈1.2 mm finished)

| # | Layer | Function | Cu (finished) | Dielectric below | Material |
|---|---|---|---|---|---|
| L1 | Top | 12 FETs (motors 1–2), phase/VBAT/GND pours, gate drivers | **2 oz (70 µm)** | PP 0.10 mm | Thermal prepreg (≥1.0 W/m·K) or 1080/2116 high-Tg |
| L2 | Inner 1 | **Solid GND plane** (thermal spreader + return for L1) | **2 oz** | Core 0.20 mm | High-Tg FR-4 (Tg ≥170 °C) |
| L3 | Inner 2 | **VBAT plane** (+ GND islands under MCU area) | **2 oz** | PP 0.20 mm | Thermal prepreg / high-Tg |
| L4 | Inner 3 | **GND plane #2** (+ phase pour fingers under FET zone) | **2 oz** | Core 0.20 mm | High-Tg FR-4 |
| L5 | Inner 4 | MCU / gyro / UART / DShot routing, GND fill | 1 oz (35 µm) | PP 0.10 mm | Thermal prepreg / high-Tg |
| L6 | Bottom | 12 FETs (motors 3–4), phase/VBAT/GND pours, current-sense shunt | **2 oz** | — | — |

**Thickness check**
- Copper: 5 × 70 µm + 35 µm = 0.385 mm
- Dielectric: 0.10 + 0.20 + 0.20 + 0.20 + 0.10 = 0.80 mm
- Total with mask: **≈1.2 mm**

This is within normal AIO thickness and compatible with standard 25.5 mm whoop/toothpick frames. The stack is symmetric around the L3/L4 core (2 oz / 2 oz), which avoids bow and twist during reflow of a heavy-copper board.

**Why this order**
- **GND sits immediately under each FET layer** through 0.1 mm prepreg. This gives the lowest thermal resistance into the biggest spreader and the tightest power-loop inductance for the half-bridges, which means less ringing and less switching loss.
- **L3 VBAT next to L4 GND** across a thin dielectric forms distributed plane capacitance that helps decouple the bridges.
- **L5 is the only 1 oz layer.** It carries the fine-pitch signals and is shielded from the FET layers by a GND plane on one side and the L6 GND pour on the other.

## 3. Thermal Numbers

**Lateral spreading (sheet thermal resistance)**
- One 2 oz layer: 1/(390 × 70 µm) ≈ **36.6 K/W per square**
- Five 2 oz layers in parallel (tied with vias): ≈ **7.3 K/W per square**
- This is about 5× better spreading than a typical 1 oz outer / 0.5 oz inner stack.

**Vertical via conduction (through 1.2 mm)**

| Via type (0.20 mm drill) | Single via | 12-via array under one FET |
|---|---|---|
| Plated only (25 µm wall) | ≈ 225 K/W | ≈ 19 K/W |
| Epoxy filled + capped | ≈ 200 K/W | ≈ 17 K/W |
| **Copper filled (VIPPO)** | **≈ 100 K/W** | **≈ 8 K/W** |

**Recommendation:** use copper-filled, planarized via-in-pad (VIPPO, IPC-4761 Type VII) under the FET drain tabs. Use standard plated vias in the stitching field around them.

**Loss budget sanity check**
- Assume ~3 mΩ FETs and ~20 A burst per motor.
- That gives roughly 0.5–0.9 W per conducting FET (conduction + switching), or **≈12–18 W board-level peak**.
- The stackup's job is to spread this evenly so the prop wash can carry it away. Without airflow, no stackup at this size survives sustained full throttle, so validate with prop-wash testing, not a bench test in still air.

## 4. Via & Copper Rules

| Item | Spec |
|---|---|
| Thermal via (in pad) | 0.20 mm drill / 0.45 mm pad, 0.5 mm pitch, Cu-filled + capped |
| Stitching vias | 0.25 mm drill, ≤1.0 mm grid across all GND/VBAT pours in the ESC zone |
| Plane connections in ESC zone | **Solid connections** (no thermal-relief spokes) on L2–L4 |
| Thermal reliefs | Only on hand-solder pads (battery leads, motor pads) |
| Phase-node copper | Duplicated on L1/L6 with dense stitching. Kept compact (switch node = EMI source); do not extend toward the gyro. |
| Solder mask | Opened on the large GND/VBAT pours on L1/L6 if conformal coating is not used. Bare ENIG radiates and convects slightly better and allows a heatsink pad. |
| Finish | ENIG (flat pads for VIPPO and QFN gate drivers) |
| Min trace/space (L5) | 0.1/0.1 mm. Achievable on 1 oz; do **not** route fine pitch on the 2 oz layers. |
| Outer 2 oz min space | ≥0.15 mm (fab etch-factor limit). Confirm with your fab. |

## 5. Placement Tied to the Stackup

- **ESC quadrant layout:** each motor's 6 FETs sit near its motor pad corner. Motors 1–2 go on L1 and motors 3–4 on L6, with the two sides offset diagonally so the FET hot zones don't stack directly over each other.
- **Thermal isolation of the FC core:** keep the MCU and gyro in the centre, separated from the FET ring by a via-free **thermal moat** about 1 mm wide on L1/L6. The L2/L4 GND remains continuous underneath, so the signal return is not broken. This keeps gyro drift and MCU temperature low.
- **Gyro:** mount it on the L1 side over the L5 routing region, with no switching copper within about 5 mm.
- **Bulk capacitor and battery pads:** place them on the VBAT entry edge with a direct via farm into L3.
- **Current-sense shunt:** on L6 with Kelvin traces routed on L5.

## 6. Fab Notes for the Order

> 6L, 1.2 mm ±10 %, Tg ≥170 °C (or thermal prepreg ≥1.0 W/m·K), outer 2 oz finished, L2–L4 2 oz, L5 1 oz, VIPPO (Cu-filled, capped) on marked pads, ENIG, controlled symmetric stack. Request the fab's actual stack sheet and match dielectric thicknesses before final routing.

## 7. Validation Pl