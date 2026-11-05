# Thermal Via Array Under QFN-20 Power MOSFETs (20 A Continuous)

I don't have the Cycle 1–6 history, so this design uses the stated assumptions below. Replace them with your actual part, stackup and earlier results where they differ.

## 0. Assumptions and a Spec Clarification

| Parameter | Assumed value |
|---|---|
| Package | QFN-20, 5 × 5 mm body, exposed pad (EP) 3.6 × 3.6 mm (EP = drain) |
| R_DS(on) | 3 mΩ at 25 °C, about 4.5 mΩ at T_j = 100 °C |
| Board | 1.6 mm, 4-layer, 2 oz outer and 1 oz inner copper, 25 µm barrel plating |
| Target | T_j ≤ 110 °C at T_amb = 50 °C |

**The "0.6 mm annular ring" needs clarifying.** A literal 0.6 mm ring around a 0.3 mm drill gives a 1.5 mm pad, which cannot be arrayed under a 3.6 mm EP. I've interpreted it as a **0.6 mm via pad diameter**, which gives a 0.15 mm annular ring. That ring meets IPC Class 2 and is marginal for Class 3, so confirm with your fab.

## 1. Power Budget

- Conduction: P = I²·R = 20² × 4.5 mΩ = **1.8 W**
- Switching, gate charge and margin: about 0.7 W
- **Design point: 2.5 W per FET**

Allowed rise: 110 − 50 = 60 °C, so **θ_JA ≤ 24 °C/W total**.

## 2. Single-Via Thermal Resistance

The plated barrel is 0.30 mm OD and 0.25 mm ID (25 µm wall).

- Copper area: A = π/4 × (0.30² − 0.25²) = 0.0216 mm²
- θ_via = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **190 °C/W per via** (unfilled)

Filling the via changes this substantially:

| Fill option | Effect on θ_via |
|---|---|
| Non-conductive epoxy | Negligible |
| Conductive epoxy | About 140 °C/W |
| Copper-filled (VIPPO, IPC-4761 Type VII) | About 35–40 °C/W |

## 3. Array Layout Under the EP

**Primary array: 4 × 4 grid at 0.9 mm pitch.**

- Pad-to-pad gap = 0.9 − 0.6 = 0.3 mm. This is adequate for most fabs and preserves copper webbing between vias.
- Array span = 3 × 0.9 + 0.6 = 3.3 mm, inside the 3.6 mm EP.
- Edge margin = 0.15 mm per side, which keeps vias off the EP edge and avoids solder voiding at the perimeter.

Via centres, relative to the EP centre (mm):

```
X/Y:  -1.35   -0.45   +0.45   +1.35

   ●───────●───────●───────●     Y = +1.35
   │       │       │       │
   ●───────●───────●───────●     Y = +0.45
   │       │       │       │
   ●───────●───────●───────●     Y = -0.45
   │       │       │       │
   ●───────●───────●───────●     Y = -1.35
   |<----- 3.6 mm EP ----->|
```

Resulting via-field resistance (16 vias in parallel):

| Via type | θ_vias |
|---|---|
| Unfilled | 190 / 16 ≈ **11.9 °C/W** |
| Copper-filled | About **2.4 °C/W** |

## 4. Full Thermal Stack (2.5 W)

| Element | Unfilled (°C/W) | VIPPO (°C/W) |
|---|---|---|
| θ_JC (bottom) | 1.5 | 1.5 |
| Solder layer (≥ 50 % coverage) | 0.5 | 0.5 |
| 16-via field | 11.9 | 2.4 |
| Bottom/inner plane spreading + convection (≥ 600 mm² of 2 oz pour) | about 15 → 10* | 10 |
| **Total θ_JA** | **≈ 24–29** | **≈ 14.5** |
| **T_j at 50 °C ambient** | **110–122 °C (marginal)** | **≈ 86 °C ✔** |

\*Lateral spreading in the top-layer 2 oz drain pour runs in parallel with the via path and recovers some of the margin.

**Verdict:** the unfilled 16-via array alone is marginal. Close the gap with **either** of these options:

- **Option A (preferred, lowest cost): add a perimeter "via fence" in the drain pour.** Place 12–20 additional 0.3/0.6 mm vias at 1.0 mm pitch within 1.5 mm of the package edge. These are not under the solder paste, so they need no plugging. This brings the effective via resistance to about 6 °C/W, the total to about 19 °C/W, and **T_j ≈ 98 °C ✔**.
- **Option B: specify VIPPO** for the 16 under-pad vias. This adds roughly 15–25 % to PCB cost.

## 5. Current-Carrying Check

The EP is the drain, so these vias also carry current if the drain connects through inner layers.

- Each 0.3 mm via carries about 1.2 A for a 10 °C rise (IPC-2152 estimate).
- 16 vias give about 19 A; with the fence vias the total is well over 30 A, which is adequate for 20 A.
- If current enters only on the top layer, the vias are mostly thermal and this check has ample margin.

## 6. Assembly and Solder Wicking (Critical for Unfilled Vias)

- **Tenting:** tent from the bottom side only with an LPI soldermask dam of 0.7 mm diameter. Do not tent the top side under the EP, because the trapped gas causes voids.
- **Better: plug-and-tent** (IPC-4761 Type VI) to prevent paste from wicking down the barrels.
- **Stencil:** use a windowpane aperture of 3 × 3 or 4 × 4 panes, giving **50–60 % paste coverage**. Keep apertures off the via locations, with web lines over the via rows. Use a 0.10–0.125 mm stencil.
- **Target voiding:** < 25 % per IPC-7093. Verify by X-ray on first articles.

## 7. Layer Connections

- Connect all vias to the drain copper on **every layer with solid (no thermal relief) connections**. Thermal reliefs would defeat the purpose.
- Make the bottom-layer pour at least 25 × 25 mm per FET where possible. This pour dominates the final θ_SA.
- **EMI trade-off:** if this FET's drain is a **switch node** (e.g., the low-side FET of a half-bridge), large drain copper on multiple layers increases dv/dt coupling. In that case:
  - Keep the switch-node pour on the top and bottom layers only.
  - Put a solid GND plane on L2 for shielding.
  - Put the extra spreading copper on the FET that sits on a quiet net (the high-side FET drain is V_IN).

## 8. Summary Specification

| Item | Value |
|---|---|
| Drill / pad diameter | 0.30 mm / 0.60 mm (0.15 mm ring) |
| Under-EP array | 4 × 4, 0.9 mm pitch, 16 vias |
| Perimeter fence | 12–20 vias, 1.0 mm pitch, in drain pour |
| Fill | Plug + bottom tent (min); VIPPO optional |
| Plating | ≥ 25 µm bar