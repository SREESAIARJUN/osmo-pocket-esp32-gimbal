# Thermal Via Array Under QFN-20 MOSFETs (20 A Continuous)

## 0. Assumptions to Verify

I don't have context from your earlier cycles, so this design rests on the assumptions below. Check them against your part's datasheet and land pattern before committing.

| Parameter | Assumed value | Why it matters |
|---|---|---|
| Package | QFN-20, 4×4 mm body, 0.5 mm pitch | Sets the pad area available |
| Exposed pad (EP) | 3.0 × 3.0 mm, tied to **drain** | Sets via count and grid |
| R<sub>DS(on)</sub> | 2.0 mΩ @25 °C, ×1.5 at T<sub>j</sub> hot → 3.0 mΩ | Sets power dissipation |
| PCB | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner, 25 µm via plating | Sets via thermal resistance |
| Via geometry | 0.3 mm finished drill, **0.6 mm pad diameter** (0.15 mm ring width) | See note below |

**Annular-ring note:** I've read "0.6 mm annular ring" as a 0.6 mm pad diameter. If you literally meant a 0.6 mm ring width, the pad becomes 1.5 mm, and only 2×2 vias fit on the EP. Inside a solid EP the outer-layer ring is merged into the copper anyway. So the real pitch constraint is hole-to-hole spacing (typically ≥0.25 mm wall-to-wall, which means ≥0.55 mm pitch), not the ring.

---

## 1. Loss Budget

**Conduction loss:**
P<sub>cond</sub> = I² · R<sub>DS(on),hot</sub> = 20² × 0.003 = **1.2 W**

**Switching loss:** add your estimate. A synchronous buck at a few hundred kHz typically adds 0.3–0.8 W.

**Design target:** **≈2.0 W per FET**, which leaves margin.

---

## 2. Via Thermal Resistance

**Plated barrel cross-section:**
A = π/4 · (0.300² − 0.250²) = 0.0216 mm²

**Per-via resistance through a 1.6 mm board:**
R<sub>via</sub> = L / (k·A) = 1.6×10⁻³ / (385 × 0.0216×10⁻⁶) ≈ **192 K/W**

| Fill type | R per via | 9 vias (3×3) | **16 vias (4×4)** |
|---|---|---|---|
| Plated, unfilled | 192 K/W | 21 K/W | **12 K/W** |
| Non-conductive epoxy fill + cap (IPC-4761 Type VII) | ~185 K/W | 21 K/W | 11.6 K/W |
| Solid Cu fill | ~59 K/W | 6.6 K/W | 3.7 K/W |

At 2 W, a 4×4 plated array drops **about 24 °C** top-to-bottom, versus 42 °C for 3×3. Thermal resistance falls with diminishing returns past roughly 16 vias, because spreading resistance in the copper starts to dominate.

**Selected: 4×4 array, VIPPO (Type VII filled and capped).**

---

## 3. Current Capacity

If the drain current transfers to inner or bottom layers through the vias, the vias must also carry current, not just heat.

- IPC-2152 gives roughly 1.0–1.5 A per 0.3 mm via, 25 µm plating, at ΔT ≈ 10 °C.
- 20 A ÷ 1.2 A ≈ **17 vias minimum** for current sharing.
- The 16 EP vias are borderline on their own.
- **Add 8 more vias in the drain pour just outside the package outline**, for **24 total**. That gives about 0.83 A per via.

---

## 4. Layout: EP Via Grid (4×4, 0.75 mm pitch)

Coordinates are relative to the EP center.

```
        -1.125  -0.375  +0.375  +1.125   (X, mm)
+1.125    ●       ●       ●       ●
+0.375    ●       ●       ●       ●
-0.375    ●       ●       ●       ●
-1.125    ●       ●       ●       ●
(Y, mm)
```

- **Grid span:** 2.25 mm. The outer via pad edge sits at 1.425 mm, giving a 75 µm margin to the 1.5 mm EP edge.
- **Hole-to-hole web:** 0.75 − 0.30 = 0.45 mm. This is fine for standard fabs.
- **Inner-layer pad overlap:** adjacent 0.6 mm pads overlap. This is acceptable because all vias are the same net and merge into the drain plane on every layer.
- **If your EP is smaller than 2.8 mm:** use a 3×3 grid at 0.9 mm pitch and move the remaining vias to the exterior rows.

### Exterior Current/Thermal Vias

- 2 rows × 4 vias, 0.9 mm pitch.
- Place them in the drain copper on the **side opposite the gate/source pins**.
- Keep them ≥0.5 mm from the package outline so they don't interfere with paste or rework.

---

## 5. Stackup and Copper Connection

| Layer | Connection |
|---|---|
| L1 (top) | EP pad merged into a drain pour of ≥15×15 mm. Use **solid connection, no thermal reliefs**. |
| L2 | Drain-net plane or pour, solid connect. If L2 must be GND, the vias still conduct heat to L4, but the spreading benefit on L2 is lost. |
| L3 | Same as L2 where possible. |
| L4 (bottom) | Large drain copper island as the primary heatsink face. Optionally add a TIM pad to the chassis or heatsink, insulated if the drain is at switch-node potential. |

**Switch-node caution:** If this FET's drain is the switch node, large drain copper raises EMI and capacitive coupling. Keep the bottom island compact, or put the heat-spreading area on the low-side FET's source/GND side instead.

---

## 6. Solder Paste and Assembly

**With VIPPO (recommended):**
- Use a windowpane stencil: 4 apertures of about 1.3×1.3 mm, 0.2 mm webs, giving ~60–70% EP coverage.
- 0.12 mm stencil thickness.
- This avoids voiding and float. Target <25% voids per IPC-7093.

**Without filling (cost option):**
- Tent the vias from the bottom only, never the top.
- Keep paste apertures off the via locations.
- Expect solder wicking and higher voiding. In that case, add 4 more exterior vias to compensate.

**Fab notes:**
- Specify IPC-4761 Type VII, planarized copper cap over filled vias.
- Specify minimum 20 µm average barrel plating (IPC Class 2) or 25 µm (Class 3).

---

## 7. Expected Thermal Result

The rough thermal path at 2 W is:

| Segment | Thermal resistance | Temperature rise |
|---|---|---|
|