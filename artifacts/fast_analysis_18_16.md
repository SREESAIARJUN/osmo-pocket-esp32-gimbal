# 6-Layer Stackup: 25.5 × 25.5 mm AIO Flight Controller with 24 MOSFETs

I don't have the history from earlier cycles, so this design rests on these assumptions. Tell me what differs and I'll revise.

| Assumption | Value |
|---|---|
| Board | 25.5 × 25.5 mm (~650 mm²) |
| Power stage | 4 ESC channels × 3 half-bridges × 2 FETs = **24 N-channel FETs** in DFN 3×3 / PowerPAK-type packages with an exposed drain pad |
| Battery | 1–4S, about 10–15 A continuous per motor, short bursts higher |
| Components | FC MCU, gyro, and BEC on top; FETs on bottom |
| Fab capability | Standard HDI-lite: 0.2–0.25 mm mechanical vias, via-in-pad filled and capped, 2 oz inner copper |

---

## 1. Stackup (≈1.15 mm finished)

| Layer | Function | Cu (finished) | Dielectric to next layer |
|---|---|---|---|
| **L1 – Top** | MCU, gyro, BEC, connectors, fine-pitch signals, GND fill | 1 oz base + plating (~45 µm) | PP 1080 high-resin, **75 µm** |
| **L2 – GND** | Solid reference for L1 signals, primary heat spreader | **2 oz (70 µm)** | Core **200 µm** |
| **L3 – VBAT** | Solid battery plane under the whole ESC region | **2 oz** | PP 2×1080, **100 µm** |
| **L4 – GND** | Solid power ground, tightly coupled to L3 | **2 oz** | Core **200 µm** |
| **L5 – Phase / Gate** | 12 motor-phase pours, gate-drive routing, current-sense traces, GND fill | **2 oz** | PP 1080 high-resin, **75 µm** |
| **L6 – Bottom** | 24 FETs, phase pads, motor solder pads, bulk caps | 1 oz base + plating (~45 µm) | — |

**Material:** high-Tg FR-4 (Tg ≥ 170 °C, Td ≥ 340 °C). If cost allows, use a thermally conductive prepreg (~1.0–1.5 W/m·K versus 0.3 for standard FR-4) on the L5–L6 and L1–L2 layers. This roughly halves the vertical thermal resistance of those dielectrics.

**Copper balance:** 1/2/2/2/2/1 oz is symmetric, which minimizes warpage on a small, heavy-copper board.

**Total copper:** about 370 µm, the main lever for spreading heat across the board.

**Surface finish:** ENIG, for flat pads on fine-pitch parts and DFN thermal pads.

---

## 2. Why this layer order

1. **Low commutation loop inductance.** L3 (VBAT) and L4 (GND) are separated by only 100 µm.
   - High-side drain vias land on L3 and low-side source vias land on L4.
   - The switching loop runs vertically through about 1 mm of board, not laterally across it.
   - This keeps loop inductance in the low single-digit nH range, which reduces ringing and the switching losses that turn into heat.
   - The plane pair also adds a small amount of distributed decoupling.
2. **Heat flows inward from the FETs.** The FETs on L6 sit 75 µm from L5. Filled vias couple them straight into four 2 oz planes (L2 to L5), which spread heat laterally across the entire 650 mm².
3. **The top side serves as the radiator.** The L1 GND fill and the L2 plane carry heat to the top surface and the propwash airflow. Meanwhile the sensitive analog parts (gyro, baro) sit two solid GND planes away from the switching nodes.
4. **Two solid ground planes (L2 and L4)** shield the gyro from the switching noise on L5/L6.

---

## 3. Thermal via strategy (the critical part)

**Per-via estimate (0.25 mm drill, 25 µm barrel plating, 1.15 mm long):**

> R ≈ L / (k·A) = 1.15e-3 / (385 × π × 0.25e-3 × 25e-6) ≈ **150 K/W per unfilled via**

**Rules under each FET exposed pad:**

- Use **6–9 vias per DFN