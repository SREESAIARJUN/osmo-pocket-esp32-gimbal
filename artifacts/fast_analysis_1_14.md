# Thermal Via Array Design: QFN-20 Power MOSFETs, 20 A Continuous (Cycle 1)

## 0. Assumptions

These must be confirmed against the actual datasheet before Cycle 2.

| Parameter | Assumed value | Why it matters |
|---|---|---|
| Package | 5.0 × 5.0 mm QFN-20, 0.65 mm pitch | Sets exposed pad (EP) size |
| Exposed pad | 3.25 × 3.25 mm, connected to **Drain** | Sets via count and net assignment |
| R_DS(on) | 3.0 mΩ @ 25 °C, ×1.5 at T_j = 125 °C, so 4.5 mΩ | Sets power dissipation |
| R_θJC (bottom) | 1.2 K/W | Junction-to-pad path |
| PCB | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | Sets via length and plane spreading |
| Via plating | 25 µm Cu (IPC Class 2/3) | Sets barrel conductance |
| **Via spec interpretation** | 0.3 mm drill, **0.6 mm pad diameter** (0.15 mm annular ring) | A literal 0.6 mm *ring width* gives a 1.5 mm pad, which won't fit usefully in a 3.25 mm EP. Please confirm. |

## 1. Power Budget

- **Conduction loss:** P = I²·R = 20² × 4.5 mΩ = **1.80 W**
- **Switching loss allowance:** +0.4 W (placeholder; refine with f_sw and Q_g in Cycle 2)
- **Design dissipation per FET:** **P_D = 2.2 W**
- **Target:** T_j ≤ 125 °C at T_a = 60 °C, so ΔT budget = 65 K and **R_θJA ≤ 29.5 K/W**

## 2. Single-Via Thermal Resistance

Barrel cross-section, with OD = 0.30 mm and ID = 0.25 mm:
A = π/4 · (0.30² − 0.25²) = 0.0216 mm²

R_via = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²)

| Via type | R per via |
|---|---|
| Open / plugged (non-conductive fill) | **≈ 192 K/W** |
| Cu-filled (VIPPO) | **≈ 59 K/W** |

## 3. Array Layout

### Recommended: 4 × 4 grid, 0.80 mm pitch

```
        EP: 3.25 x 3.25 mm (Drain)
   +-------------------------------+
   |   o      o      o      o      |   o = 0.3 drill / 0.6 pad
   |                               |   pitch = 0.80 mm (X and Y)
   |   o      o      o      o      |   array span (pad edges) = 3.00 mm
   |                               |   edge clearance to EP = 0.125 mm
   |   o      o      o      o      |   pad-to-pad web = 0.20 mm
   |                               |   hole-to-hole = 0.50 mm
   |   o      o      o      o      |
   +-------------------------------+
```

**Via centre coordinates** (relative to EP centre, mm):
X, Y ∈ {−1.20, −0.40, +0.40, +1.20}, giving 16 vias.

**Fab check:** a 0.20 mm copper web and 0.50 mm hole-to-hole spacing are within standard capability (typical minimums are 0.10 mm and 0.25 mm). Flag this on the fab drawing anyway.

**Array resistance (16 in parallel):**

| Via type | R_array | ΔT @ 2.2 W |
|---|---|---|
| Open/plugged | 12.0 K/W | 26 K (too much of the 65 K budget) |
| **VIPPO Cu-filled** | **3.7 K/W** | **8 K** |

**Rejected alternative:** a 3 × 3 grid at 1.0 mm pitch has only 9 vias, giving 21 K/W unfilled. It fails the budget.

## 4. Current-Carrying Check (20 A through the drain)

If the drain current must transfer between layers through the EP vias:
- A 0.3 mm via with 25 µm plating carries roughly 1.0–1.5 A at a 10 °C rise (IPC-2152-type estimate).
- 16 vias give about 16–24 A. That is **marginal at 20 A** and stacks heating on top of the thermal load.

**Mitigation (Cycle 1 rule):**
1. Route the main 20 A path on the **top layer** (2 oz pour) wherever possible, so the EP vias are primarily thermal.
2. Add **fringe vias** in the drain pour just outside the package footprint: 2 rows × 6 vias, same 0.3/0.6 geometry, 1.0 mm pitch, ≥0.5 mm from the package outline. That brings the total to 28 vias, about 28–40 A capacity.
3. Fringe vias are also useful thermally. They spread heat without solder-wicking risk and can stay open.

## 5. Via Fill and Solder Strategy

| Option | Thermal | Voiding / wicking | Cost | Verdict |
|---|---|---|---|---|
| **VIPPO** (IPC-4761 Type VII, Cu or conductive fill, planarised and capped) | Best | None | +15–30 % | **Primary** |
| Non-conductive plug + cap (Type VII, epoxy) | 12 K/W | None | +10–20 % | Acceptable fallback |
| Bottom-side tented / plugged (Type II/III) | 12 K/W | Low to moderate | Low | Fallback 2 |
| Open through-vias | 12 K/W | **Solder wicking, voids, starved joint** | Lowest | **Rejected** |

**Stencil (EP aperture):**
- **VIPPO:** windowpane 4 × 4 panes, 55–65 % paste coverage, 0.125 mm (5 mil) stencil.
- **Non-VIPPO:** keep panes *between* vias, with no paste over via pads.
- **Void target:** ≤ 25 % total EP voiding (IPC-7093). Verify by X-ray.

## 6. Plane Connections and Spreading

- **Connect vias solidly to every layer**, with no thermal reliefs on the EP vias.
- **Net caveat:** the EP is **Drain**.
  - **High-side FET in a half-bridge:** the drain is V_IN. Large planes on L2/L3/L4 are fine, since V_IN is a quiet node.
  - **Low-side FET:** the drain is the **switch node**. Large copper here creates an EMI antenna. Limit the switch-node copper area and spread heat into an adjacent GND plane through the dielectric. Thin prepreg (≤ 0.1 mm) between L1 and L2 helps.
- **Bottom-layer copper:** at least 25 × 25 mm per FET on the same net, as a heatsink or TIM landing area. This is the dominant term in R_θJA.

## 7. Cycle 1 Thermal Stack-Up

| Segment | R (K/W) |