# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have your Cycle 30 results or exact FET part numbers, so this design assumes typical whoop/toothpick AIO conditions: 1S–4S input, DFN3×3 or DFN2×2 FETs, and about 15–25 A peak per ESC. Adjust the copper weights if your current targets differ.

---

## 1. Design Philosophy

FET heat leaves the board through three paths:
1. **Laterally**, through thick copper pours.
2. **Vertically**, through vias into the inner planes.
3. **Out of the board**, into the air and the motor/battery leads.

On a board this small, the **inner planes are the heatsink**. So the stackup aims for:
- Heavy copper on as many layers as possible.
- A very thin dielectric between the FET layer and the first plane.
- Filled via arrays under every drain and source pad.

---

## 2. Stackup (target 1.2 mm ±10%)

| # | Layer | Function | Cu (finished) | Dielectric below | Thickness (mm) |
|---|---|---|---|---|---|
| L1 | **Top** | 24 FETs, phase nodes, gate drive, current sense | **2 oz (70 µm)** | — | 0.070 |
| | Prepreg | 2× 1080 (high resin, fills 2 oz) | — | εr ≈ 4.2 | **0.100** |
| L2 | **GND plane** | Solid return, primary heat spreader | **2 oz** | — | 0.070 |
| | Core | FR-4, high-Tg | — | | 0.200 |
| L3 | **VBAT plane** | Battery bus to high-side drains, heat spreader | **2 oz** | — | 0.070 |
| | Prepreg | 2× 2116 (center, symmetry) | — | | 0.250 |
| L4 | **GND plane** | Second return/heat plane, shields MCU side | **2 oz** | — | 0.070 |
| | Core | FR-4 | — | | 0.200 |
| L5 | **Signal** | MCU, SPI (gyro), UART, DShot, LED | 1 oz (35 µm) | — | 0.035 |
| | Prepreg | 1× 2116 / 2× 1080 | — | | 0.100 |
| L6 | **Bottom** | MCU, gyro, regulators, OSD, BEC; FET pad mirrors | **2 oz** | — | 0.070 |
| | | | | **Total + mask** | **≈ 1.27** |

**Copper total:** about 11 oz across 6 layers, roughly 2.5× a standard 1 oz build.

**Material:** high-Tg FR-4 (Tg ≥ 170 °C, Td ≥ 340 °C). The board will see repeated hot cycles and multiple reflow passes on a double-sided assembly. Standard FR-4 conducts heat poorly (k ≈ 0.3 W/m·K). That is why the L1→L2 dielectric is kept at 0.1 mm: it is the only dielectric resistance that matters much.

---

## 3. Why This Arrangement

**L1 FETs over L2 GND, 0.1 mm apart**
- The low-side FET sources sink straight into GND with minimal thermal and inductive impedance.
- The tight coupling also shrinks the commutation loop (high-side drain → low-side source → bulk cap), which reduces ringing and switching loss. Lower switching loss means less heat to remove in the first place.

**L3 VBAT plane**
- A full-area plane, not traces, so the high-side drains get a large copper heat spreader.
- Pairing it with L2 GND at 0.2 mm gives some distributed plane capacitance for HF decoupling.

**L4 second GND plane**
- Doubles the ground heat mass.
- Gives L5 signals a solid reference.
- Electrostatically shields the MCU/gyro from phase-node dV/dt.

**Only one 1 oz layer (L5)**
- Fine-pitch routing for the MCU's QFN/LQFP breakout needs thinner copper, since 2 oz limits trace/space to about 0.15/0.15 mm.

**FETs on top, flight electronics on bottom**
- Keeps the gyro thermally and electrically as far as possible from the FETs.
- Place the gyro near the board center on L6, under a quiet area of L1.

---

## 4. Thermal Via Strategy (the biggest lever)

A single via's thermal resistance through 1.2 mm of board:

| Via type | Drill | Thermal resistance per via |
|---|---|---|
| Plated only (25 µm wall) | 0.30 mm | ≈ **140 K/W** |
| Copper-filled | 0.30 mm | ≈ **45 K/W** |
| Resin-filled + capped (VIPPO) | 0.25 mm | ≈ 120 K/W (but allows via-in-pad) |

**Specification:**
- **Via-in-pad, filled and capped (IPC-4761 Type VII)** under every FET drain and source pad. Without capping, open vias wick solder away and cause voids under the exposed pad.
- Use **0.25–0.3 mm drill at 0.6–0.7 mm pitch**. That fits about 6–9 vias under a DFN3×3 drain pad and 3–4 under a DFN2×2.
- Total: about **150–220 thermal vias** across the 24 FETs.
- With 8 plated vias per pad (~18 K/W), heat reaches all four heavy planes. Copper-fill drops that to about 6 K/W if budget allows.
- **Stitching vias:** connect L2 and L4 with a 1.0–1.5 mm grid around the ESC region so both GND planes share heat.
- **Phase nodes:** no inner-layer copper. Phase pours stay on L1, mirrored on L6 only at the motor pads. This limits capacitive coupling and EMI. Heat from the phase pads leaves mainly through the motor wires, which are a significant heat sink.

---

## 5. Layout Rules That Pair With the Stackup

1. **Distribute the FETs.** Place one ESC per quadrant near its motor pads rather than clustering them, so heat spreads evenly across the full plane area.
2. **Fill copper everywhere unused** on L1 and L6. Use GND pours stitched to L2/L4, and keep copper balance above 70% per layer to prevent warpage with 2 oz cores.
3. **Make the battery pads and VBAT entry large.** Battery leads are a major heat exit; tie them to L3 with 20+ vias.
4. **Add a thermal break for the gyro/MCU zone.** Use routed slots or a narrow neck in