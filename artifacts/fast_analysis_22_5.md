# Thermal Via Array Under QFN-20 Power MOSFETs (20 A Continuous)

I don't have the history from earlier cycles, so this design uses the stated assumptions below. Replace them with your actual part, stackup, and fab capabilities.

## 1. Design Assumptions

| Parameter | Assumed value |
|---|---|
| Package | QFN-20 power MOSFET, exposed drain pad ≈ 3.6 × 4.2 mm (check your datasheet land pattern) |
| R<sub>DS(on)</sub> | 2.0 mΩ at 25 °C, about 3.0 mΩ at T<sub>J</sub> = 125 °C |
| Board | 1.6 mm, 4-layer. 2 oz outer copper, 1 oz inner copper. L2 is a solid drain/thermal plane, L4 is a heat-spreading pour. |
| Via | 0.30 mm drill, 25 µm barrel plating, 0.60 mm pad diameter (0.15 mm annular ring per side) |

If you meant a 0.6 mm ring *width* (1.5 mm pad), the pitch below must increase to at least 1.6 mm. That pad would be unusually large for a QFN pad.

## 2. Power to Remove

- Conduction loss: P = I²·R<sub>DS(on),hot</sub> = 20² × 0.003 = **1.2 W per FET**.
- Switching loss: add this from your own calculation. A margin of about 0.3–0.8 W is typical at 100–300 kHz.
- Design target: **about 2 W per FET**.
- Budget for the via array: ΔT ≤ 10 °C, so R<sub>θ,vias</sub> ≤ 5 °C/W.

## 3. Thermal Resistance per Via

The barrel cross-section is π/4·(0.30² − 0.25²) = 0.0216 mm².

| Via type | Calculation | R<sub>θ</sub> per via |
|---|---|---|
| Unfilled barrel | 1.6 mm / (385 W/m·K × 0.0216 mm²) | **≈ 190 °C/W** |
| Copper-filled (VIPPO) | full 0.30 mm Cu core | **≈ 59 °C/W** |
| Epoxy-filled, non-conductive | about the same as unfilled | ≈ 185 °C/W |

## 4. Array Layout

### Primary array, inside the exposed pad: 4 × 4 = 16 vias
- **Pitch:** 0.90 mm on both axes. Adjacent pads are on the same net, so the 0.30 mm pad-to-pad gap is fine. Hole-to-hole web is 0.60 mm, which meets the typical fab minimum of ≥ 0.5 mm.
- **Array span:** 2.70 × 2.70 mm, centered on the pad.
- **Edge inset:** ≥ 0.45 mm from the pad edge on the short axis and 0.75 mm on the long axis. This keeps solder away from the pad edge and prevents wicking toward the pins.

```
   Exposed drain pad 3.6 x 4.2 mm   (o = 0.3 drill / 0.6 pad)
  +--------------------------------+
  |                                |
  |     o     o     o     o        |
  |                                |   pitch 0.90 mm
  |     o     o     o     o        |
  |                                |
  |     o     o     o     o        |
  |                                |
  |     o     o     o     o        |
  |                                |
  +--------------------------------+
     pins 1-20 around perimeter (gate/source/drain)
```

### Secondary array, in the drain copper pour beyond the pad
- Add **12–20 vias** in a 1.0 mm grid.
- Place them within 3 mm of the pad on the drain-tab side(s).
- No solder-wicking concern here, so they can be open.

### Resulting performance

| Configuration | R<sub>θ</sub>, 16 vias | ΔT at 2 W | Verdict |
|---|---|---|---|
| Unfilled, in-pad | 11.9 °C/W | 24 °C | Marginal |
| Unfilled, 16 in-pad + 16 out-of-pad | about 6.5 °C/W (effective) | 13 °C | Acceptable |
| **Cu-filled and capped (VIPPO)** | **3.7 °C/W** | **7.4 °C** | **Recommended** |

### Current capacity
- 20 A shared across 16 vias is about 1.25 A per via, well within the 0.3 mm via rating of about 2 A at ΔT 10 °C.
- Most drain current should still flow in the top-layer pour.
- Give the source pins a wide L1 copper pour, plus their own 6–8 via stitching row into the source/GND plane.

## 5. Manufacturing Rules

1. **Via fill.** Specify IPC-4761 **Type VII** (filled and planarized-capped) for vias in the pad.
   - If cost prohibits that, use **Type II/III** (tented or plugged from the bottom) to stop solder wicking and voids.
   - Never leave in-pad vias open on both sides.
2. **Stencil.** Use a window-pane aperture on the exposed pad.
   - Apply a 3 × 3 or 4 × 4 paste grid with **50–60 % coverage**.
   - Place paste squares *between* vias.
   - Target voiding below 25 % per IPC-7093.
3. **Solder mask.** Leave no mask over the exposed pad. On the bottom side, tent the vias with mask if they are not capped.
4. **Planes.**
   - Connect L2/L3 with **solid, non-thermal-relief** connections to all thermal vias.
   - Make the L4 copper pour at least 4× the package footprint, about 15 × 15 mm per FET.
5. **Parallel FETs.**
   - Keep FET-to-FET spacing ≥ 8 mm so the hot spots don't overlap.
   - Mirror the via arrays so each device sees the same thermal path, which helps current sharing.

## 6. Verification Checklist for This Cycle
- [ ] Confirm the exposed pad dimensions against the datasheet land pattern. Adjust the grid to N × M with 0.9–1.0 mm pitch.
- [ ] Confirm with the fab: 0.3/0.6 via, VIPPO capability, and the 0.6 mm web.
- [ ] Recompute losses with actual R<sub>DS(on)</sub> and switching loss.
- [ ] Run a thermal simulation or IR-camera test at 20 A. Target T<sub>J</sub> < 110 °C at maximum ambient.
- [ ] Check X-ray voiding on the first-article build.

If you send the exact MOSFET part number and stackup, I can size the exact via count and grid for that pad.