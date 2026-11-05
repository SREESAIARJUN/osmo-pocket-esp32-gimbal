# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have the Cycle 1–25 history, so this design is self-contained. Where it conflicts with earlier constraints such as board thickness, fab house, or MOSFET package, tell me and I'll reconcile.

**Assumptions:**
- 2–6S whoop/toothpick class board.
- 24 × N-ch MOSFETs in DFN 3×3 or 2×2 packages (4 motors × 3 phases × high/low side).
- The 20 × 20 mm mounting pattern is retained.

---

## 1. Design Philosophy

1. **Thick copper close to the heat source.** The MOSFET exposed drain pads dump heat into three 2 oz planes within about 0.5 mm of the surface.
2. **Thin dielectrics on the ESC side.** FR-4 conducts about 0.3 W/m·K and copper about 390 W/m·K, so dielectric thickness is the main thermal bottleneck.
3. **Split the board thermally by side.**
   - The ESC (hot) components go on L1.
   - The MCU and gyro (sensitive) components go on L6.
   - A 1 oz reference plane and the thickest dielectric sit between them.
4. **Filled-and-capped via-in-pad** (VIPPO) under every MOSFET pad, tying all heavy planes together into one heat-spreading slab.

---

## 2. Stackup (≈1.2 mm finished)

| Layer | Function | Cu (finished) | Dielectric below | Notes |
|---|---|---|---|---|
| **L1 – Top** | ESC power: MOSFETs, gate drivers, phase pours, VBAT pours | **2 oz (70 µm)** | Prepreg 0.10 mm, thermally enhanced (≥1.0 W/m·K, e.g. Ventec VT-4B or equivalent) | 8/8 mil minimum trace/space; large copper pours |
| **L2** | **GND plane** (solid, primary heat spreader) | **2 oz** | Core 0.20 mm, high-Tg FR-4 (Tg ≥170 °C) | No splits under MOSFETs |
| **L3** | **VBAT plane** + bulk current distribution | **2 oz** | Prepreg 0.20 mm | Feeds high-side drains directly through via arrays |
| **L4** | **GND / phase overflow pours** + slow signals (motor telemetry, current sense) | **2 oz** | Core 0.20 mm | Fill unused area with GND copper for spreading |
| **L5** | **GND reference** for FC signals | 1 oz (35 µm) | Prepreg 0.10 mm | Solid under the MCU, gyro and USB |
| **L6 – Bottom** | FC side: MCU, gyro, baro, OSD, regulators, connectors | 1 oz | — | 0.5 mm-pitch QFN/BGA routing; controlled impedance |

**Thickness budget:**
- Copper: 4 × 70 µm + 2 × 35 µm = 350 µm
- Dielectric: 0.10 + 0.20 + 0.20 + 0.20 + 0.10 = 0.80 mm
- Solder mask and finish: ≈ 0.05 mm
- **Total: ≈ 1.20 mm ± 10 %**

**If your frame or stack height allows 1.6 mm:** thicken only the L3–L4 core (to ≈0.6 mm). Keep the L1–L2 and L5–L6 prepregs at 0.10 mm.

**Finish:** ENIG (flat pads for DFN and fine-pitch parts, good for VIPPO caps).

---

## 3. Thermal Via Strategy (the critical part)

| Parameter | Value |
|---|---|
| Via type | Through-hole, **copper- or epoxy-filled, planarized and capped (VIPPO)** |
| Drill / pad | 0.25 mm / 0.45 mm |
| Plating | ≥25 µm barrel |
| Pitch | 0.6–0.7 mm grid |
| Per DFN 3×3 drain pad | 6–9 vias |
| Per DFN 2×2 drain pad | 4 vias |
| Plane connection | **Solid (no thermal reliefs)** on L2/L3/L4 for drain/source nets |

**Why each MOSFET connects where it does:**
- **High-side MOSFET:** the drain pad is VBAT, so it stitches straight into L3 (VBAT plane), and the L3 copper spreads heat across the board.
- **Low-side MOSFET:** the drain pad is the phase node. Stitch it to a phase pour duplicated on L1 + L4, sized as large as switching-node EMI tolerates. Its source connects to L2 GND with a dense via array next to the package.
- **Thermal performance:** each filled 0.25 mm via is roughly 100–150 K/W. Eight in parallel give about 15 K/W through the board, versus more than 200 K/W through bare FR-4.

**Edge stitching:** place a GND via fence at 1.0 mm pitch around the board perimeter, tying L2/L4/L5. This spreads heat to the edges and mounting holes, and through the standoffs into the frame.

**Mounting holes:** plated, connected to GND on all layers with no reliefs. Optionally use exposed copper rings under the screw heads/grommets as a heat path into the frame. Check that this doesn't defeat soft-mount vibration isolation.

---

## 4. Placement Rules That Support the Stackup

1. **MOSFET layout.** Arrange the 4 ESC channels in the four quadrants on L1, with MOSFETs pushed toward the **board edges** where prop wash flows. Keep at least 1.5 mm between packages for copper spreading.
2. **Gate drivers.** Place them inboard of each MOSFET group; they generate little heat.
3. **Gyro isolation.** Put the gyro on L6 near the board center, as far as possible from the MOSFET quadrants.
   - Void L1–L4 copper in a ~4 × 4 mm island under the gyro, leaving only the L5 GND reference.
   - Connect that island with a narrow neck to limit thermal and vibration coupling.
4. **Bulk caps and VBAT entry.** Put the battery pads on L1 at one edge, flooding L1 + L3 so current and heat share the same copper.
5. **Copper balance.** The 2/2/2/2/1/1 oz stack is asymmetric, which creates warp risk on a thin board.
   - Pour GND fill on L5/L6 wherever possible.
   - Specify **warp ≤0.75 %** to the fab.
   - If the fab can't hold that, move to 2/2/2/2/2/1.5 oz.

---

## 5. Signal Integrity Checks (L6 over L5, 0.10 mm prepreg, εr ≈ 4.2)

| Net | Target | Approx. geometry |
|---|---|---|
| USB D+/D− | 90 Ω differential | ~0.12 mm trace / 0.13 mm gap |
| Single-ended (SPI gyro, UART, DShot) | 50 Ω (not critical) | ~0.17 mm |
| Gyro SPI | Short, on L6 only, no layer changes | — |

- **Phase and current-sense nets:** route current-sense on L4, referenced to the L5 GND plane, as Kelvin pairs from the shunts. Never run them under switching nodes on L1.
- **Return paths:** never route an FC-side signal across a void on L5.

---

## 6