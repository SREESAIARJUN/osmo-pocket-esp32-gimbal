# PCIe Gen2 Differential Microstrip Impedance (Top Layer)

**I don't have the Radxa CM4 carrier board's "Cycle 29" stackup.** Please share the layer-1-to-layer-2 dielectric height, Er, copper weight, and solder mask details, and I'll recompute. In the meantime, here is the method and a worked example using a common 4-layer stackup.

---

## 1. Check the target impedance first

- The **PCIe Base Spec (Gen2 and later)** recommends **85 Ω ±15% differential**. 100 Ω falls within the tolerance band and is commonly used.
- The **Raspberry Pi CM4 / Radxa CM-series design guides** may specify a different value. Verify against your module's hardware design guide before fixing on 100 Ω.

## 2. Formulas (IPC-2141, edge-coupled microstrip)

Single-ended impedance:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Differential impedance:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

where h = dielectric height, w = trace width, t = copper thickness, s = edge-to-edge gap.

## 3. Worked example (assumed stackup)

These are typical values for a JLC-style 1.6 mm 4-layer board with 7628 prepreg:

| Parameter | Value |
|---|---|
| h (L1→L2 prepreg) | 0.2104 mm (8.28 mil) |
| εr | 4.4 |
| t (1 oz outer, plated) | ~0.035 mm (1.4 mil) |

**Trace width w = 0.25 mm (9.84 mil):**
- 0.8w + t = 9.27 mil
- 5.98h = 49.5 mil
- ln(49.5 / 9.27) = 1.675
- Z₀ = (87 / 2.410) × 1.675 ≈ **60.5 Ω**

**Results for different gaps s:**

| s | s/h | Z_diff |
|---|---|---|
| 0.20 mm | 0.951 | 121 × 0.807 ≈ **97.6 Ω** |
| 0.25 mm | 1.188 | 121 × 0.847 ≈ **102.4 Ω** |

→ **w ≈ 0.25 mm, s ≈ 0.22 mm gives ≈ 100 Ω.**

For **85 Ω**, w ≈ 0.30 mm with s ≈ 0.18–0.20 mm is a reasonable starting point.

## 4. Caveats

- **Accuracy:** IPC-2141 is only accurate to about ±5–10%. Solder mask over the traces typically lowers Z_diff by 2–4 Ω. Do the final sizing with a field solver (Saturn PCB, Polar Si9000, or your fab's impedance calculator) using the exact stackup.
- **Fab control:** Specify controlled impedance on the fab order, e.g., "100 Ω ±10% diff, L1, ref L2 GND."

## 5. PCIe Gen2 routing reminders

- Intra-pair skew: < 5 mil (0.127 mm)
- Keep a continuous GND reference under the pairs on L2, with no plane splits.
- AC coupling caps (typically 100 nF, 0201/0402) go on the TX side, placed symmetrically.
- Keep at least 3× trace-width spacing (≥ 3w) to other pairs and minimize vias, using GND stitching vias at layer transitions.

Send the actual Cycle 29 stackup numbers and I'll give you exact w/s values.