# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC + 4-in-1 ESC (24 MOSFETs)

I don't have the context for "Cycle 14" (previous iterations, your requirements doc, or fab-house limits). The design below is a complete recommendation. Swap in your fab's materials and rules where they differ.

---

## 1. Design Goals and Assumptions

- **Topology:** 4 motors × 3 phases × 2 FETs (high side + low side) = 24 discrete FETs, likely DFN2x2 or DFN3x3.
- **Battery:** 2–4S, with roughly 15–25 A continuous per motor in short bursts.
- **Cooling:** mostly conduction into copper, then convection from prop wash.
- **Thermal strategy:** turn as much copper as possible into heat spreaders that connect electrically to FET drains and sources.
  - High-side drains sit on VBAT.
  - Low-side sources sit on PGND.
  - Both of these can be full planes, so they make excellent heatsinks.

---

## 2. Stackup (≈1.06 mm finished, target 1.0 mm ±10%)

| Layer | Function | Cu weight | Thickness (mm) | Notes |
|---|---|---|---|---|
| **L1 Top** | FETs (12), MCU, gyro, signal, phase pours | 1 oz + plating (~1.4 oz) | 0.045 | Large phase/VBAT/PGND copper pours around FETs |
| PP | Prepreg 1080 (high-Tg) | — | 0.080 | Thin dielectric couples L1 heat into L2 |
| **L2** | **GND (solid)** | 1 oz | 0.035 | Signal reference for L1; secondary heat spreader |
| Core | High-Tg FR-4 | — | 0.250 | |
| **L3** | **VBAT plane** | **2 oz** | 0.070 | High-side drain heatsink via VIPPO |
| PP | Prepreg 2× 1080 / 2116 | — | 0.100 | Thin: tight VBAT/PGND coupling, low loop inductance |
| **L4** | **PGND plane** | **2 oz** | 0.070 | Low-side source heatsink and return path |
| Core | High-Tg FR-4 | — | 0.250 | |
| **L5** | Gate drive, current sense, phase islands, GND fill | 1 oz | 0.035 | Fill all unused area with GND or phase copper |
| PP | Prepreg 1080 | — | 0.080 | |
| **L6 Bottom** | FETs (12), phase pours, motor pads, cap bank | 1 oz + plating | 0.045 | Mirror of the L1 power section |

**Materials:**
- Use high-Tg FR-4 (Tg ≥ 170 °C, e.g., IT-180A or S1000-2 class) for both prepreg and cores.
- If your budget allows, ask the fab for a thermally enhanced prepreg (≥ 1 W/m·K) on the L1–L2 and L5–L6 layers. Those are the dielectrics heat must cross first.

**Total copper ≈ 8.8 oz across 6 layers.** That is a lot of lateral heat spreading for a 650 mm² board.

---

## 3. Why This Arrangement

1. **The 2 oz VBAT/PGND core pair (L3/L4)** carries the bulk current and doubles as the main heat spreader. The thin 0.1 mm gap between them forms a distributed bypass capacitor. This cuts switching ringing, which reduces FET switching loss and therefore heat.
2. **L2 and L5 stay at 1 oz.** That preserves fine-pitch routing and impedance control near the MCU, gyro and gate drivers, where 2 oz etching would limit trace and space.
3. **Thin outer dielectrics (0.08 mm)** keep the thermal resistance from a FET pad down to the first plane low, roughly 3.5× better than a 0.2 mm prepreg.
4. **The stack is symmetric**, which keeps the board from warping during reflow. Warp matters with 24 bottom-terminated parts.

---

## 4. MOSFET Placement and Via Strategy

**Split and offset the FETs: 12 on top, 12 on bottom.**
- Do not place top and bottom FETs directly over each other, or their hotspots stack.
- Stagger them so each FET's footprint overlaps plane copper with no heat source on the opposite side.

**Connect each FET node to the matching copper:**

| FET node | Connects to | Via treatment |
|---|---|---|
| High-side drain (tab) | L3 VBAT | VIPPO array under the tab: 0.25 mm drill, 0.45 mm pad, ~0.5 mm pitch; 6–9 vias per DFN3x3 |
| Low-side source | L4 PGND (+ L2/L5 GND) | VIPPO or a via fence directly at the source pins |
| Phase node (LS drain / HS source) | L1/L6 pour + **L5 phase island** | 4–6 vias stitch the outer pour to a matching L5 island. This doubles the phase-node copper without a full plane. |

**Via-in-pad details:**
- Vias must be **filled and capped** (VIPPO). This prevents solder wicking and voids under the thermal tab.
- Specify conductive or copper-filled vias if your fab offers them. Non-conductive epoxy fill also works, but conducts heat less well.

**Other placement rules:**
- **Keep the planes continuous.** Avoid slotting L3/L4 with antipads between FETs. Route signals on L1/L5/L6 so the power planes stay as solid sheets.
- **Edge stitching:** add a GND via fence around the perimeter at ≤ 1.5 mm pitch to tie L2/L4/L5 together thermally and electrically.
- **Mounting holes** (20 × 20 or 25.5 mm pattern): if the frame is metal or carbon, tie the plated rings to GND with thermal relief removed. Full connection lets the hardware act as a heatsink.

---

## 5. Surface and Mechanical

- **Finish:** ENIG for flat pads on fine-pitch DFN/QFN parts and good solder joints on FET tabs.
- **Solder mask:** leave **mask-defined exposed copper strips** on the phase and VBAT pours of L1/L6, about 0.5–1 mm wide. Users can add solder blobs or bond a thin aluminum or graphite spreader there.
- **Conformal coat:** if used, keep it thin over the FETs, since it raises thermal resistance to air slightly.

---

## 6. Fab Rules to Confirm

| Parameter | Value |
|---|---|
| Outer trace/space