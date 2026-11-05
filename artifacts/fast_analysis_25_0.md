# PCIe Gen2 Differential Microstrip Impedance (Top Layer)

I don't have your board's stackup or the "Cycle 25" design files, so I can't give the final trace geometry. Below is the method and a worked example using a common 4-layer stackup. If you send your fab's stackup, I can recompute with your numbers.

## 1. Check the target impedance first

- The PCIe base and CEM specifications use **85 Ω differential nominal**. Many designs use 85–100 Ω successfully at Gen2 (5 GT/s).
- **Confirm the target in the Radxa or compute-module hardware design guide.** Some module vendors specify 85 Ω or 90 Ω for PCIe. If the guide specifies 100 Ω, the calculation below applies directly.

## 2. Inputs needed

| Parameter | Symbol | Example value (JLC 4-layer, 7628 prepreg, 1.6 mm) |
|---|---|---|
| Dielectric height (top to L2 GND) | h | 0.2104 mm |
| Dielectric constant | εr | 4.4 (7628 is often 4.4–4.6) |
| Copper thickness (1 oz outer) | t | 0.035 mm |
| Trace width | w | to be solved |
| Gap (edge to edge) | s | to be solved |

## 3. Formulas (IPC-2141, edge-coupled microstrip)

Single-ended impedance:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Differential impedance:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

## 4. Worked example (target 100 Ω)

Try **w = 0.25 mm**:

- √(4.4 + 1.41) = 2.410, so 87 / 2.410 = 36.10
- 5.98 × 0.2104 = 1.258
- 0.8 × 0.25 + 0.035 = 0.235
- ln(1.258 / 0.235) = ln(5.353) = 1.678
- **Z₀ ≈ 60.6 Ω**, so 2Z₀ = 121.2 Ω

Solve for the gap that gives 100 Ω:

- 100 / 121.2 = 0.825, so 0.48·e^(−0.96s/h) = 0.175
- e^(−0.96s/h) = 0.364, so 0.96s/h = 1.010
- **s ≈ 0.22 mm**

**Result: w ≈ 0.25 mm, s ≈ 0.22 mm, giving about 100 Ω differential.**

Alternative: w = 0.20 mm gives Z₀ ≈ 67 Ω, which needs s ≈ 0.14 mm. That is more tightly coupled and harder to fabricate.

## 5. Corrections and verification

- **Accuracy:** IPC-2141 formulas are only good to about ±5–10%. Verify with a field solver (Saturn PCB Toolkit, Polar SI9000, or your fab's online impedance calculator).
- **Solder mask:** Coating the traces lowers the impedance by about 2–4 Ω. Widen the gap slightly to compensate.
- **Plating:** Plated outer copper (about 45–50 µm finished) also lowers the impedance slightly.
- **Controlled-impedance order:** Specify "100 Ω ±10% differential, top layer, reference L2" so the fab tunes the width.

## 6. PCIe Gen2 routing rules

- Keep an unbroken GND reference on L2 under the pairs, with no plane splits.
- Match length within a pair to ≤ 5 mil (0.127 mm).
- Matching length between TX and RX pairs is not required, since each lane is independent.
- Place AC-coupling caps (0.1 µF, 0201/0402) on the TX lines near the transmitter. Void the reference plane under the pads if they are large.
- Minimize vias. If you must use one, add GND stitching vias next to it.
- Keep at least 3×w spacing to other signals (≥ 5×w preferred).

Send your actual stackup (layer heights, εr, copper weights), and I'll recalculate the exact width and gap for 100 Ω or 85 Ω.