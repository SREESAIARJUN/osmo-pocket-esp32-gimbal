# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

> **Note on "Cycle 9":** I don't have the earlier iterations, so this design is built from stated assumptions. If you share the exact part number, stackup, and previous results, I can tune it to your board.

---

## 1. Assumptions (verify against your datasheet)

| Parameter | Assumed value |
|---|---|
| Package | QFN-20, 5 × 5 mm body, exposed pad (EP) ≈ 3.2 × 3.2 mm (drain) |
| R<sub>DS(on)</sub> | 2.0 mΩ @ 25 °C → ~3.0 mΩ @ T<sub>J</sub> ≈ 110 °C |
| Board | 4-layer FR-4, 1.6 mm, 2 oz outer / 1 oz inner, 25 µm via plating |
| Via | 0.30 mm finished drill, **0.60 mm pad diameter** (0.15 mm annular ring width) |
| Duty | Continuous conduction (conduction loss dominant) |

**About "0.6 mm annular ring":** I've interpreted this as a 0.6 mm pad diameter, which is standard for a 0.3 mm drill. If you meant a 0.6 mm ring *width* (1.5 mm pad), that won't fit a useful array under a 3.2 mm EP, so please confirm.

---

## 2. Power Dissipation Budget

P<sub>cond</sub> = I² · R<sub>DS(on),hot</sub> = 20² × 0.003 = **1.2 W per FET**

If the FET also switches (for example, the high side of a buck), add switching loss. Budget about **1.5–2 W** for margin.

---

## 3. Single-Via Thermal Resistance

The plated barrel cross-section is:

A = π(r<sub>o</sub>² − r<sub>i</sub>²) = π(0.150² − 0.125²) = 0.0216 mm²

| Via type | R<sub>θ</sub> per via (1.6 mm board) |
|---|---|
| Unfilled, 25 µm plating | **≈ 190 K/W** |
| Non-conductive epoxy fill (IPC-4761 Type VII) | ≈ 175 K/W (thermal benefit is small; the main gain is manufacturability) |
| Copper filled | ≈ 60 K/W |

---

## 4. Array Layout Under the Exposed Pad

**Recommended: 3 × 3 grid, 1.0 mm pitch, centered on the EP**

```
        EP 3.2 x 3.2 mm (drain)
   ┌───────────────────────────┐
   │                           │
   │    ●        ●        ●    │   ● = 0.3 mm drill / 0.6 mm pad
   │                           │   pitch = 1.0 mm (X and Y)
   │    ●        ●        ●    │   edge margin to EP edge ≈ 0.3 mm
   │                           │
   │    ●        ●        ●    │
   │                           │
   └───────────────────────────┘
```

**Via coordinates (relative to EP center):**

| Row | X = −1.0 | X = 0 | X = +1.0 |
|---|---|---|---|
| Y = +1.0 | V1 | V2 | V3 |
| Y = 0 | V4 | V5 | V6 |
| Y = −1.0 | V7 | V8 | V9 |

**Thermal result (9 vias in parallel):**
- Unfilled: 190 / 9 ≈ **21 K/W**
- Copper filled: 60 / 9 ≈ **6.7 K/W**

**Denser option: 4 × 4 at 0.8 mm pitch.** This leaves only a 0.2 mm copper web between pads and gives ≈ 12 K/W unfilled. Use it only with filled-and-capped vias and fab confirmation.

---

## 5. Current Capacity

The thermal array alone is not enough for current. If the 20 A drain current transitions to inner or bottom layers through vias:

- A 0.3 mm plated via carries roughly **1–1.5 A** at a 10 °C rise (IPC-2152 basis).
- 20 A therefore needs **≥ 16–20 vias** in the current path.

**Action:** Add a **"current via field" of 12–16 additional vias** in the drain copper pour just outside the package outline, on the side facing the load or inductor. Use the same 0.3/0.6 via at 1.0 mm pitch. These vias also extend the thermal spreading area.

Similarly, the source pins need their own via clusters next to the pins. They cannot use the EP vias.

---

## 6. Thermal Stack Estimate

| Path element | R<sub>θ</sub> (K/W) |
|---|---|
| Junction → case (EP), typical | 1.0–1.5 |
| Solder + EP (with ~60% coverage) | ~0.5 |
| Via array (9, unfilled) ‖ external via field | ~10–12 |
| Spreading in 2 oz planes → ambient (~25 × 25 mm Cu per FET) | ~20–25 |
| **Total junction → ambient** | **~32–38 K/W** |

**Temperature rise:** ΔT ≈ 1.2 W × 35 K/W ≈ **42 °C**, giving T<sub>J</sub> ≈ 92 °C at 50 °C ambient. This is within spec with margin, and convergent with the 3 mΩ hot R<sub>DS(on)</sub> assumption.

At a 2 W budget, T<sub>J</sub> ≈ 120 °C. In that case, go to copper-filled vias or add a bottom-side heatsink or thermal pad.

---

## 7. Manufacturing Rules

1. **Via-in-pad treatment (preferred):** Use IPC-4761 Type VII, epoxy filled and copper capped (planarized). This eliminates solder wicking and voids.
2. **If unfilled vias are required:**
   - Tent the vias from the bottom side only, with solder mask on the bottom. Never tent the top side, since that traps outgassing and causes voids.
   - Expect 10–20% solder loss into the barrels.
3. **Stencil:**
   - Use a windowpane aperture with **50–60% paste coverage** of the EP.
   - Use a 2 × 2 or 3 × 3 pane grid whose webs run *over* the via rows, so paste avoids the vias.
4. **Thermal relief:** Do **not** use thermal-relief spokes on these vias. Connect them solid to all drain planes.
5. **Inner layers:** Make the drain net a solid pour on every layer the vias touch. The bottom layer should be an exposed copper area of ≥ 25 × 25 mm if possible.
6. **Multiple FETs (for example, a half-bridge):** Keep ≥ 3 mm between EP via arrays, so the thermal spreading zones don't overlap.

---

## 8. Verification Checklist for This Cycle

- [ ] Confirm EP dimensions and recommended land pattern from the datasheet
- [ ] Confirm the ring-diameter vs. ring-width interpretation
- [ ] Fab sign-off on 0.2 mm web (only if using the 0.8 mm pitch option)
- [ ] Thermal simulation or IR measurement at 20 A: target T<sub>case</sub> < 100 °C
- [ ] X-ray voiding check on the first article: target < 25% EP voiding

If you send the MOSFET part number and the Cycle 8 results (measured temperatures or failure mode), I can adjust via count, pitch, and fill type.