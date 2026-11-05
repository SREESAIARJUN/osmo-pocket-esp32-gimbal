# Thermal Via Array: QFN-20 MOSFETs at 20 A Continuous (Cycle 3)

## 1. Assumptions

I don't have the Cycle 1–2 data, so these values are placeholders. Replace them with your datasheet and stackup numbers.

| Parameter | Assumed value |
|---|---|
| Package | QFN-20, 5 × 5 mm body, exposed pad (EP) 3.4 × 3.4 mm, EP = drain |
| R_DS(on) | 2.5 mΩ at 25 °C, about 3.5 mΩ at T_J = 100 °C |
| R_θJC (bottom) | about 1.0 K/W |
| Board | 4-layer, 1.6 mm FR-4, 2 oz outer layers, 1 oz inner layers (L2 = GND, L3 = power or thermal plane) |
| Via | 0.30 mm finished drill, **0.60 mm pad diameter** (0.15 mm annular ring), 25 µm barrel plating |
| Ambient / T_J limit | 50 °C / 125 °C (design target ≤ 110 °C) |

> **Note on "0.6 mm annular ring":** I interpreted this as a 0.6 mm *pad diameter*. A literal 0.6 mm *ring width* would give a 1.5 mm pad, which won't fit an array on a 3.4 mm EP. Please confirm.

## 2. Power Budget per MOSFET

- Conduction loss: P_cond = I² · R_DS(on),hot = 20² × 3.5 mΩ = **1.40 W**
- Switching loss plus reverse recovery: an estimate of about 0.4 W is needed until it's calculated from f_sw and the gate-charge curve.
- **Design total: P_D ≈ 1.8 W per FET** (2.0 W used below for margin)
- Allowed rise: 110 − 50 = 60 K, so the junction-to-ambient thermal resistance must satisfy **R_θJA ≤ 30 K/W**

## 3. Single-Via Thermal Resistance

The barrel cross-section, with outer radius 0.15 mm and inner radius 0.125 mm, is:

A = π(0.15² − 0.125²) = 0.0216 mm²

The resulting resistance through the board is:

R_via = L / (k · A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **190 K/W per via** (unfilled)

| Fill type | R per via | Comment |
|---|---|---|
| Unfilled / tented | ~190 K/W | Wicks solder unless tented |
| Epoxy-filled (non-conductive), capped | ~170 K/W | Negligible gain |
| **Cu-filled / VIPPO (IPC-4761 Type VII)** | **~45–60 K/W** | Recommended |

## 4. Array Geometry: 4 × 4 at 0.90 mm Pitch, Inside the EP

- Array extent: 3 × 0.90 + 0.60 = 3.30 mm, which fits the 3.4 mm EP with 0.05 mm clearance per side.
- Copper web between via pads: 0.90 − 0.60 = 0.30 mm, which meets the typical ≥ 0.2 mm fab minimum.
- Drill-to-drill spacing is 0.60 mm, within standard limits.

**Via center coordinates** (origin at EP center, in mm):

```
x, y ∈ {−1.35, −0.45, +0.45, +1.35}

  (−1.35,+1.35) (−0.45,+1.35) (+0.45,+1.35) (+1.35,+1.35)
  (−1.35,+0.45) (−0.45,+0.45) (+0.45,+0.45) (+1.35,+0.45)
  (−1.35,−0.45) (−0.45,−0.45) (+0.45,−0.45) (+1.35,−0.45)
  (−1.35,−1.35) (−0.45,−1.35) (+0.45,−1.35) (+1.35,−1.35)
```

**Supplementary "fence" vias** go in the drain copper pour, outside the package body:

- Place 2 rows of 0.3/0.6 mm vias at 1.0 mm pitch along the drain-side edge(s), starting about 0.5 mm from the body outline.
- Use about 10–12 vias per FET.
- These vias don't need filling.
- Purpose: they add parallel thermal and electrical paths without solder-wicking risk.

## 5. Thermal Stack Check (per FET, VIPPO)

| Element | R (K/W) |
|---|---|
| R_θJC | 1.0 |
| Solder layer (EP, ~60 % paste coverage) | 0.3 |
| 16 VIPPO vias in parallel (~50 / 16) | 3.1 |
| ‖ 12 unfilled fence vias (~190 / 12 ≈ 16 K/W, partial effectiveness) | combined ≈ 2.7 |
| Spreading in L2/L3/bottom planes to ambient (~25 × 25 mm Cu area per FET, natural convection) | ~22 |
| **Total R_θJA** | **≈ 26 K/W** |

- Temperature rise: ΔT = 2.0 W × 26 K/W = 52 K, giving T_J ≈ 102 °C, which meets the 110 °C target.
- With **unfilled** vias in the EP, the via stack rises to about 9 K/W, T_J reaches about 115 °C, and margin is lost. **Specify VIPPO, or add a bottom-side heatsink or thermal pad.**

## 6. Current Capacity (Drain Current Through Vias)

- A single 0.3 mm via carries about 1.0–1.5 A at a 10 °C rise (IPC-2152, 1.6 mm board).
- The 16 EP vias plus 12 fence vias give about 28–40 A aggregate, which is adequate for 20 A even if the drain is routed on an inner or bottom layer.
- Preferred approach: carry the main 20 A on the top-layer 2 oz pour (≥ 6 mm wide for about 10 °C rise). Let the vias share the current and handle heat spreading.

## 7. Layout Rules

1. **Solid connections only.** Use no thermal reliefs on EP vias or fence vias on any layer.
2. **Paste stencil:** use a 3 × 3 or 4 × 4 windowpane pattern with ~55–65 % EP coverage. Keep paste apertures off via pads unless they are VIPPO-capped.
3. **If VIPPO is not available:** tent the vias from the bottom side only (solder mask), and reduce the EP array to 3 × 3 at 1.0 mm pitch to limit voiding. Expect T_J to be about 10–15 °C higher.
4. **Inner planes:** connect L2 and L3 to the drain net under each FET if the topology allows. For a high-side FET, L3 = V_IN. For a low-side FET, the EP is the switch node, so keep that copper area minimal for EMI and rely on the top and bottom pours.
5. **Bottom side:** provide an exposed copper area of at least 15 × 15 mm under each FET, with mask opened, for an optional TIM or heatsink.
6. **FET spacing:** keep ≥ 8 mm center-to-center between FETs to avoid thermal coupling. If they are closer, derate R_θJA by about 15 %.
7. **Fab notes:** specify IPC-4761 Type VII (filled and capped) for the EP vias, 25 µm minimum barrel plating, and Class 2 or better.

## 8. Verification (Cycle 3 Exit Criteria)

- Run a thermal FEA (e.g., Ansys Icepak or Simcenter FloTHERM) using the actual stackup and copper maps.
- Run a DC IR-drop / current-density simulation, targeting < 30 A/mm² in vias and < 10 °C rise in the planes.
- Test the bench