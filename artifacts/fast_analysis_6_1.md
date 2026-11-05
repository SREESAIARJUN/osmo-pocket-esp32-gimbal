# PCIe Gen2 Differential Microstrip Impedance (Top Layer, 100 Ω)

I don't have your actual stackup for "Cycle 6," so the calculation below uses two common 4-layer, 1.6 mm fab stackups. Substitute your fab's real numbers (prepreg height, Er, copper weight) and confirm with their impedance calculator or a field solver before release.

## 1. Method (IPC-2141 edge-coupled microstrip)

**Single-ended impedance:**

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

**Differential impedance:**

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

Where:
- h = dielectric height from L1 to the L2 ground plane
- w = trace width
- s = edge-to-edge gap
- t = copper thickness (1 oz ≈ 1.4 mil)

These closed-form equations are accurate to about ±5–10%.

## 2. Case A: 7628 prepreg (h = 0.21 mm / 8.28 mil, Er ≈ 4.4, 1 oz)

- √(4.4 + 1.41) = 2.41, so 87 / 2.41 = 36.1
- Choose w = 0.20 mm (7.87 mil):
  - 0.8w + t = 7.70 mil
  - 5.98h = 49.5 mil
  - ln(6.43) = 1.861
  - **Z₀ ≈ 67.2 Ω**
- Choose s = 0.15 mm (5.9 mil):
  - s/h = 0.713
  - e^(−0.684) = 0.505
  - Coupling factor = 1 − 0.242 = 0.758
  - **Z_diff ≈ 2 × 67.2 × 0.758 ≈ 101.8 Ω**

Solder mask typically lowers the result by about 2–4 Ω, giving roughly 98–100 Ω. ✔

**Result: w = 0.20 mm, s = 0.15 mm**

## 3. Case B: 3313 prepreg (h ≈ 0.10 mm / 3.9 mil, Er ≈ 4.1, 1 oz)

- 87 / √5.51 = 37.1
- Choose w = 4 mil (0.10 mm):
  - ln(23.4 / 4.6) = 1.626
  - **Z₀ ≈ 60.3 Ω**
- Choose s = 5 mil (0.127 mm):
  - Coupling factor = 0.859
  - **Z_diff ≈ 103.6 Ω**
- Widening to w ≈ 0.11 mm brings it to about 100 Ω, or about 97 Ω with solder mask.

**Result: w ≈ 0.10–0.11 mm, s = 0.127 mm**

## 4. Summary

| Stackup (L1→L2) | w | s | Z_diff (calc) |
|---|---|---|---|
| 7628, h = 0.21 mm | 0.20 mm | 0.15 mm | ≈ 100 Ω |
| 3313, h = 0.10 mm | 0.10–0.11 mm | 0.127 mm | ≈ 100 Ω |

## 5. PCIe Gen2 Routing Notes

- **Target impedance:** The PCIe spec nominal is 85 Ω ±15%, so 100 Ω ±10% falls inside that window and is acceptable. Check the Radxa CM module's design guide in case it specifies 85 or 90 Ω for its PCIe lanes.
- **Length matching:** Keep intra-pair skew ≤ 5 mil (0.127 mm). Inter-pair matching (TX to RX) is not required.
- **AC coupling:** Place 100 nF (0201/0402) capacitors on the TX lanes near the transmitter, and void the ground plane under the cap pads if possible.
- **Reference plane:** Use a continuous GND reference on L2 with no split crossings. Add a stitching via within about 1 mm of any layer-change via.
- **Spacing:** Keep at least 3×w (preferably 5×h) to other signals, and avoid stubs.
- **Ordering:** Request controlled impedance from the fab (e.g., "100 Ω ±10% diff, L1, ref L2") and have them adjust w/s to their actual laminate.

If you share the actual Cycle 6 stackup (prepreg type or thickness, copper weight), I can recompute exact values.