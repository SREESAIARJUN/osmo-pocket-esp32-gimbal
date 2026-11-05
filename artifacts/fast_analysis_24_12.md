# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

> **Note on "Cycle 24":** I don't have context from previous design cycles. I've stated all assumptions below so you can swap in your actual values (part number, Rds(on), stackup).

---

## 1. Assumptions (verify against your datasheet)

| Parameter | Assumed value | Notes |
|---|---|---|
| Package | QFN-20, 5 × 5 mm | Common for power stages / dual-FET packages |
| Exposed pad (EP) | 3.4 × 3.4 mm | Usually the **drain** pad, so it carries current too |
| Rds(on) @ Tj = 125 °C | ~4.5 mΩ | Typical ~3 mΩ part, derated for temperature |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | |
| Via | 0.30 mm finished drill, 0.60 mm pad, 25 µm plating | See note below |
| T_ambient | 50 °C | |
| Tj max target | ≤ 125 °C (derated from 150 °C) | |

**Clarification on "0.6 mm annular ring":** I've interpreted this as a **0.6 mm pad diameter** (0.15 mm annular ring). A literal 0.6 mm *ring* gives a 1.5 mm pad, which allows only ~4 vias under a 3.4 mm EP and defeats the purpose. If you meant the literal value, tell me and I'll redo the layout.

---

## 2. Power Dissipation Budget

**Conduction loss:**
P_cond = I² × Rds(on) = 20² × 0.0045 = **1.8 W**

**Switching loss:** this depends on topology and frequency. As a placeholder, assume +0.5 to 1.0 W at a few hundred kHz.

**Design target: P_total ≈ 2.5 W per FET.**

---

## 3. Single-Via Thermal Resistance

The barrel cross-section, with 25 µm plating inside a 0.30 mm finished hole:

A = π(0.175² − 0.150²) = **0.0255 mm²**

R_via = L / (k·A) = 1.6×10⁻³ / (385 × 2.55×10⁻⁸) ≈ **163 K/W per via** (unfilled)

| Fill option | R per via | Comment |
|---|---|---|
| Unfilled / tented | ~163 K/W | Cheapest; solder wicking risk |
| Non-conductive epoxy, capped (IPC-4761 Type VII) | ~160 K/W | Fixes wicking, no thermal gain |
| Conductive (Ag) epoxy, capped | ~110–130 K/W | Modest gain |
| **Copper-filled, capped (VIPPO)** | **~43 K/W** | Best; higher cost |

---

## 4. Array Layout

**Pitch selection:** a 0.6 mm pad at 0.8 mm pitch leaves 0.2 mm copper web between pads. That is fabricable at standard drill registration and is the densest practical pitch.

**Array:** 4 × 4 = **16 vias**, 0.8 mm pitch, centered on the EP.
- Array span: 3 × 0.8 + 0.6 = 3.0 mm
- Margin to EP edge: 0.2 mm per side

### Via coordinates (origin = EP center, mm)

```
        X:  -1.2    -0.4    +0.4    +1.2
Y +1.2      ●       ●       ●       ●
Y +0.4      ●       ●       ●       ●
Y -0.4      ●       ●       ●       ●
Y -1.2      ●       ●       ●       ●

  ┌────────── EP 3.4 × 3.4 mm ──────────┐
  │  ●      ●      ●      ●             │
  │  ●      ●      ●      ●     pitch   │
  │  ●      ●      ●      ●     0.8 mm  │
  │  ●      ●      ●      ●             │
  └─────────────────────────────────────┘
```

### Array thermal resistance (16 in parallel, ~15% derating for spreading/crowding)

| Fill | R_array | ΔT at 2.5 W |
|---|---|---|
| Unfilled | ~12 K/W | ~30 °C |
| Cu-filled VIPPO | ~3.1 K/W | ~8 °C |

### Extension vias (strongly recommended)

Place a further **12–20 vias in the drain copper pour immediately outside the package outline**, at 1.0 mm pitch, within 2 mm of the body. They add spreading area and current capacity, and they need no fill because no solder lands on them. Effective array R drops to roughly **7–8 K/W** even with unfilled vias.

---

## 5. Junction Temperature Check (2.5 W)

| Path element | Unfilled + extensions | VIPPO 16 + extensions |
|---|---|---|
| RθJC (bottom) | 1.5 K/W | 1.5 K/W |
| Solder / TIM layer | 0.5 K/W | 0.5 K/W |
| Via array | 7.5 K/W | 2.5 K/W |
| Bottom Cu → ambient (≈ 6 cm² 2 oz pour, natural convection) | 22 K/W | 22 K/W |
| **Total RθJA** | **~31.5 K/W** | **~26.5 K/W** |
| **Tj at 50 °C ambient** | **~129 °C** ⚠️ | **~116 °C** ✅ |

**Takeaway:** the board-to-ambient term dominates, not the vias. To hold Tj ≤ 125 °C with unfilled vias, do one of:
- Increase the bottom pour to ≥ 10 cm² per FET.
- Add a heatsink or thermal pad to chassis on the bottom side (brings R_CA to ~5–8 K/W).
- Add ~200 LFM of airflow.

---

## 6. Current-Carrying Check (20 A)

- A 0.3 mm via carries roughly 1.5–2 A at a 10 °C rise (IPC-2152 estimate for barrel cross-section).
- 16 EP vias plus 12+ extension vias give ≥ 40 A of capacity, so **2× margin**.
- The **main current path should be planar**. Route 20 A through top-layer copper of ≥ 8 mm width at 2 oz, and use the vias to share current into an inner/bottom plane.
- **Connect vias solid to all planes. Do not use thermal reliefs** on these nets.

---

## 7. Assembly Rules

1. **Solder wicking:** with open 0.3 mm vias, solder will wick through and starve the joint.
   - **Preferred:** IPC-4761 Type VII (filled and capped, plated over).
   - **Budget option:** leave vias open and use a windowpane stencil whose apertures avoid the via locations.
   - **Avoid:** bottom-only tenting. It traps flux and air and causes voiding.
2. **Stencil:** windowpane pattern with 50–65% paste coverage of the EP, in a 2×2 or 3×3 grid of apertures. Use a 0.10–0.127 mm stencil.
3. **Void target:** < 25% EP voiding per IPC-7093. Confirm with X-ray on first articles.
4. **Solder mask:** use an NSMD/SMD hybrid. Mask-define the EP edge if the datasheet recommends it.