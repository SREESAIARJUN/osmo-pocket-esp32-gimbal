# PCIe Gen2 Differential Microstrip Impedance (Top Layer)

## Information I don't have

I can't find a public "Cycle 30" stackup for the Radxa CM4 carrier board. The result depends entirely on the actual stackup, so below I use a common assumed stackup, show a worked estimate, and explain how to get the production values.

**Check the impedance target first.** The PCIe Base Spec for Gen2 calls for about **85 Ω differential**. Many CM4-class carrier boards use 85–90 Ω for PCIe lanes. Before targeting 100 Ω, confirm the requirement in the Radxa CM module/carrier design guide.

## Assumed Stackup

This is a typical 4-layer, 1.6 mm board (JLC04161H-7628 style):

| Parameter | Value |
|---|---|
| Top copper (finished, 1 oz) | T = 0.035 mm (1.38 mil) |
| Dielectric L1→L2 (7628 prepreg) | H = 0.2104 mm (8.28 mil) |
| Dielectric constant | Er ≈ 4.4 |
| Reference plane | L2 solid GND |

## Formulas (IPC-2141, edge-coupled microstrip)

Single-ended impedance:

Z₀ = [87 / √(Er + 1.41)] · ln[5.98H / (0.8W + T)]

Differential impedance:

Z_diff = 2Z₀ · [1 − 0.48·e^(−0.96·S/H)]

## Worked Solution for 100 Ω

1. 87 / √(4.4 + 1.41) = 87 / 2.410 = **36.1**
2. Choose spacing S = 0.20 mm (7.87 mil), so S/H = 0.95.
   - Coupling factor = 1 − 0.48·e^(−0.912) = 1 − 0.193 = **0.807**
3. Required Z₀ = 100 / (2 × 0.807) = **62 Ω**
4. Solve for width:
   - ln(ratio) = 62 / 36.1 = 1.717, so ratio = 5.57
   - 0.8W + T = 49.5 / 5.57 = 8.89 mil
   - **W ≈ 9.4 mil (≈ 0.24 mm)**

**Estimate: W ≈ 0.24 mm, S ≈ 0.20 mm gives about 100 Ω differential.**

For 85 Ω on the same stackup, widen the trace to about 0.30–0.32 mm, or reduce the gap.

## Caveats

- IPC-2141 formulas are only about ±10% accurate when W/H ≈ 1. Use a field solver for final values: Polar Si9000, Saturn PCB Toolkit, the JLCPCB impedance calculator, or the KiCad calculator.
- Solder mask over the traces lowers impedance by about 2–5 Ω. Compensate by narrowing the trace slightly.
- Etch trapezoid and copper plating also shift the result. Always request controlled impedance (±10%) from the fab and let them adjust the width.

## PCIe Gen2 Routing Rules (5 GT/s)

- Intra-pair skew ≤ 5 mil (0.127 mm). Inter-pair skew is not critical, since lanes are independently deskewed.
- Place AC coupling caps (100 nF, 0201/0402) on the TX lines, near the transmitter.
- Keep a continuous GND reference on L2, with no plane splits under the pairs.
- Spacing to other signals ≥ 3×W (about 20 mil). Place GND stitching vias next to any layer-change vias.
- Keep REFCLK as a 100 Ω (or 85 Ω) differential pair with similar rules.

If you share the actual Cycle 30 stackup (layer thicknesses, prepreg/core type, Er, copper weight, solder mask), I can recompute exact W/S values for both 85 Ω and 100 Ω.