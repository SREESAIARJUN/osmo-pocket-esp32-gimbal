# Thermal Via Array: QFN-20 Power MOSFET, 20 A Continuous (Cycle 17)

## 1. Design Basis and Assumptions

I don't have the outputs from Cycles 1–16, so the parameters below are assumed. Replace any of them with your locked values and the calculations will scale directly.

| Parameter | Assumed value | Notes |
|---|---|---|
| Package | QFN-20, 5.0 × 5.0 mm, 0.65 mm pitch | Single drain exposed pad (EP) |
| Exposed pad (drain) | 3.40 × 3.40 mm | Check the vendor land pattern |
| R_DS(on) | 3.0 mΩ at 25 °C, 4.5 mΩ at T_J = 125 °C | |
| Conduction loss | 20² × 4.5 mΩ = **1.8 W** | |
| Switching and margin | +0.7 W | |
| **Design dissipation** | **2.5 W per FET** | |
| Board stack-up | 1.6 mm, 4 layers, 2 oz outer and 1 oz inner | |
| Via plating | 25 µm (IPC Class 2) minimum | |
| Via geometry | 0.30 mm finished drill, **0.60 mm pad diameter** (0.15 mm ring) | I read "0.6 mm annular ring" as the pad outer diameter. A 0.6 mm ring *width* would give a 1.5 mm pad, which won't fit an array under a 3.4 mm EP. |
| Thermal target | T_J ≤ 110 °C at T_A = 60 °C, so ΔT ≤ 50 °C | |

## 2. Per-Via Thermal Resistance (1.6 mm barrel)

The formula is R = L / (k · A), with copper k = 385 W/m·K.

| Via type | Copper cross-section | R per via |
|---|---|---|
| Open, 25 µm plating | π/4 × (0.30² − 0.25²) = 0.0216 mm² | **~192 K/W** |
| Open, 35 µm plating | 0.0291 mm² | ~143 K/W |
| Copper-filled (VIPPO) | π/4 × 0.30² = 0.0707 mm² | **~59 K/W** |

## 3. Via Count and Pitch

The pitch is set by the pad size plus the copper web between pads. A 0.60 mm pad on a **0.90 mm pitch** leaves a 0.30 mm web, which is manufacturable for standard fabs.

The EP fit check for a 4 × 4 array is 3 × 0.90 + 0.60 = 3.30 mm, which is less than 3.40 mm. That leaves 0.05 mm per side, which is tight but acceptable because the vias sit inside the EP copper.

| Configuration | Vias | R through board | ΔT at 2.5 W |
|---|---|---|---|
| 4×4 open, under EP only | 16 | 12.0 K/W | 30 °C ❌ too much of the budget |
| 4×4 VIPPO under EP | 16 | 3.7 K/W | 9 °C ✅ |
| **4×4 VIPPO + 14 open in drain-pour skirt** | 30 | ~3.0 K/W | **~7.5 °C ✅ recommended** |

The rest of the stack also uses part of the budget. R_θJC ≈ 1.5 K/W and the solder layer adds about 0.3 K/W. The bottom-side spreading into ambient through a 25 × 25 mm 2 oz pour plus inner planes is about 12–15 K/W. That gives T_J ≈ 60 + 2.5 × (1.8 + 3.0 + 13) ≈ **104 °C**, which meets the target.

## 4. Layout (top view, units in mm, origin at EP centre)

```
          ┌──────── QFN-20 body 5.0 × 5.0 ────────┐
          │  ▫ ▫ ▫ ▫ ▫   (signal/source pins)      │
          │ ┌──────── EP 3.40 × 3.40 ─────────┐    │
   ○  ○   │ │   ●       ●       ●       ●     │    │   ○  ○
          │ │ (-1.35, 1.35)       (1.35, 1.35) │    │
   ○  ○   │ │   ●       ●       ●       ●     │    │   ○  ○
          │ │                                  │    │
   ○  ○   │ │   ●       ●       ●       ●     │    │   ○  ○
          │ │                                  │    │
          │ │   ●       ●       ●       ●     │    │
          │ └──────────────────────────────────┘    │
          └─────────────────────────────────────────┘
      ○ ○ ○ ○ ○ ○ ○ ○   ← skirt vias in drain pour (off-pad side)
   ● = VIPPO 0.30/0.60, pitch 0.90    ○ = open/tented 0.30/0.60, pitch 1.0
```

**Under-pad via coordinates (4 × 4 grid):** X and Y each take the values −1.35, −0.45, +0.45 and +1.35.

**Skirt vias:** Place 14 vias on a 1.0 mm pitch, starting ≥ 0.5 mm outside the package body. Put them on the drain side, the side away from the gate and source pins. Use two rows on each of the two drain-side flanks. Keep them as close to the EP as the design rules allow, because thermal benefit falls off sharply beyond about 3 mm.

## 5. Fabrication and Assembly Notes

**Via treatment under the EP**
- Specify IPC-4761 **Type VII**: filled with non-conductive or copper paste, then planarized and capped (VIPPO).
- Open vias under the EP wick solder, which causes voids and starves the joint. If VIPPO is cost-prohibitive, the fallback is Type III (plugged from the bottom side). In that case add about 8 more skirt vias and accept roughly 25 °C ΔT across the vias.

**Plating and connection**
- Call out a minimum of 25 µm barrel plating on the fab drawing.
- Use a **direct connect** (no thermal relief) on every layer, including inner planes.

**Bottom side**
- Place an exposed copper pad matching the EP, with the solder mask opened, for a heatsink or gap pad.
- Expose the skirt vias too, or tent them if the bottom will be wave-soldered.

**Paste stencil (0.125 mm foil)**
- Use a 2 × 2 windowpane of 1.30 × 1.30 mm apertures with a 0.40 mm web, centred on the EP.
- This gives about 58% coverage, which falls within the recommended 50–70% range.
- Pasting over the vias is acceptable only with VIPPO. With plugged vias, shift the apertures so they land between via rows.

## 6. Current Capacity Check

The vias carry drain current as well as heat. A 0.30 mm via with 25 µm plating carries about 1.2 A for a 10 °C rise under IPC-2152. With 30 vias the capacity is about 36 A, which is more than 1.8× the 20 A requirement. ✅

## 7. Verification for This Cycle

1. Run a thermal simulation such as FloTHERM or Ansys Icepak, or use a Saturn PCB via-array estimate, with the real stack-up and the neighbouring FET's heat. Two FETs in a half-bridge closer than 8 mm will couple.
2. On the first-article build, run an IR scan at 20 A DC. The target case temperature rise is ≤ 40 °C.
3. X-ray the EP solder joint. The accept