# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

I don't have the results from earlier iterations, so this is a self-contained design. **Replace the bracketed assumptions with your datasheet values.**

---

## 1. Design Assumptions (verify against your part)

| Parameter | Assumed value | Notes |
|---|---|---|
| Package | QFN-20, 5 × 5 mm | Common power-stage / dual-MOSFET outline |
| Exposed pad (EP, drain) | 3.6 × 3.0 mm | Check the land pattern drawing |
| R<sub>DS(on)</sub> @ 25 °C | 2.0 mΩ | |
| R<sub>DS(on)</sub> @ T<sub>J</sub> = 110 °C | ~3.0 mΩ | ×1.5 temperature factor |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | |
| Via | 0.30 mm finished drill, **0.60 mm pad Ø** | Annular ring = 0.15 mm/side |
| Plating | 25 µm (IPC Class 2/3 minimum average) | |

> **Interpretation note:** "0.6 mm annular ring" is read here as a **0.6 mm pad diameter**. If you meant a 0.6 mm ring width (1.5 mm pad), that is too large for in-pad use, because only about 4 vias would fit.

---

## 2. Power Dissipation Budget

- Conduction loss: P<sub>cond</sub> = I² · R<sub>DS(on),hot</sub> = 20² × 3.0 mΩ = **1.2 W**
- Switching, gate, and reverse-recovery losses at about 100–300 kHz: add roughly **0.3–0.8 W**
- **Design target: P<sub>total</sub> ≈ 2.0 W per FET**

---

## 3. Single-Via Thermal Resistance

Barrel copper cross-section:

A = π/4 × (0.30² − 0.25²) = **0.0216 mm²**

Thermal resistance:

R<sub>via</sub> = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W per via** (unfilled)

| Fill option | R<sub>via</sub> (approx.) |
|---|---|
| Unfilled / tented | ~190 K/W |
| Non-conductive epoxy plug + cap (VIPPO) | ~180 K/W |
| Conductive (Ag) epoxy, k ≈ 5–8 W/m·K | ~170 K/W |
| Solid copper fill | ~30 K/W |

---

## 4. Via Count

**Thermal requirement.** The goal is ≤ 10 °C drop across the board thickness at 2 W, so R<sub>array</sub> ≤ 5 K/W.

N ≥ 192 / 5 ≈ **38 vias** (unfilled). That number cannot fit inside a 3.6 × 3.0 mm pad, so the array is split between in-pad and perimeter vias (Section 5).

**Electrical requirement.** This applies only if the drain current transfers between layers through these vias. A 0.3 mm via with 25 µm plating carries about 1.5–2 A for a 10 °C rise (IPC-2152 estimate):

20 A / 1.5 A ≈ **14 vias minimum**, so design for ≥ 20 for margin.

---

## 5. Layout

### In-pad array (under the exposed pad)
- **Pitch: 1.0 mm** grid. This leaves a 0.4 mm copper web between 0.6 mm pads, which stays manufacturable without fragmenting the plane.
- **Edge keep-in: ≥ 0.4 mm** from via pad edge to EP edge.
- 3.6 × 3.0 mm EP → **4 × 3 = 12 vias**

```
        EP: 3.6 mm (X) × 3.0 mm (Y), drain
   ┌─────────────────────────────────────┐
   │   ○        ○        ○        ○      │   ○ = 0.3 drill / 0.6 pad
   │                                     │   pitch = 1.0 mm X & Y
   │   ○        ○        ○        ○      │   paste windows (▒) sit
   │                                     │   between via rows
   │   ○        ○        ○        ○      │
   └─────────────────────────────────────┘
          12 vias in-pad
```

### Perimeter array (in the drain copper pour beside the package)
- Place **2 rows of vias at 1.0 mm pitch** along the drain-side pour, starting ≥ 0.5 mm from the package body edge to keep them clear of the solder fillet.
- Target **24–30 additional vias** within about 5 mm of the package. Effectiveness drops sharply beyond about 2× the package width.
- Add **4–6 vias near the source pin cluster** as well. Source pins carry the same 20 A and also conduct heat.

**Total per FET: about 36–42 vias → R<sub>array</sub> ≈ 4.5–5.3 K/W** (unfilled).

> **Alternative:** Specify **copper-filled VIPPO**. Then the 12 in-pad vias alone give about 2.5 K/W, and the perimeter vias become optional for thermal purposes. This is the best option if board cost allows.

---

## 6. Via Fill, Tenting, and Solder Paste

| Option | Recommendation |
|---|---|
| **Preferred** | IPC-4761 **Type VII** (filled + capped / VIPPO). Gives a flat pad, no solder wicking, and no voiding from vias. |
| Budget option | **Bottom-side tenting** (solder mask on the bottom only). Never tent the top in-pad, or you get outgassing voids. |
| Avoid | Open through-vias in-pad with no fill. Solder wicks away, starving the joint, with >50% voids likely. |

**Stencil for the EP:**
- Use a segmented paste window with **50–65% coverage**: a 2 × 2 or 3 × 2 window grid placed *between* via rows.
- Keep paste ≥ 0.15 mm from any unfilled via pad.
- Stencil thickness: 0.10–0.125 mm.
- Target **< 25% voiding**, verified by X-ray on first articles.

---

## 7. Copper and Stack-Up Rules

1. **Solid connections only.** No thermal relief spokes on any thermal via, on any layer.
2. **Bottom layer:** Use an exposed drain copper area of ≥ 400–600 mm² per FET, mirroring the top pour. Leave it soldermask-open if a heatsink or gap pad attaches there.
3. **Inner layers:** Connect the vias to an inner plane at drain potential if one exists. If the inner plane is GND or a different net, the vias still spread heat through the barrel, but clearance antipads reduce plane benefit.
4. **Top pour for 20 A:** 2 oz copper at ≥ 8–10 mm width for a ~10 °C rise. Use a polygon, not a trace.
5. **Multiple FETs:** Keep ≥ 8–10 mm between FET centers, or stagger them, so their heat-spreading zones don't overlap.

---

## 8. Expected Thermal Result (estimate)

| Path element | R<sub>θ</sub> (K/W) |
|---|---|
| Junction → case (bottom), R<sub>θJC</sub> | ~1.0 (datasheet) |
| Solder joint (with <25% voids) | ~0.3 |
| Via array (≈40 unfilled) | ~5 |
| Bottom copper → ambient (600 mm², still air) | ~25–30 |

Two cases:
- **No heatsink:** Top-side spreading runs in parallel, giving a total of about **20 K/W**. At 2 W, ΔT ≈ 40 °C, so T<sub>J</sub> ≈ 105 °C at 65 °C ambient. This is acceptable but has limited margin.
- **With a bottom-side heatsink and gap pad:** the vias become the dominant term, and T<sub>J</sub> drops to about 80 °C. Recommended if T<sub>A</sub> > 50 °C.

---

## 9. Checklist for This Cycle

- [ ] Confirm EP dimensions and R<sub>DS(on),max</sub> from the datasheet, then recompute N.
- [