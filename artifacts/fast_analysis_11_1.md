# PCIe Gen2 100 Ω Differential Microstrip: Top Layer

I don't have the actual stackup for your board or your "Cycle 11" revision. The worked example below uses a common 4-layer stackup. Substitute your fab's real values and confirm with a field solver.

## 1. Assumed Stackup (Top Layer → L2 GND)

Example: JLCPCB JLC04161H-7628, 1.6 mm, 4-layer.

| Parameter | Symbol | Value |
|---|---|---|
| Dielectric height (prepreg 7628) | h | 0.2104 mm |
| Copper thickness (1 oz, finished) | t | 0.035 mm |
| Dielectric constant | εr | 4.4 |
| Reference plane | – | Solid GND on L2, unbroken under the pair |

## 2. Formulas (IPC-2141 / Wadell approximation)

Single-ended (surface microstrip):

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Edge-coupled differential:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

## 3. Calculation

Constant term: 87 / √5.81 = **36.1**

The trace spacing is fixed at s = 0.20 mm, which gives a coupling factor of 1 − 0.48·e^(−0.913) = **0.807**.

| w (mm) | 0.8w+t | ln(5.98h / (0.8w+t)) | Z₀ (Ω) | Z_diff (Ω) |
|---|---|---|---|---|
| 0.20 | 0.195 | 1.865 | 67.3 | 108.6 |
| 0.23 | 0.219 | 1.748 | 63.1 | 101.9 |
| **0.24** | **0.227** | **1.712** | **61.8** | **≈ 99.7** |

**Result:** about **w = 0.24 mm (9.4 mil), s = 0.20 mm (7.9 mil)** gives roughly 100 Ω differential.

Solder mask over the traces typically lowers Z_diff by 2–5 Ω. You may need to narrow the traces to about 0.22–0.23 mm to compensate. Verify with your fab's impedance calculator or Polar SI9000.

## 4. Target Check

- The PCIe base spec is nominally 85 Ω differential. The Raspberry Pi CM4 datasheet routing guidelines specify the target for the module's PCIe lanes. Check which value your carrier design (Radxa CM4-compatible) requires before finalizing. Both 85 Ω and 100 Ω are used in practice, with ±10–15% tolerance.
- For an 85 Ω target on the same stackup, widen to about 0.32 mm with s ≈ 0.18 mm, then verify in a solver.

## 5. PCIe Gen2 Routing Rules

- **Intra-pair skew:** < 5 mil (0.127 mm).
- **AC coupling caps:** 100 nF (0201/0402) are required on TX. Place them near the connector or the source, symmetric within the pair.
- **Reference plane:** keep it continuous with no splits under the pair. If a layer change is unavoidable, add GND stitching vias.
- **Spacing to other signals:** ≥ 3× the trace width (3w) between this pair and other signals.
- **TX/RX separation:** keep TX and RX pairs apart, or interleave them with GND.
- **Lane polarity:** P/N inversion is allowed by the spec.
- **Bends:** use 45° bends or arcs. Avoid stubs.

If you share your actual stackup (layer count, prepreg type and thickness, copper weight, fab house), I can recompute the exact width and spacing.