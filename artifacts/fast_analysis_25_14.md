# Thermal Via Array Design: QFN-20 Power MOSFETs at 20 A Continuous

## 0. Assumptions and Open Items

I don't have your schematic, part number, or what "Cycle 25" refers to. If it's a design revision or a prior constraint set, send the deltas and I'll reconcile. Until then, the design rests on these stated assumptions:

| Parameter | Assumed value | Change if… |
|---|---|---|
| Package | 5 × 5 mm QFN-20, exposed drain pad 3.5 × 3.5 mm | Use the actual datasheet land pattern |
| R<sub>DS(on)</sub> | 3.0 mΩ @ 25 °C, ~4.5 mΩ @ T<sub>J</sub> = 110 °C | Scale the power figures linearly |
| Duty | 100 % conduction (worst case, DC) | For PWM, add switching loss and scale by duty |
| Board | 4-layer, 1.6 mm, 2 oz outer / 1 oz inner, FR-4 | |
| Plating | 25 µm (1 mil) barrel copper, IPC Class 2 | |
| Ambient | 50 °C, natural convection | |
| T<sub>J</sub> limit | 125 °C design target (150 °C absolute) | |

**Via geometry interpretation:** I read "0.3 mm drill, 0.6 mm annular ring" as a **0.6 mm finished pad diameter**, which gives a 0.15 mm ring width. If you meant a 0.6 mm ring *width* (1.5 mm pad), that pad won't fit a usable array on a 3.5 mm exposed pad. Please confirm.

---

## 1. Power Dissipation

P<sub>cond</sub> = I² × R<sub>DS(on),hot</sub> = 20² × 4.5 mΩ = **1.8 W per FET**

Allowable rise: 125 − 50 = **75 K**, so total R<sub>θJA</sub> must be ≤ **~41 K/W**.

Typical budget:

| Path segment | Target |
|---|---|
| R<sub>θJC</sub> (die to exposed pad, datasheet) | ~1.5 K/W |
| Solder layer | ~1 K/W |
| Via array (top to bottom) | **≤ 8 K/W** |
| Copper spreading plus board-to-ambient | ≤ 30 K/W (this dominates) |

---

## 2. Single-Via Thermal Resistance

Barrel annulus (drill 0.30 mm, plated ID 0.25 mm):

A = π/4 × (0.30² − 0.25²) = 0.0216 mm²

R<sub>via</sub> = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W per via** (unfilled)

| Fill option | R per via | Notes |
|---|---|---|
| Unfilled / tented | ~190 K/W | Solder wicking risk |
| Non-conductive epoxy plug + cap (IPC-4761 Type VII, VIPPO) | ~185 K/W | **Recommended**: flat pad, no voids |
| Conductive (Cu/Ag) fill + cap | ~60–90 K/W | About 2–3× better, higher cost |

---

## 3. Array Layout (per FET)

Pad 0.6 mm, pitch 0.95 mm, web 0.35 mm (well above the typical 0.15 mm fab minimum). Array span is 3 × 0.95 + 0.6 = 3.45 mm, which fits the 3.5 mm pad.

```
        Exposed drain pad 3.50 x 3.50 mm
   ┌─────────────────────────────────────┐
   │   ●        ●        ●        ●      │   ● = 0.30 drill / 0.60 pad
   │                                     │   pitch = 0.95 mm (X and Y)
   │   ●        ●        ●        ●      │   4 x 4 = 16 vias
   │                                     │   edge clearance ≈ 0.03 mm (pad)
   │   ●        ●        ●        ●      │   centered on EP
   │                                     │
   │   ●        ●        ●        ●      │
   └─────────────────────────────────────┘
```

**Array resistance, 16 vias in parallel:**

| Fill | R<sub>array</sub> | ΔT at 1.8 W |
|---|---|---|
| Epoxy-plugged VIPPO | 185 / 16 ≈ **11.6 K/W** | ~21 K |
| Conductive fill | ~4–6 K/W | ~9 K |

The epoxy-filled array exceeds the 8 K/W target. Two fixes are available:

- **Option A: conductive fill.** Meets the budget within the footprint.
- **Option B: add perimeter vias (recommended, cheaper).** Place 12–20 more vias of the same size in the drain copper immediately outside the package outline, within 1–2 mm of the body. Total count rises to roughly 30–36, giving **~5–6 K/W**. These vias don't need plugging if they sit outside the paste area.

```
      ○  ○  ○  ○  ○  ○        ○ = perimeter vias in drain pour
   ○ ┌──────────────────┐ ○     (drain-side pins only, avoid G/S pins)
   ○ │  ● ● ● ●         │ ○
   ○ │  ● ● ● ●  QFN-20 │ ○
     │  ● ● ● ●         │
     │  ● ● ● ●         │
     └──────────────────┘
       (gate/source pins side: no vias, keep loop clean)
```

---

## 4. Current Capacity Check

If the drain current transitions layers through these vias, each 0.3 mm / 25 µm via carries roughly 1.5–2 A for a 10 K rise (IPC-2152 estimate). That puts 16 in-pad vias at about 24–32 A, and with perimeter vias the margin exceeds 2×.

Even so, route the main 20 A on the **2 oz top layer** with wide pours. Treat the vias as primarily thermal, with paralleled current as a bonus.

---

## 5. Stack-Up and Copper Spreading

| Layer | Function |
|---|---|
| L1 (2 oz) | Drain pour, ≥ 6 cm² per FET, unbroken around the EP |
| L2 (1 oz) | Solid plane tied to drain net (thermal plus current), or GND if isolation is needed (via still conducts heat through the dielectric poorly, so tie to drain if possible) |
| L3 (1 oz) | Same as L2 |
| L4 (2 oz) | Drain pour mirrored, ≥ 6 cm²; heatsink/TIM landing zone if used |

- **Connect vias solid to all planes.** Do **not** use thermal-relief spokes on these vias.
- Each 2 oz layer of 6–10 cm² gives roughly 25–35 K/W board-to-ambient in natural convection. That is the dominant term. If T<sub>J</sub> margin is tight, add a bottom-side heatsink with a gap pad, which can bring it under 10 K/W.

---

## 6. Assembly / Paste Stencil (Exposed Pad)

- Use a **windowpane aperture**: 3 × 3 or 4 × 4 segmented openings at **50–60 % total coverage** of the EP.
- Place the paste windows **between** via positions. With VIPPO this matters less, but it still reduces voiding.
- Stencil thickness: 0.10–0.127 mm.
- Voiding target: ≤ 25 % per IPC-7093. Verify by X-ray on first articles.
- If you use unplugged vias (not recommended), tent from the bottom only and expect wicking. Never leave in-pad vias open.

---

## 7. Predicted Junction Temperature (Option B, 30+ vias)

| Segment | K/W | ΔT @ 1.8 W |
|---|---|---|
| R<sub>θJC</sub> | 1.5 | 2.7 |
| Solder |