# Thermal Via Array Design: QFN-20 Power MOSFET, 20 A Continuous

## 0. Assumptions to Confirm

Please confirm these before release, since they drive the via count and pitch:

| Parameter | Assumed value | Why it matters |
|---|---|---|
| Package | 5×5 mm QFN-20, exposed pad (EP) ≈ 3.4×3.4 mm | Sets how many vias fit |
| EP function | Drain (typical for power QFN) | Vias carry both heat and current |
| R_DS(on) | 2.0 mΩ at 25 °C, ≈3.0 mΩ at T_J = 110 °C | Sets power dissipation |
| Board | 1.6 mm, 4-layer, 2 oz outer / 1 oz inner | Sets via length and plane spreading |
| Plating | 25 µm (1 mil) barrel, IPC Class 2 | Sets via thermal resistance |
| "0.6 mm annular ring" | Interpreted as **0.6 mm finished via pad diameter** (0.15 mm ring) | A literal 0.6 mm ring gives a 1.5 mm pad, which would only allow about 4 vias under the EP |

"Cycle 29" isn't defined in what you've shared. If it refers to a design iteration, test cycle, or internal requirement, tell me the constraint and I'll fold it in.

---

## 1. Power Budget

- P_cond = I² × R_DS(on),hot = 20² × 3.0 mΩ = **1.2 W**
- P_sw: add from your switching analysis. A typical allowance at 100–300 kHz is 0.3–1.0 W.
- **Design target: P_total ≈ 2.0 W per FET**

---

## 2. Via Array Geometry (Under the Exposed Pad)

**Grid: 4 × 4 = 16 vias at 0.9 mm pitch**

```
          EP = 3.40 mm
   ┌─────────────────────────┐
   │  ○     ○     ○     ○    │   ○ = 0.30 mm drill / 0.60 mm pad
   │                         │   Pitch      = 0.90 mm (X and Y)
   │  ○     ○     ○     ○    │   Array span = 3×0.9 + 0.6 = 3.30 mm
   │                         │   Pad-to-pad gap = 0.30 mm
   │  ○     ○     ○     ○    │   Pad-to-EP-edge = 0.05 mm
   │                         │
   │  ○     ○     ○     ○    │
   └─────────────────────────┘
```

- If the 0.05 mm edge margin is too tight for your solder mask registration, use a **4×4 grid at 0.85 mm pitch** (array span 3.15 mm, 0.125 mm margin). This is still a 0.25 mm gap, which standard fabs handle.
- The fallback is a 3×3 grid at 1.1 mm pitch (9 vias). This roughly doubles via thermal resistance. Avoid it unless the fab requires it.

---

## 3. Thermal Resistance Calculation

**Per-via conduction (open, unfilled):**

- Copper barrel area: A = π/4 × (0.30² − 0.25²) = 0.0216 mm²
- R_via = L / (k·A) = 1.6 mm / (385 W/m·K × 0.0216 mm²) ≈ **192 K/W**

**Array totals by fill option:**

| Fill option | R per via | R for 16 vias | ΔT at 2 W |
|---|---|---|---|
| Open / tented | 192 K/W | **12.0 K/W** | 24 °C |
| Non-conductive epoxy fill + cap (IPC-4761 Type VII) | ~185 K/W | 11.6 K/W | 23 °C |
| Conductive (Ag) epoxy fill + cap | ~120 K/W | 7.5 K/W | 15 °C |
| Solid copper fill (VIPPO) | ~59 K/W | **3.7 K/W** | 7 °C |

**Full stack estimate (open vias):**

| Stage | Thermal resistance |
|---|---|
| R_θJC (datasheet, typical) | ~1.5 K/W |
| Solder layer | ~0.5 K/W |
| Via array | 12 K/W |
| Bottom-plane spreading to ambient (≥ 25×25 mm, 2 oz copper) | ~20–25 K/W |
| **R_θJA** | **≈ 35–40 K/W** |

At 2 W this gives ΔT ≈ 75 °C. With T_A = 50 °C, T_J ≈ 125 °C, which is marginal.

**Recommendation:** Use **VIPPO (copper-filled, plated over)** or add a bottom-side heatsink or thermal pad. Either brings T_J to about 95–105 °C.

---

## 4. Current Capacity (EP = Drain)

- At ~1–1.5 A per 0.3 mm via (IPC-2152, ΔT ≈ 10 °C), 16 vias give about 16–24 A. That is **marginal for 20 A** once the vias are already hot from conducting heat.
- **Add 10–12 stitching vias** (same 0.3/0.6 mm size) in the drain copper pour just outside the package outline, on the side facing the load or inductor:

```
   ○ ○ ○ ○ ○ ○        ← stitching row 1, 0.5 mm from package body
   ○ ○ ○ ○ ○ ○        ← stitching row 2, 1.0 mm pitch
 ┌──────────────┐
 │  QFN-20      │
 │  (16 vias)   │
 └──────────────┘
```

- **Result:** about 26–28 vias share the current, giving ≈0.75 A per via, with comfortable margin.
- **Copper widths:** for 20 A on 2 oz outer copper, use ≥ 8 mm wide pours (IPC-2152, ΔT = 20 °C). Inner 1 oz planes should be full pours, not traces.

---

## 5. Solder Wicking and Stencil

**Open 0.3 mm vias will wick solder.** This causes voids and pulls the FET down unevenly. Choose one:

1. **VIPPO (preferred):** no wicking, best thermal performance. Adds cost of roughly 10–20% on the PCB.
2. **Bottom-side tenting (mask plug):** cheap, but trapped flux and air can outgas and cause voids. Acceptable for prototypes only.
3. **Open vias + windowpane stencil:** keep paste apertures away from the vias.

**Stencil for the EP (0.12 mm / 5 mil stencil):**

- 3×3 windowpane apertures, each about 0.8 × 0.8 mm, placed *between* via rows. This applies when not using VIPPO.
- Target **50–65% paste coverage** of the EP. This limits voiding (IPC-7093 target < 25% voids) and prevents the FET from floating.
- With VIPPO, use a 4×4 windowpane at about 60% coverage. Via positions don't matter.

---

## 6. Layer Stack and Plane Connection

| Layer | Treatment |
|---|---|
| L1 (top) | Solid EP land plus drain pour. **No thermal reliefs** on the vias (direct connect). |
| L2 (GND) | Keep solid if possible. Vias pass through with antipads, so this layer won't help heat spreading. |
| L3 (power/drain) | Drain pour under the FET footprint, ≥ 20×20 mm, direct-connected to vias. |
| L4 (bottom) | Largest possible drain copper area (≥ 25×25 mm, 2 oz). Exposed (mask-open) if attaching a thermal pad or heatsink. |

---

## 7. Multi-FET Placement (Half-Bridge or Paralleled FETs)

- Space FET centers **≥ 12 mm** apart to avoid overlapping thermal footprints, or stagger them on opposite board sides.
- For **paralleled FETs** sharing 20 A, use symmetric copper geometry so the R_DS(on) positive temperature coefficient can balance current. Mismatched via counts or pour shapes cause current h