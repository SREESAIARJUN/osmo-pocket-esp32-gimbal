# 6-Layer Thermal-Optimized Stackup: 25.5×25.5 mm AIO Flight Controller (24-MOSFET 4-in-1 ESC)

I don't have context on your previous design cycles, so this is a standalone design. Tell me your current stackup, MOSFET part number, target current, and fab house, and I can adjust it against those.

---

## 1. Design Assumptions

| Parameter | Assumption |
|---|---|
| Board outline | ~32×32 mm with 25.5 mm mounting pattern (whoop/toothpick class) |
| MOSFETs | 24 discrete N-channel parts (4 ESCs × 3 phases × HS/LS), DFN3×3 or DFN2×2 with an exposed drain pad |
| Input | 1–4S. Plan conductors for 6S if you want headroom (≥0.3 mm spacing between VBAT and logic) |
| Continuous current | ~12–20 A per ESC |
| Fine-pitch parts | MCU (QFN/LQFP, 0.4–0.5 mm pitch), IMU (LGA), ESC MCU or driver (QFN) |
| Target thickness | ~1.2 mm (weight vs. stiffness compromise) |

**Core conflict:** heavy outer copper spreads heat best, but 2 oz outer copper cannot reliably etch 0.4 mm pitch. The stackup below therefore uses:
- **1 oz outer** copper for fine-pitch routing,
- **2 oz inner** copper as the bulk heat and current spreaders,
- **very thin outer dielectrics** so heat crosses from L1 to L2 quickly.

---

## 2. Stackup

| # | Layer | Function | Cu (finished) | Dielectric below | Thickness |
|---|---|---|---|---|---|
| — | Soldermask | — | — | — | 0.020 |
| **L1** | TOP | MOSFETs, gate drivers, phase pours, high-speed signals | 1 oz base + plating (~45 µm) | — | 0.045 |
| — | Prepreg | 1× 1080 (or thermally enhanced PP, ≥1.0 W/m·K) | — | **0.075** | 0.075 |
| **L2** | GND | Solid ground. Primary heat spreader under FETs and reference for L1 | 1 oz (35 µm) | — | 0.035 |
| — | Core | FR-4, Tg ≥170 °C | — | 0.20 | 0.200 |
| **L3** | VBAT | Full battery pour. High-side drain heat sink | **2 oz (70 µm)** | — | 0.070 |
| — | Prepreg | 2× 2116, resin-rich to fill 2 oz copper | — | 0.25 | 0.250 |
| **L4** | GND / PHASE | GND flood, plus local phase islands under each FET pair | **2 oz (70 µm)** | — | 0.070 |
| — | Core | FR-4, Tg ≥170 °C | — | 0.20 | 0.200 |
| **L5** | GND | Solid ground. Reference for L6 and shield for IMU/MCU | 1 oz (35 µm) | — | 0.035 |
| — | Prepreg | 1× 1080 | — | **0.075** | 0.075 |
| **L6** | BOTTOM | FC MCU, IMU, OSD, regulators, connectors | 1 oz base + plating | — | 0.045 |
| — | Soldermask | — | — | — | 0.020 |
| | | | | | **≈1.14 mm (spec 1.2 ±10 %)** |

**Why this order:**
- **Symmetric copper (1-1-2-2-1-1).** This prevents bow and twist, which matters for flat MOSFET solder joints on a small board.
- **Thin L1–L2 prepreg (75 µm).** It lowers vertical thermal resistance into the GND plane. It also gives tight coupling for impedance control and a small switching-loop area, which reduces ringing and EMI.
- **Adjacent VBAT and GND planes (L3/L4).** Together they act as a distributed bulk capacitor with low power-loop inductance.
- **Separated sides.** The ESC power stage sits on top and the noise-sensitive FC sits on the bottom, with three ground-referenced layers between them.

**Material options:**
- **Baseline:** high-Tg FR-4 (e.g., IT-180A, S1000-2). Required anyway, since FET pads can exceed 110 °C during bursts.
- **Upgrade:** thermally conductive prepreg on L1–L2 and L5–L6 (e.g., Ventec VT-4A2 or similar, ~1.5–2.2 W/m·K vs. 0.3 for FR-4). This cuts dielectric thermal resistance by about 5×. Confirm availability with your fab.

---

## 3. Layer Usage Rules

### L1 (Top, power stage)
- Arrange the 24 FETs as **4 quadrant clusters of 6**. Pair each high-side and low-side FET so the phase node is a short, wide pour between them.
- Run 3 phase pours per ESC to the motor pads, at least 1.5 mm wide on L1 and doubled on L4.
- Place decoupling caps (VBAT–GND) **within 2 mm** of each HS-drain/LS-source pair.
- Keep logic routing out of the FET zone except gate traces. Gate traces should be short, with their return directly above L2 GND.

### L2 (GND)
- **No splits** under the FET clusters. Every slot here raises both thermal resistance and loop inductance.
- Use one continuous ground. No separate AGND/PGND islands; partition by placement instead.

### L3 (VBAT)
- Make it a full-board flood except keep-outs around FC-only vias.
- High-side FET drain pads connect here through thermal via arrays.

### L4 (GND + phase islands)
- Make it primarily a GND flood, which acts as the second heat spreader.
- Under each HS/LS pair, add a **phase-node island** stitched to the L1 phase pour. This doubles the phase copper cross-section.
- Keep each island ≤ the FET pair footprint plus 1 mm. Phase nodes switch at full dV/dt, and large islands radiate and couple into L3/L5.

### L5 (GND)
- Solid plane shielding the IMU and MCU from the power stage.
- Place a "quiet zone" under the IMU with no vias from power nets.

### L6 (Bottom, flight controller)
- Place the IMU as far as possible from the FET clusters and motor-pad current paths, ideally in the board center under the stack nut-free region.
- Use leftover area as GND pour stitched to L5 for extra spreading.

---

## 4. Thermal Via Strategy (the key lever)

**Under every MOSFET exposed pad:**

| Item | Spec |
|---|---|
| Via drill | 0.20–0.25 mm |
| Pitch | 0.5–0.6 mm grid |
| Count | DFN3×3: 6–9 vias. DFN2×2: 4 vias |
| Fill | **Copper-filled and capped (VIPPO)**. Required: open vias wick solder and cause voids |
| Connection | Solid (no thermal relief spokes) to the target plane |

**Targets by FET pad:**
- High-side drain