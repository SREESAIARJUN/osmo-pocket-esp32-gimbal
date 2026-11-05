# 6-Layer Stackup for a 25.5 × 25.5 mm AIO Flight Controller: Thermal Optimization for 24 MOSFETs (Cycle 1)

## 0. Cycle 1 Design Assumptions

These assumptions are frozen for this cycle. Correct any that don't match your design before Cycle 2.

| Parameter | Assumption |
|---|---|
| Form factor | 25.5 mm whoop mount pattern. Board outline is about 29–30 mm square, with M2 holes. |
| Topology | FC (STM32H7/AT32F435-class MCU, gyro) plus a 4-in-1 ESC (4 motors × 3 half-bridges × 2 FETs = 24 N-MOSFETs) |
| MOSFETs | N-channel, PDFN 3×3 or 2×2. The exposed pad is the **drain**. |
| Power | 1–2S. About 12 A continuous and 15–20 A burst per motor, so about 60 A total burst. |
| Thickness target | ~1.0 mm finished. Whoop weight budget rules out 1.6 mm. |
| Cooling | Prop wash airflow. Plastic whoop frame, so there is no chassis heatsink. |

## 1. Core Thermal Strategy

On an N-FET half-bridge, the two FETs have different tab nets, so they need different heat paths:

- **High-side FET tab = VBAT.** This tab can be stitched directly into a full VBAT plane, which is a large spreader. This is the best available heat path.
- **Low-side FET tab = phase (switch) node.** This tab must stay a small local island to limit switching EMI. It cannot be dumped into a plane. Heat must therefore cross a dielectric into GND. This is why the L1–L2 prepreg is made **very thin and thermally conductive**.

**Placement rule:**
- 12 high-side FETs go on the **bottom side (L6)**, stitched to VBAT on L5.
- 12 low-side FETs go on the **top side (L1)**, over the GND plane on L2.
- Phase nodes connect top to bottom through via arrays.

Spreading the FETs across both sides also halves heat density per side and