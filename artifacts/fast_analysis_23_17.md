# 6-Layer Stackup: 25.5 × 25.5 mm AIO Flight Controller with 24 MOSFETs

I don't know what "Cycle 23" refers to in your project, so I've treated it as a design iteration label. If it implies constraints from earlier cycles, such as a fixed thickness, fab house or current rating, tell me and I'll adjust.

---

## 1. Design Assumptions

| Parameter | Assumption |
|---|---|
| ESC topology | 4 × 3-phase bridge = 24 N-channel MOSFETs (or 12 dual half-bridge packages) |
| Package | PDFN 3.3×3.3 single or DFN 3×3 dual (half-bridge preferred to save area) |
| Battery | 2–6S, ~35–45 A continuous total, burst higher |
| Mounting | 20 × 20 mm M2 pattern with grommets (negligible conduction to the frame) |
| Other parts | MCU, IMU, BEC, gate drivers or pre-drivers, current sense |
| Target thickness | ~1.2 mm (common for this class, with good rigidity) |

**Area reality check:** The board is ~650 mm². Subtracting mounting holes and pads leaves ~520 mm² usable. Twenty-four 3.3 mm single MOSFETs need ~260 mm² plus clearances, so they won't fit on one side. Two options:

- **Option A (recommended):** 12 dual half-bridge DFN3×3 packages, 6 per side.
- **Option B:** 24 singles, 12 per side.

Either way, the heat sources sit on both outer layers. That shapes the whole stackup.

---

## 2. Stackup (≈1.20 mm finished)

| # | Layer | Function | Cu (finished) | Thickness |
|---|---|---|---|---|
| — | Solder mask (top) | LPI, selectively opened over pours | — | 0.015 |
| **L1** | **Top** | MOSFETs (motors 1–2), MCU, IMU, phase/VBAT pours | 1 oz base + plating ≈ **1.5–2 oz** | 0.050 |
| | Prepreg | 1080 + 2116 (high resin, thermal grade) | — | 0.120 |
| **L2** | **GND plane** | Solid reference and heat spreader. *No splits.* | **2 oz** | 0.070 |
| | Core | Thermal-enhanced FR-4 | — | 0.150 |
| **L3** | **VBAT plane** | Battery positive, feeds all high-side drains | **2 oz** | 0.070 |
| | Prepreg (center) | 2 × 2116 | — | 0.250 |
| **L4** | **GND plane #2** | Return current, heat spreading, shields L5 | **2 oz** | 0.070 |
| | Core | Thermal-enhanced FR-4 | — | 0.150 |
| **L5** | **Signal / GND flood** | SPI (IMU), UART, DShot/PWM, telemetry, current sense; GND-flooded elsewhere | 1 oz | 0.035 |
| | Prepreg | 1080 + 2116 | — | 0.120 |
| **L6** | **Bottom** | MOSFETs (motors 3–4), gate drive, BEC, phase pours | ≈ **1.5–2 oz** | 0.050 |
| — | Solder mask (bottom) | | — | 0.015 |
| | | | **Total** | **≈ 1.21 mm** |

### Why this arrangement

1. **Thin dielectric between the outer layers and the planes (0.12 mm).** Most heat leaves a DFN through its exposed pad into L1/L6, then crosses the first dielectric into a solid plane. Dielectric conductivity (~0.3–1 W/m·K) is 400–1000× worse than copper, so this gap dominates the vertical thermal path. Keeping it thin is the single biggest lever.
2. **Three 2 oz inner planes give ~210 µm of internal copper.** These act as the lateral heatsink, spreading heat from the hot MOSFET corners into the cooler center and the MCU region.
3. **L2/L3 form a tight VBAT–GND pair.** This gives distributed decoupling capacitance and a low-inductance commutation loop, which reduces switching ringing. Lower ringing means lower switching loss, and therefore less heat at the source.
4. **The L4 GND plane shields L5 signals** from the L3 VBAT and from the L6 phase nodes.
5. **The stack is symmetric** (outer-to-plane spacing and copper weights are mirrored), so the board won't warp through reflow. Warp matters with DFN packages on both sides.

### Material

- **Baseline:** High-Tg (≥170 °C) FR-4, e.g. Isola 370HR or Shengyi S1000-2. MOSFET junctions run hot, and the board around them can exceed 100 °C.
- **Thermal upgrade:** Thermally conductive prepreg/core in the 1.0–2.2 W/m·K range (offered by Ventec, Shengyi and others). This roughly halves to quarters the vertical resistance through the dielectric. Ask your fab what they stock; availability varies.
- **Resin fill:** 2 oz inner copper needs resin-rich prepreg (1080/2116 combos) to fill between features. Confirm with the fab. Avoid 7628 against heavy copper.

---

## 3. Thermal Via Strategy

The exposed drain/source pads need a direct thermal path to the planes.

| Feature | Spec |
|---|---|
| Drill / pad | 0.25–0.30 mm drill, 0.50–0.55 mm pad |
| Pitch | 0.7–0.8 mm grid under each exposed pad |
| Count | 6–12 per DFN3×3 exposed pad |
| Plating | ≥25 µm barrel |
| Treatment | **VIPPO** (via-in-pad, resin-filled and copper-capped, plated over) |

**Why VIPPO:** Open vias in a DFN pad wick solder away from the joint. That causes voids and raises thermal resistance. Tenting from the other side traps flux and gas. VIPPO gives a flat, void-free pad.

**Rough numbers per via (1.2 mm long, 0.3 mm drill, 25 µm plating):**
- Copper cross-section ≈ π × 0.3 × 0.025 ≈ 0.024 mm²
- R_th ≈ 1.2 / (0.385 W/mm·K × 0.024) ≈ **~130 K/W per via**
- 9 vias in parallel 