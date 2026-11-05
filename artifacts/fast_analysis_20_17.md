# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC with 4-in-1 ESC (24 MOSFETs)

I don't have the context from your earlier cycles, so this is a self-contained proposal. Tell me what changed in Cycles 1–19, such as the FET package, cell count, or current target, and I'll reconcile it.

## 1. Design Assumptions

| Parameter | Value |
|---|---|
| Board | 25.5 × 25.5 mm |
| Battery | 1–2S whoop/toothpick class |
| Current | ~8–12 A peak per motor, ~5 A continuous |
| MOSFETs | 24 single N-ch, DFN2020 (2 × 2 mm, exposed drain pad); 4 motors × 3 half-bridges × HS/LS |
| Placement | All FETs on top (L1). MCU, gyro, regulators and connectors on bottom (L6). |
| Target finished thickness | ≈ 1.1 mm (keeps via thermal resistance low; still rigid enough) |
| Material | High-Tg FR-4, Tg ≥ 170 °C (e.g., IT-180A or S1000-2M), for repeated thermal cycling near FETs |

## 2. Stackup

| Layer | Function | Cu (finished) | Dielectric below | Notes |
|---|---|---|---|---|
| **L1 Top** | FETs, phase nodes, VBAT/GND pours | **2 oz (70 µm)** | PP 2116, ~0.10 mm | Primary heat spreader; max pour area |
| **L2** | **Solid GND** | **2 oz** | Core 0.20 mm | Tight coupling to L1 = low thermal and electrical impedance; return path for gate drive |
| **L3** | **VBAT plane** | **2 oz** | PP 2×1080, ~0.15 mm | HS drain connection; second spreader; forms plane capacitance with L2 |
| **L4** | Signals: SPI, UART, gate drive, DShot | 1 oz (35 µm) | Core 0.20 mm | GND fill around routing, stitched every ~2 mm |
| **L5** | **Solid GND** | **2 oz** | PP 2116, ~0.10 mm | Reference for L6 signals; shields MCU and gyro from FET switching |
| **L6 Bottom** | MCU, gyro, regulators, USB, pads | 1 oz + plating | — | Thermal relief pours tied to GND where free |

**Thickness estimate:**
- Copper: 4 × 70 µm + 2 × 35 µm = 350 µm
- Dielectric: ~750 µm
- Total: ≈ **1.10–1.15 mm** with solder mask

**Fab notes:**
- Use 2116 or doubled 1080 prepreg next to 2 oz inner copper. A single 1080 sheet risks resin starvation and voids.
- Request ≥ 25 µm via barrel plating (IPC Class 3 plating).
- The stackup is symmetric (2/2/2/1/2/1 is close enough). If warpage concerns the fab, use 2 oz on L6 as well.

## 3. Thermal Via Strategy

**Under each FET drain pad:**
- 4–6 vias, 0.20 mm drill / 0.40 mm pad
- **Via-in-pad, copper-filled and capped (VIPPO)**. This is the single biggest thermal win.

**Per-via thermal resistance (L = 1.13 mm, k_Cu = 385 W/m·K):**

| Via type | Copper area | R_th per via |
|---|---|---|
| Plated only, 25 µm wall | ~1.57 × 10⁻⁸ m² | ≈ 187 K/W |
| Copper-filled | ~3.1 × 10⁻⁸ m² | ≈ 93 K/W |

**Six copper-filled vias per FET → ≈ 15 K/W** through the board, versus ≈ 31 K/W plated-only.

If VIPPO cost is prohibitive, use epoxy-filled and capped vias. Never leave open vias in the pads, because they wick solder and leave voids under the FETs.

**Plane connections:**
- **LS FET sources:** direct (no thermal relief) to L2 and L5 GND. These two 2 oz planes are the main lateral heat spreaders.
- **HS FET drains:** direct to L3 VBAT plane.
- **Phase nodes:**
  - Keep them on L1 only, sized for current but not oversized. Switching-node area radiates noise toward the gyro.
  - Get heat out of them through the adjacent FET pads instead.

**Stitching:**
- GND stitching vias (0.25 mm) on a ~1.5 mm grid in all free areas, tying L1/L2/L5/L6 copper.
- This turns the whole board into one isothermal mass.

## 4. Layout Rules That Support the Stackup

1. **Distribute the FETs** around the perimeter, one corner quadrant per motor. Don't cluster them; spread heat across the full 650 mm².
2. **Gyro on L6, at the board center or far from the hottest bridge.** Put an L4/L5 GND shield under it, and keep VBAT current loops out from under it.
3. **Bulk and decoupling caps:**
   - Put 0402/0603 MLCCs at each half-bridge, VBAT to GND, on L1, within 1 mm of the HS drain.
   - Minimizing loop area cuts ringing and switching loss, which means less heat.
4. **Exposed copper:**
   - Open the solder mask on non-critical GND/VBAT pours on L1 for direct convection in prop wash.
   - Finish with ENIG.
   - Add conformal coating afterwards if needed, kept thin over the FETs.
5. **Mounting holes:** plate them and tie them to GND through a small resistor/cap network if frame grounding is wanted. Otherwise isolate them. Don't rely on them as heat paths through rubber grommets.
6. **Battery pads:** bond directly to L1, L3 (VBAT) and L2/L5 (GND) with ≥ 12 vias each. The battery leads also act as heat sinks.

## 5. Controlled Impedance (Ask Your Fab to Confirm)

| Signal | Layer | Reference | Approx. geometry |
|---|---|---|---|
| USB 90 Ω differential | L6 | L5 (0.10 mm PP) | ~0.13 mm width / 0.13 mm gap |
| Gyro SPI, 50 Ω single-ended (non-critical, but keep short) | L6 | L5 | ~0.17 mm |

## 6. Expected Result

With ≈ 15 K/W through the board per FET plus four strongly coupled 2 oz planes:
- Steady-state FET case temperature at ~0.3 W dissipation per FET, in prop-wash airflow, should sit around **~25