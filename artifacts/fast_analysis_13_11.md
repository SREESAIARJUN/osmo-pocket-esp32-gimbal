# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

> **Note on "Cycle 13":** I don't have the prior cycles' data. The design below states every assumption explicitly. Swap in your actual MOSFET datasheet values (R_DS(on), exposed-pad dimensions) and the numbers rescale directly.

---

## 1. Assumptions

| Parameter | Value | Notes |
|---|---|---|
| Package | QFN-20, 5 × 6 mm body | Typical power QFN |
| Exposed pad (drain) | 4.2 × 3.6 mm | **Verify against the datasheet land pattern** |
| R_DS(on) @ T_j = 100 °C | 3.0 mΩ | Hot value, not 25 °C |
| Continuous current | 20 A | |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | |
| Via plating | 25 µm Cu (IPC Class 2/3) | |
| Drill | 0.30 mm finished | |
| Via pad | **0.60 mm diameter** (0.15 mm annular ring) | See note below |

**How I interpreted "0.6 mm annular ring":** I took it as a 0.6 mm pad diameter. That is the standard pairing with a 0.3 mm drill. A literal 0.6 mm ring width would give a 1.5 mm pad, and only about 4 of those fit under the exposed pad. That would defeat the purpose of the array.

---

## 2. Heat Load

- **Conduction loss:** P_cond = I² · R = 20² × 0.003 = **1.20 W**
- **Switching loss:** add an estimated 0.3 W.
- **Design load:** **P ≈ 1.5 W per FET.**
- **Through-via share:** assume about 70% of the heat leaves through the vias to the inner and bottom planes, so **≈ 1.05 W**.

---

## 3. Single-Via Thermal and Electrical Resistance

**Plated barrel cross-section:**

A = π/4 · (0.30² − 0.25²) = **0.0216 mm²**

**Thermal resistance per via** (k_Cu = 385 W/m·K, L = 1.6 mm):

R_th = L / (k·A) = 1.6e-3 / (385 × 2.16e-8) ≈ **192 K/W (unfilled)**

**Copper-filled (VIPPO) alternative:**

A = 0.0707 mm², giving R_th ≈ **59 K/W**

**Electrical resistance per via:**

R = ρL/A = 1.72e-8 × 1.6e-3 / 2.16e-8 ≈ **1.27 mΩ**

**Current capacity per via:**

- The barrel circumference is π × 0.3 = 0.94 mm. Treat it as a 0.94 mm wide, ~0.7 oz trace.
- Per IPC-2152, that carries **≈ 1.5–2 A** at a 10 °C rise.

---

## 4. Array Sizing

| Criterion | Requirement | Vias needed |
|---|---|---|
| Thermal: ΔT through board ≤ 10 °C at 1.05 W | R_th,array ≤ 9.5 K/W, so N ≥ 192 / 9.5 | **≥ 20 (unfilled)** or ≥ 7 (filled) |
| Current: 20 A at about 1.5 A/via, if drain current transitions layers here | N ≥ 13.3 | **≥ 14** |
| Fit under a 4.2 × 3.6 mm pad at 1.0 mm pitch | 4 × 4 grid max (with 0.3 mm edge clearance) | **16 max** |

**Conclusion:** About 16 vias fit under the pad, but the unfilled thermal target needs about 20. There are two ways to close the gap:

- **Option A (recommended for cost):** 16 vias under the pad, plus 8–10 vias in the drain copper pour just outside the pad perimeter. This gives **24–26 total**.
- **Option B (best performance):** 16 copper-filled, capped vias (VIPPO, IPC-4761 Type VII). This gives R_th,array ≈ 3.7 K/W.

---

## 5. Layout (Option A)

```
          Drain copper pour (top, 2 oz) — extend ≥ 3 mm beyond body
   ┌───────────────────────────────────────────────────────┐
   │   o        o        o        o        o   ← perimeter │
   │      ┌─────────────────────────────┐        vias      │
   │      │  ●      ●      ●      ●     │  (1.2 mm pitch,  │
   │   o  │                             │   0.8 mm from    │
   │      │  ●      ●      ●      ●     │   pad edge)      │
   │      │                             │  o               │
   │   o  │  ●      ●      ●      ●     │                  │
   │      │                             │                  │
   │      │  ●      ●      ●      ●     │  o               │
   │      └─────────────────────────────┘                  │
   │       Exposed pad 4.2 × 3.6 mm                        │
   │       ● = 4×4 grid, 1.0 mm pitch (X) × 0.9 mm (Y)     │
   └───────────────────────────────────────────────────────┘
   Gate/Source pins on opposite side — keep perimeter vias off that edge
```

### Grid Coordinates (origin at pad center)

- **X:** −1.50, −0.50, +0.50, +1.50 mm
- **Y:** −1.35, −0.45, +0.45, +1.35 mm
- **Clearance:** 0.30 mm from via pad edge to exposed-pad edge (Y edge: 1.80 − 1.35 − 0.30 = 0.15 mm, the tightest point)
- **Web between via pads:** 1.0 − 0.6 = 0.40 mm copper in X, and 0.30 mm in Y. Both are fine for standard fab.

### Array Performance (26 vias, unfilled)

| Metric | Value |
|---|---|
| R_th,array | 192 / 26 ≈ **7.4 K/W**, giving ΔT ≈ 7.8 °C at 1.05 W ✔ |
| R_elec,array | 1.27 / 26 ≈ **0.05 mΩ**, giving 20 mW loss at 20 A ✔ |
| Current per via | **0.77 A** ✔ |

---

## 6. Solder and Assembly Control

A 0.3 mm open via under a QFN will wick solder. This causes voids, standoff tilt, and opens. Choose one mitigation, in order of preference:

1. **VIPPO (filled + planarized + capped).** No stencil restriction. This is the best choice.
2. **Bottom-side tenting or plugging (IPC-4761 Type VI).** Lower cost. Some outgassing voids remain.
3. **Open vias.** Use **only** with the stencil fix below.

### Stencil (for options 2 and 3)

- **Pattern:** windowpane of 4 × 3 apertures, covering **50–60%** of the pad area.
- **Via keep-out:** apertures avoid vias by ≥ 0.15 mm.
- **Thickness:** 0.12–0.125 mm.
- **Void target:** < 25% (IPC-7093). Verify with X-ray on first articles.

---

## 7. Stack-up and Copper

| Layer | Function | Requirement |
|---|---|---|
| L1 (top) | Drain pour + pad | 2 oz, ≥ 150 mm² pour per FET |
| L2 | GND plane | Solid. Vias connect **only if the plane is the drain net**; otherwise use antipads of 0.3 mm clearance |
| L3 | Drain/VIN plane (heat spreader) | Solid connection (**no thermal relief spokes**) |
| L4 (bottom) | Drain pour, heatsink/TIM landing | 2 oz, exposed copper or a solder-mask opening for the TIM pad |

**No thermal reliefs on these vias.** Spokes would cut thermal conductance by roughly 3–5×.

---

## 8. DRC and Fab Note Summary

```
Via:  0.30 mm finished drill / 0.60 mm pad / 25 µm min plating
Array: 4×4 under EP, pitch 1.0 × 0.9 mm; + 8–10 perimeter vias @ 1.2 mm pitch
