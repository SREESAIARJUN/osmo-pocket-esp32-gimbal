# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC with 4-in-1 ESC (24 MOSFETs)

**Assumptions:** 4 ESC channels × 6 single N-FETs in DFN/PDFN 3×3 packages, 2–6S input, and roughly 20–35 A continuous / 40–55 A burst per channel. "Cycle 25" is treated as your revision label. Send me any different package or current targets and I'll adjust the numbers.

---

## 1. Placement Strategy (drives the stackup)

| Side | Contents |
|---|---|
| **Top (L1)** | FC MCU (F405/H743), gyro, baro, OSD, BEC, connectors. All fine-pitch parts. |
| **Bottom (L6)** | All 24 MOSFETs, ESC MCU(s), gate drivers, current shunt, bulk-cap pads. |

Why put every FET on L6 and none on L1:
- The gyro stays thermally and electrically separated from the hot switching side.
- L6 can use heavier copper because no fine-pitch QFN lives there.
- The FETs face outward into prop wash, the only real airflow on a whoop or toothpick.

---

## 2. Stackup (1.60 mm finished, symmetric heavy-copper core)

```
 Layer  Function                    Finished Cu    Dielectric
 ─────────────────────────────────────────────────────────────────
 L1     Signal + FC components      1 oz +plating (~45 µm)
                                                   PP  0.100 mm (1×2116, RC ~55%)
 L2     GND (solid, unbroken)       2 oz (70 µm)
                                                   Core 0.200 mm
 L3     VBAT+ plane                 3 oz (105 µm)
                                                   PP  0.530 mm (center, 2116×2 + 7628×2)
 L4     GND/PGND plane              3 oz (105 µm)
                                                   Core 0.200 mm
 L5     Phase pours + VBAT stitch   2 oz (70 µm)
                                                   PP  0.100 mm (2×1080, high resin for 2 oz fill)
 L6     FETs / ESC power side       2 oz finished (base 1.5 oz + plating)
 ─────────────────────────────────────────────────────────────────
 Total Cu ≈ 0.47 mm   Dielectric ≈ 1.13 mm   Total ≈ 1.60 mm ±10%
```

### Design rationale
- **The L3/L4 3 oz pair is the main thermal spreader and DC bus.** VBAT and GND sit as adjacent planes, so they also form a low-inductance bus with distributed capacitance. This reduces ringing on the FETs, which lowers switching loss and heat.
- **L5 to L6 uses a thin 0.10 mm prepreg.** Heat leaves the FET drain pads through a very short vertical path into the L5 phase pours, which act as a second spreading layer under each half-bridge.
- **L1 to L2 also uses a thin 0.10 mm prepreg.** This gives tight reference coupling for gyro SPI and USB. With 0.10 mm prepreg to L2:
  - roughly 0.18 mm wide traces give about 50 Ω single-ended;
  - roughly 0.13 mm traces at 0.13 mm gap give about 90 Ω differential for USB.
  - Have the fab confirm both with their field solver.
- **The thick center prepreg** sits between two planes of the same weight. It keeps the stack symmetric and limits warp. It is also the best place to absorb thickness, because heat crosses it in parallel with many vias.
- **Copper symmetry:**
  - 2 oz (L2) mirrors 2 oz (L5).
  - 3 oz (L3) mirrors 3 oz (L4).
  - The outer layers differ slightly (L1 ≈ 1.3 oz, L6 ≈ 2 oz finished). Correct this with copper thieving or hatched GND fill on L1 open areas.

---

## 3. Material Specification

| Parameter | Spec | Reason |
|---|---|---|
| Laminate | High-Tg FR-4, Tg ≥ 170 °C, Td ≥ 340 °C (e.g., 370HR-class / S1000-2) | Handles FET junctions near 120 °C and repeated reflow |
| Thermal upgrade (optional) | Thermally conductive prepreg, about 1.0–1.6 W/m·K, on the L5–L6 and L1–L2 layers only | About 3–5× better vertical conduction than standard FR-4 (0.3 W/m·K) |
| CTE (z) | ≤ 3.0% (50–260 °C) | Protects via barrels under thermal cycling |
| Surface finish | ENIG | Flat pads for DFN; good for motor solder tabs |
| Solder mask | Thin LPI, green or black | Black looks good but runs slightly hotter in sun; it radiates better too, roughly a wash |

---

## 4. Thermal Via Design (where most of the gain is)

**Via spec under every FET drain/source tab:**
- 0.25 mm drill, 0.45–0.50 mm pad, 0.55 mm pitch.
- Via-in-pad, copper/epoxy filled and capped (IPC-4761 Type VII, VIPPO). This prevents solder wicking and voids under the DFN.
- Prefer copper-filled vias over epoxy-filled if your fab offers it.

**Per-via thermal resistance over a 1.6 mm length:**

| Via type | Conducting area | R_th per via |
|---|---|---|
| 25 µm plated barrel, epoxy fill | ~0.022 mm² | ~190 K/W |
| Solid copper fill | ~0.071 mm² | ~60 K/W |

**Per 3×3 DFN with 9 vias