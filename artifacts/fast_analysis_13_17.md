# 6-Layer Thermal-Optimized Stackup: 25.5×25.5 mm AIO FC + 4-in-1 ESC

I don't have context from earlier design cycles, so this design rests on the assumptions below. Tell me what differs, such as Cycle 12's thermal results or the fab's capabilities, and I'll revise.

## 0. Design Assumptions

| Parameter | Assumption |
|---|---|
| Board outline | ~30.5×30.5 mm, M2/M3 holes on a 25.5 mm pattern |
| ESC topology | 4 motors × 3 half-bridges × 2 N-FETs = 24 FETs |
| FET package | 3.3×3.3 mm PDFN (e.g., ~1–2 mΩ, 30–40 V) |
| Battery | 4S–6S, 25–35 A continuous per motor, 45–55 A burst |
| Layout split | All 24 FETs on top. MCU, gyro, ESC MCU(s), gate drivers, and BEC on bottom. |
| Fab class | HDI-capable. Filled and capped vias (VIPPO), 2 oz outer and inner copper available. |

---

## 1. Stackup (≈1.50 mm finished)

```
 Layer   Function                          Cu        Dielectric to next
 ─────────────────────────────────────────────────────────────────────────
 L1 TOP  FETs, phase/VBAT/GND power pours  2 oz (70µm)
                                                      PP  0.10 mm  (thermal prepreg, 1.0–3 W/mK pref.)
 L2      SOLID GND plane (primary heat spreader)  2 oz
                                                      CORE 0.20 mm
 L3      Phase-node islands (ESC zone) /    1 oz
         signals (FC zone)
                                                      PP  0.48 mm  (center, std FR-4 Tg170)
 L4      VBAT plane (ESC zone) /            2 oz
         5V/3V3 pours (FC zone)
                                                      CORE 0.20 mm
 L5      SOLID GND plane                    2 oz
                                                      PP  0.10 mm
 L6 BOT  MCU, gyro, drivers, BEC, phase-    1 oz (+plating ≈ 1.4 oz)
         node copper mirror under FETs
 ─────────────────────────────────────────────────────────────────────────
 Total Cu ≈ 11 oz equiv.  |  Finished ≈ 1.50 mm ±10%  |  ENIG finish
```

### Why this arrangement

- **Thin 0.10 mm prepreg between L1 and L2.** Heat from FET pads crosses the dielectric into the solid 2 oz GND plane, which spreads it across the whole board. This path works even for pads that cannot be via'd to GND because they sit on phase or VBAT nets.
- **Heavy copper (2 oz) on L1, L2, L4, and L5.** These four layers carry most of the current and lateral heat spreading. Lateral spreading matters more than vertical conduction on a 30 mm board.
- **L4 as a full VBAT plane under the ESC zone.** The high-side FET drains connect directly to it with thermal vias. Because those vias stay on the same net, there is no isolation compromise.
- **L3 at 1 oz.** This keeps the core balanced against L6 and leaves fine-pitch routing for the FC's SPI, UART, and DShot lines.
- **Symmetric around the center.** The heavy layers L2/L5 and L1/L4 roughly balance. Keeping bow and twist under 0.75% matters for a stack this copper-heavy.
- **Thick center prepreg (0.48 mm).** It provides most of the board's rigidity and puts distance between the noisy L1–L2 power pair and the gyro on L6.

---

## 2. Thermal Via Strategy (per FET)

The two FET types need different treatment.

| FET | Drain tab net | Via strategy |
|---|---|---|
| **High-side** (12×) | VBAT | 9–12 vias, 0.25–0.30 mm drill, **copper-filled and capped (VIPPO)** in the pad. They run L1 → L4 (VBAT plane) → L6 VBAT island. |
| **Low-side** (12×) | Phase node | 9–12 VIPPO vias into **phase islands on L3 and L6**, each sized ~2× the pad area. Do not cut the L2/L5 GND planes beyond the via antipads. |
| Source pins (both) | Phase / GND | Low-side sources get dense stitching straight into L2 and L5 GND. This is the best thermal path on the board, so maximize it. |

Estimated via thermal resistance, using a 1.5 mm barrel and k_Cu = 385 W/m·K:
- Plated only (25 µm wall, 0.30 mm hole): ≈180 K/W per via, so ≈18 K/W for 10 vias.
- **Copper-filled 0.30 mm: ≈55 K/W per via, so ≈5.5 K/W for 10 vias.** This is the recommended option.
- Plane coupling through the 0.10 mm thermal prepreg (k = 2 W/m·K, 12 mm² pour): ≈4 K/W in parallel.

Additional via rules:
- Keep antipads on L2/L5 at minimum (0.15 mm clearance), so the GND planes stay as continuous as possible between FETs.
- Ring the ESC zone with a GND stitching fence at ~1.5 mm pitch. It ties L1, L2, L5, and L6 together thermally and for EMI.

---

## 3. Thermal Budget Check

Worst-case burst example: 4 motors at 40 A, 2 mΩ R_DS(on) at temperature, 48 kHz PWM.

| Loss | Per FET (avg) | Total |
|---|---|---|
| Conduction (~50% duty share) | ~0.8 W | ~19 W |
| Switching + dead-time/body diode | ~0.3 W | ~7 W |
| **Total burst** | **~1.1 W** | **~26 W** |

At a typical cruise of 10–15 A per motor, the total drops to roughly 3–4 W.

What the stackup achieves:
- **Junction to board copper:** about 1.5 K/W for the package, plus about 3 K/W for vias and plane coupling.
- **Board to air:** the copper planes even out temperature across the board. With prop-wash airflow of roughly 5–10 m/s over both faces, the board-to-air resistance is about 2–3 K/W.
- **Estimated result:** a ~20 s burst stays within T_j ≈ 105–115 °C at 35 °C ambient. Steady cruise sits around 55–65 °C at the board.

Run a thermal FEA (e.g., Ansys Icepak or SimScale) before release to confirm these numbers.

---

## 4. Layout Rules That Make the Stackup Work

1. **Spread the FETs across all four quadrants.** Place one motor's six FETs per quadrant along the board edge, where prop