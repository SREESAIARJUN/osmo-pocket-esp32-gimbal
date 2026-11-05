# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have the context for "Cycle 16" (prior iterations, requirements, or constraints), so this design states its assumptions. Change any that don't match your project.

**Assumptions**
- 4 ESC channels × 6 single N-FETs, in DFN3.3×3.3 or similar (exposed drain pad)
- 2–4S input, about 12–20 A continuous per motor
- Whoop or micro frame with prop wash over the bottom side
- 20 × 20 mm M2 mounting pattern
- Target finished thickness about 1.1 mm (whoop-compatible, still rigid)

---

## 1. Layer Stack

| Layer | Function | Cu (finished) | Dielectric below | Thickness |
|---|---|---|---|---|
| **L1 Top** | MCU, gyro, ESC MCUs/drivers, fine signals | 1 oz base + plating (~45 µm) | **Thermal prepreg** (1080, k ≥ 1.0 W/m·K) | 75 µm |
| **L2** | **Solid GND** (reference for L1, heat spreader) | 1 oz (35 µm) | Core | 200 µm |
| **L3** | **VBAT plane** + phase pours, heavy current | **2 oz (70 µm)** | Prepreg (2×2116) | 200 µm |
| **L4** | **PGND plane** (power return, main heat spreader) | **2 oz (70 µm)** | Core | 200 µm |
| **L5** | Mixed: GND pour + secondary signals (gate drives, ESC telemetry) | 1 oz (35 µm) | **Thermal prepreg** (1080, k ≥ 1.0 W/m·K) | 75 µm |
| **L6 Bottom** | **24 MOSFETs**, phase nodes, VBAT/PGND pours | 1 oz base + plating (~45 µm) | — | — |

**Total:** about 1.05 mm of copper and dielectric, about 1.1 mm with solder mask.

**Why this arrangement**
- **Symmetric copper weights** (1/1/2/2/1/1) prevent bow and twist. Warp is a real failure mode on small, heavily poured boards.
- **Thin 75 µm thermal prepreg** sits directly under the MOSFET layer, giving the shortest path from drain pads into the L5/L4 spreaders.
- **2 oz inner planes (L3/L4)** carry battery current and spread heat laterally across the whole 650 mm² board. This keeps each FET from acting as an isolated hotspot.
- **The L1/L2 split** keeps the gyro and MCU on a quiet, separately referenced side, away from the switching plane pair.

**Materials**
- High-Tg FR-4 (Tg ≥ 170 °C, e.g., Isola 370HR or Shengyi S1000-2) for the cores.
- A thermally conductive prepreg (e.g., Ventec VT-4B series, 1–2.2 W/m·K) for the L1–L2 and L5–L6 gaps, if your fab stocks it. Otherwise, use standard 1080 and lean harder on vias.

---

## 2. MOSFET Placement (L6)

- **All 24 FETs go on the bottom,** directly in prop wash. This is forced convection for free.
- Arrange them as **4 quadrant blocks of 6** (one per motor, each block near its motor pad corner). Interleave high-side and low-side devices so dissipation is uniform. Don't cluster all high-sides together.
- Leave **≥ 0.5 mm copper "rivers"** between blocks, connected to the L4 PGND spreader. Avoid isolated islands.
- Keep the **gyro on L1 diagonally opposite** the hottest bridge, or centered over the coolest region. Do not place it directly above a FET block (thermal drift and vibration coupling).

---

## 3. Thermal Via Strategy (the dominant lever)

**Via-in-pad, filled and capped (VIPPO)** under every exposed drain pad:

| Parameter | Value |
|---|---|
| Drill | 0.20 mm |
| Pad | 0.40–0.45 mm |
| Pitch | 0.50–0.60 mm |
| Fill | **Copper fill** preferred (epoxy fill + cap acceptable) |
| Count | 6–9 per DFN3.3 pad |

**Rough numbers** for a 1.05 mm via length:
- Plated-only via (25 µm wall): about 150 K/W each
- **Copper-filled via:** about 55 K/W each
- 8 filled vias in parallel: **about 7 K/W** from pad to inner planes
- Through 75 µm prepreg alone over a 9 mm² pad: about 21 K/W at k = 0.4, or about 8 K/W at k = 1.0

So the vias matter more than the dielectric, and both matter. Use both.

**Additional via rules**
- **Stitch PGND (L4) to the L6 and L2 pours** with a 1 mm grid of 0.25 mm vias across all free area. These act as both a thermal and an EMI fence.
- **Source pins** of low-side FETs get direct vias to L4 PGND.
- **High-side drains** get vias to L3 VBAT.
- No thermal reliefs on power and FET pads; use **solid connections**. Use paste-mask windowing (about 50–60% coverage, segmented) to manage solder voiding and float.

---

## 4. Plane and Pour Rules

- **L3 VBAT:** one continuous plane, about 90% fill. Bulk caps at the pad entry, with ceramics per bridge using via-in-pad to L3/L4.
- **L4 PGND:** solid. **Do not split.** Tie it to L2 signal GND at a single star point near the current shunt or ADC reference, through a stitched corridor. This prevents ESC switching currents from flowing under the gyro.
- **Phase nodes (L6 plus mirrored on L3 if needed):** use the minimum area that carries the current. They are the EMI antennas, so don't flood them for "cooling." Cool via the drain and source nets instead.
- **L5:** route gate-drive traces short and paired with their return. Flood every remaining space with GND, stitched to L4.