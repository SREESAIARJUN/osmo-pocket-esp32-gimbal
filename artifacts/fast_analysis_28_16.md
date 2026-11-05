# 6-Layer Stackup: 25.5 × 25.5 mm AIO FC with 4-in-1 ESC (24 MOSFETs)

I don't have context for "Cycle 28," so I've treated it as your design-iteration label. This design assumes:

* **Mounting:** 25.5 mm whoop pattern, ~30–32 mm outline, ~1.2 mm finished thickness.
* **Power stage:** 4 ESC channels × 3 half-bridges × 2 FETs, in 3×3 or 2×2 DFN packages.

---

## 1. Design Strategy

1. **Thick inner copper does the heavy lifting.** All four inner layers are 2 oz. Outer layers stay 1 oz base so fine-pitch MCU, gyro and gate-driver footprints (0.4–0.5 mm) still etch reliably.
2. **Thermal vias carry heat out of the FETs.** FR-4 conducts heat poorly (~0.3 W/m·K), so heat must leave each FET pad through a dense via array into the inner planes.
3. **The FETs sit on the bottom (L6) with a thin prepreg to L5.** This puts the FETs in prop-wash airflow and keeps their heat away from the gyro on top.
4. **The copper is symmetric (1/2/2/2/2/1 oz).** This prevents bow and twist, which matters for gyro mounting and soft-mount grommets.

---

## 2. Stackup Table

| # | Layer | Cu (base → finished) | Function | Dielectric below |
|---|---|---|---|---|
| L1 | **Top – Signal** | 1 oz → ~1.4 oz (plated) | MCU, gyro, OSD, baro, BEC, USB, connectors | PP 0.09 mm (1080/2116) |
| L2 | **GND (signal ref)** | 2 oz | Solid, unbroken reference for L1 (SPI, USB, video) | Core 0.15 mm |
| L3 | **VBAT plane** | 2 oz | Main battery bus to all 12 high-side drains | PP 0.15 mm |
| L4 | **PGND plane** | 2 oz | Power return, central heat spreader | Core 0.15 mm |
| L5 | **VBAT / Phase pours** | 2 oz | Duplicate VBAT plus per-phase pours under FETs | PP 0.09 mm (thin) |
| L6 | **Bottom – Power** | 1 oz → ~1.4 oz (plated) | 24 FETs, gate drivers / ESC MCUs, current shunt | — |

**Thickness budget:**

| Element | Thickness |
|---|---|
| Outer Cu (2 × ~0.05 mm) | 0.10 mm |
| Inner Cu (4 × 0.07 mm) | 0.28 mm |
| Dielectrics (0.09 + 0.15 + 0.15 + 0.15 + 0.09) | 0.63 mm |
| Mask and finish | ~0.04 mm |
| **Total** | **≈1.05 mm** |

* For 1.2 mm nominal, increase the central PP (L3–L4) to ~0.30 mm. That layer pair is only VBAT and PGND, so the extra spacing costs nothing electrically.
* Keep L3/L4 tight (0.15 mm) if you want more plane capacitance for ripple suppression. Then let the board finish at ~1.0–1.05 mm instead of 1.2 mm.

**Material:**

* Use high-Tg FR-4 (Tg ≥ 170 °C, e.g. S1000-2 or IT-180A). FETs on a 1S–4S whoop can push the local board temperature past 100 °C during punch-outs.
* Optionally use a high-thermal-conductivity prepreg (~1.0–1.5 W/m·K) for the L5–L6 layer, if your fab offers it.

---

## 3. Thermal Path Analysis (per FET)

**Direct conduction through the thin L5–L6 prepreg** (2 × 2 mm drain pad, 0.09 mm FR-4):

R = t / (k·A) = 0.09e-3 / (0.3 × 4e-6) ≈ **75 K/W**. This path is negligible, so the vias are mandatory.

**Single thermal via** (0.3 mm drill, 25 µm plating):

* Copper cross-section ≈ 0.0216 mm²
* L6→L5 (≈0.14 mm): ~17 K/W
* Full board (1.05 mm): ~125 K/W

**Recommended via array** (6–9 vias per drain pad, via-in-pad):

* L6→L5: **~2–3 K/W**
* L6→L3/L4 planes: ~5–8 K/W
* All vias also tie into L4/L3, which spreads heat across the full ~9 cm² of board.

**Budget example:**

* Assume ~0.25 W per FET during a burst, so ~6 W total.
* With 4 × 2 oz inner planes as spreaders plus airflow, expect roughly a **25–40 °C board rise**.
* A 2 × 1 oz inner design would give roughly 45–65 °C.

---

## 4. Via Specification

| Item | Spec |
|---|---|
| Thermal vias (FET pads) | 0.25–0.30 mm drill, **filled and capped (POFV / VIPPO)** so solder doesn't wick away |
| Pitch | 0.6–0.7 mm grid under drain/source pads |
| Power stitching | 0.3 mm vias every ~1.5 mm along VBAT/PGND/phase pours, connecting L1–L6 |
| Thermal relief | **None** on power and thermal vias (solid connect) |
| Signal vias | 0.2 mm drill / 0.4 mm pad |
| Type | Through-hole only; avoid blind/buried for cost and yield |

---

## 5. Lay