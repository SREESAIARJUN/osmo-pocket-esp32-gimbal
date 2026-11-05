# 100 Ω Differential Microstrip for PCIe Gen2 (Top Layer)

I don't have the actual stackup for the Radxa CM4 carrier board, and I don't know what "Cycle 19" refers to (a board revision, fab order, or project milestone). Rather than invent numbers for that board, below is the method and a worked example using a common 4-layer stackup. Substitute your real values.

## 1. Inputs needed from your fab or stackup file

| Parameter | Symbol | Example (JLCPCB-style 4L, 1.6 mm, 7628 prepreg) |
|---|---|---|
| Dielectric height, L1 to L2 (GND) | H | 0.2104 mm (8.28 mil) |
| Dielectric constant | Er | ~4.4 |
| Finished copper thickness (1 oz outer) | T | 0.035 mm (1.38 mil) |
| Trace width | W | solve for this |
| Edge-to-edge gap | S | solve for this |

## 2. Equations (IPC-2141 approximation)

Single-ended impedance:

Z₀ = 87 / √(Er + 1.41) × ln[ 5.98·H / (0.8·W + T) ]

Differential impedance:

Z_diff = 2·Z₀ × [ 1 − 0.48·e^(−0.96·S/H) ]

## 3. Worked example

The constant term is 87 / √5.81 = 36.1.

**Try W = 6 mil, S = 6 mil:**
- 5.98H = 49.5 and 0.8W + T = 6.18, so Z₀ = 36.1 × ln(8.01) = 75.1 Ω.
- S/H = 0.725, so the coupling factor = 1 − 0.48·e^(−0.696) = 0.761.
- Z_diff ≈ 114 Ω. This is too high, so widen the traces.

**Try W = 8 mil, S = 6 mil:**
- 0.8W + T = 7.78, so Z₀ = 36.1 × ln(6.36) = 66.8 Ω.
- Z_diff = 2 × 66.8 × 0.761 ≈ 101.7 Ω.

Solder mask over the traces typically lowers impedance by about 2–5 Ω. The approximate starting point is therefore **W ≈ 7.5–8 mil, S ≈ 6 mil**, giving about 100 Ω.

## 4. Important caveats

- **The IPC formula is only about ±5–10% accurate.** Confirm with a field solver such as Saturn PCB Toolkit, Polar Si9000, KiCad's calculator, or JLCPCB's online impedance tool. Better still, order impedance control and let the fab adjust the geometry.
- **Check the impedance target.** PCIe CEM nominally specifies 85 Ω differential. The Raspberry Pi CM4 datasheet also gives guidance for PCIe and USB pairs. 100 Ω ±15% usually works for Gen2 over short runs, but verify which target your design requires.
- **Gen2 layout rules (5 GT/s):**
  - Intra-pair skew under ~5 mil.
  - Keep the reference plane solid underneath, with no splits.
  - Place AC coupling caps on TX only, and keep them symmetric.
  - Use stitching vias at every layer change.
  - Maintain clearance of at least 3×W to other signals.

If you share the actual stackup (layer count, prepreg type and thickness, Er, copper weight, solder mask), I can compute the specific W and S for your board.