# PCIe Gen2 Differential Microstrip Impedance: Top Layer

I don't have your specific stackup from "Cycle 10." The calculation below assumes a common 4-layer, 1.6 mm board (JLCPCB JLC7628-style). **Replace the inputs with your fab's actual stackup values.**

## 1. Assumed Inputs (Top Layer L1, Referenced to GND on L2)

| Parameter | Symbol | Value |
|---|---|---|
| Dielectric height (L1→L2 prepreg, 7628) | h | 0.2104 mm (8.28 mil) |
| Copper thickness (1 oz outer) | t | 0.035 mm (1.38 mil) |
| Dielectric constant | εr | 4.4 |
| Target | Zdiff | 100 Ω ±10% |

## 2. Formulas (IPC-2141 Edge-Coupled Microstrip)

Single-ended impedance:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Differential impedance:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

## 3. Iteration

| w (mm) | s (mm) | Z0 (Ω) | Zdiff (Ω) |
|---|---|---|---|
| 0.20 | 0.20 | 67.3 | 108.6 |
| 0.23 | 0.20 | 63.1 | 101.9 |
| **0.23** | **0.18** | **63.1** | **≈ 99.6 ✔** |

Worked example for the final row:
- √(4.4 + 1.41) = 2.410, so 87 / 2.410 = 36.10
- 0.8(0.23) + 0.035 = 0.219, and 5.98(0.2104) = 1.258
- ln(1.258 / 0.219) = ln(5.744) = 1.748, so **Z0 = 63.1 Ω**
- s/h = 0.855, e^(−0.821) = 0.440, and 1 − 0.211 = 0.789
- **Zdiff = 2 × 63.1 × 0.789 ≈ 99.6 Ω**

## 4. Result (Starting Geometry)

- **Trace width:** ~0.23 mm (≈9 mil)
- **Pair gap:** ~0.18 mm (≈7 mil)
- **Zdiff:** ≈100 Ω

## 5. Important Caveats

1. **Check the required target impedance.** The Raspberry Pi CM4 design guidelines, which Radxa CM4-compatible modules follow, call for **90 Ω** on PCIe and USB, not 100 Ω. The PCIe base spec itself targets 85 Ω. Confirm the target before finalizing. For 90 Ω with this stackup, widen to roughly w ≈ 0.27 mm at s ≈ 0.18 mm.
2. **IPC-2141 accuracy is only ±5–10%.** Verify with a field solver such as Saturn PCB Toolkit, Polar Si9000, or your fab's impedance calculator (e.g., JLCPCB's). Then order controlled-impedance fabrication.
3. **Soldermask lowers the impedance.** It typically reduces Zdiff by 2–5 Ω on outer layers, so the solver should include it.

## 6. PCIe Gen2 Layout Rules (5 GT/s)

- Intra-pair length match: ≤ 5 mil (0.127 mm)
- Keep a continuous GND reference on L2. No plane splits under the pairs.
- Place AC coupling caps (100 nF, 0201/0402) on the TX lines near the transmitter.
- Keep spacing to other signals at ≥ 3× the trace width (≥ 5w between TX and RX pairs).
- Minimize vias. If a layer change is unavoidable, add GND stitching vias next to each signal via.
- Lane-to-lane matching is not required for a ×1 link. Keep REFCLK as a 100 Ω pair with similar rules.

If you share your actual stackup (layer heights, εr, copper weight) and confirm 90 Ω vs. 100 Ω, I can recompute exact dimensions.