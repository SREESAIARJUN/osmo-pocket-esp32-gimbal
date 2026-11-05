# 100 Ω Differential Microstrip for PCIe Gen2 (Top Layer)

**I can't give you a definitive trace geometry yet.** I don't have the actual stackup for your Radxa CM4 carrier board revision ("Cycle 5"). The result depends heavily on the dielectric height to the reference plane, εr, and copper thickness. Below is the method, a worked example on a common stackup, and what I'd need to finalize it.

---

## 1. Inputs Required

| Parameter | Symbol | Where to get it |
|---|---|---|
| Dielectric height (L1 → L2 GND) | H | Fab stackup sheet |
| Dielectric constant of prepreg | εr | Fab sheet, ideally at ~1–5 GHz |
| Finished outer copper thickness (base + plating) | T | Typically 1 oz ≈ 35 µm finished |
| Solder mask thickness / εr | — | Usually lowers Z by about 2–4 Ω |
| Target | Zdiff | 100 Ω ±10% |

---

## 2. Formulas (IPC-2141, edge-coupled surface microstrip)

Single-ended impedance:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98H}{0.8W + T}\right)$$

Differential impedance:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,S/H}\right)$$

These are approximations, accurate to roughly ±5–10%. Use a field solver for sign-off.

---

## 3. Worked Example (typical 4-layer, 1.6 mm, 7628 prepreg)

**Assumed:** H = 0.2104 mm, εr = 4.4, T = 0.035 mm, spacing S = 0.20 mm.

**Step 1 – Coupling factor**
- S/H = 0.951
- e^(−0.913) = 0.401
- 1 − 0.48 × 0.401 = **0.807**

**Step 2 – Required Z₀**
- Z₀ = 100 / (2 × 0.807) = **62.0 Ω**

**Step 3 – Solve for W**
- 87 / √5.81 = 36.10
- ln(5.98H / (0.8W + T)) = 62.0 / 36.10 = 1.716
- 5.98H / (0.8W + T) = 5.564
- 0.8W + T = 0.226 mm
- **W ≈ 0.24 mm**

**Result (example stackup):** **W ≈ 0.24 mm (9.4 mil), S ≈ 0.20 mm (7.9 mil)** → Zdiff ≈ 100 Ω before solder mask.

With solder mask, expect about 96–98 Ω. Narrow W by about 10–15 µm, or widen S slightly, to recenter.

---

## 4. PCIe Gen2 Layout Rules

- **Impedance target:** The PCIe spec nominally targets 85 Ω differential, but 100 Ω ±10% is widely used and works fine at Gen2. Check what the CM4 datasheet/design guide specifies and match it.
- **Intra-pair skew:** < 5 mil (0.127 mm). Match near the mismatch source.
- **Inter-pair:** TX and RX don't need matching to each other at Gen2.
- **AC coupling caps:** On TX only (typically 100 nF, 0201/0402), placed symmetrically. Consider a GND void under the pads to reduce capacitive dips.
- **Reference plane:** Keep a solid, unbroken GND on L2 under the entire route. No split crossings.
- **Spacing to other signals:** ≥ 3×W (or ≥ 5H) from aggressors.
- **Vias:** Minimize them. If used, place GND stitching vias adjacent to each signal via pair.
- **REFCLK:** Route as a 100 Ω pair with the same rules.

---

## 5. To Finalize

Please send:
1. The fab's stackup for Cycle 5: layer count, prepreg type/thickness, εr, and copper weights.
2. Whether solder mask is over the traces (and its thickness, if known).
3. Any spacing constraints, such as BGA/connector breakout pitch.

I'll then compute the exact W/S. I also recommend confirming with the fab's impedance calculator or a solver (Saturn PCB, Polar Si9000, or Altium/KiCad's built-in tool), and ordering impedance-controlled fabrication with coupons.