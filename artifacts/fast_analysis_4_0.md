# PCIe Gen2 100 Ω Differential Microstrip: Top Layer

I don't have the actual stackup for your Radxa CM4 carrier "Cycle 4" board, and Radxa doesn't publish one. The numbers below therefore use common fab stackups. Replace them with your fab's dielectric height, εr and copper thickness, then confirm with a field solver.

## 1. Formulas (IPC-2141 edge-coupled microstrip)

Single-ended impedance:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}}\ \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Differential impedance:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

Where:
- h = dielectric height from L1 to L2 (the reference plane)
- w = trace width
- s = edge-to-edge gap between the two traces
- t = copper thickness

## 2. Worked Example A: Standard 4-layer, 1.6 mm, 7628 prepreg

This is typical of JLCPCB-style boards.

- h = 0.2104 mm, εr ≈ 4.4, t = 0.035 mm (1 oz finished)
- Prefactor: 87 / √5.81 = 36.1

With w = 0.20 mm:
- Z0 = 36.1 × ln(1.258 / 0.195) = 36.1 × 1.864 ≈ **67.3 Ω**

With s = 0.15 mm:
- s/h = 0.713
- Coupling factor = 1 − 0.48·e^(−0.684) = 0.758
- Zdiff = 2 × 67.3 × 0.758 ≈ **102 Ω**

Solder mask over the traces typically lowers impedance by about 2–5 Ω, so the result lands near 100 Ω.

**Result: w ≈ 0.20 mm (8 mil), s ≈ 0.15–0.18 mm (6–7 mil).**

## 3. Worked Example B: Thin prepreg (3313, h ≈ 0.1 mm)

This is common when routing density is high.

- h = 0.0994 mm, εr ≈ 4.1, t = 0.035 mm
- Prefactor: 87 / √5.51 = 37.1

With w = 0.12 mm:
- Z0 = 37.1 × ln(0.594 / 0.131) ≈ **56 Ω**

With s = 0.20 mm:
- s/h = 2.01
- Coupling factor = 0.930
- Zdiff ≈ **104 Ω**

**Result: w ≈ 0.127 mm (5 mil), s ≈ 0.20 mm (8 mil) gives ≈ 100 Ω.**

The thin-prepreg option is usually preferred for CM4 carriers. It gives narrower traces that fit the CM4 connector breakout and tighter coupling to the reference plane.

## 4. Accuracy caveat

IPC-2141 formulas are only accurate to about ±5–10%. Before release, verify with one of these:
- Your fab's impedance calculator, such as JLCPCB's or PCBWay's
- Saturn PCB Toolkit
- A 2D field solver, such as Polar Si9000 or the KiCad/Altium solvers

You should also request controlled impedance (100 Ω ±10%) on the fab order.

## 5. PCIe Gen2 routing rules for the CM4

| Item | Guideline |
|---|---|
| Differential impedance | 100 Ω ±10%, which matches the CM4 datasheet recommendation |
| Intra-pair skew | ≤ 5 mil (0.127 mm); match lengths near the mismatch point |
| Inter-pair (TX vs RX) | No strict requirement; keep the clock pair reasonably matched |
| AC coupling caps | 100 nF (0402/0201) on TX only, placed near the transmitter; symmetric pads with ground void under them if needed |
| Reference plane | Solid GND on L2, with no splits under the pairs |
| Layer changes | Minimize; when a pair changes layers, place a GND stitching via within about 1 mm |
| Pair-to-pair spacing | ≥ 3× trace width (≥ 5w from other aggressors) |
| Total length | Keep short; Gen2 at 5 GT/s tolerates a few inches on FR-4 comfortably |

If you share your actual stackup (layer count, prepreg type and thickness, copper weight, fab house), I can compute exact width and gap values for your board.