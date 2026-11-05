# 100 Ω Differential Microstrip for PCIe Gen2 (Top Layer)

I don't have the Radxa CM4 carrier board's stackup or any "Cycle 17" documentation, so I can't give the board's actual values. Below I calculate for the two most common 4-layer, 1.6 mm fab stackups. Substitute your fab's numbers for the real design.

## Impedance target

- **PCIe base spec:** 85 Ω differential nominal. 100 Ω ±10% is widely tolerated at Gen2 (5 GT/s).
- **Host module requirement:** The Raspberry Pi CM4 and Radxa CM3/CM5 hardware design guides give their own PCIe target. Check the guide for your module, and use that number if it differs from 100 Ω.

## Formulas (IPC-2141 / Wadell approximations)

Single-ended surface microstrip:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}}\ \ln\!\left(\frac{5.98H}{0.8W + T}\right)$$

Edge-coupled differential:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,S/H}\right)$$

Where:
- H = dielectric height to the L2 ground plane
- W = trace width
- S = edge-to-edge gap between the pair
- T = copper thickness

## Case A: 7628 prepreg (e.g., JLC04161H-7628)

Assumptions: H = 0.2104 mm, εr ≈ 4.4, T = 0.035 mm (1 oz finished).

| W (mm) | Z₀ (Ω) | S (mm) | Z_diff (Ω) |
|---|---|---|---|
| 0.20 | 67.3 | 0.20 | 108.6 |
| **0.20** | **67.3** | **0.15** | **≈102 (≈98–99 with solder mask)** |
| 0.22 | 64.4 | 0.15 | 97.6 |

**Recommended:** W = 0.20 mm (7.9 mil), S = 0.15 mm (5.9 mil), giving about 100 Ω.

## Case B: 2116/3313 thin prepreg (e.g., JLC04161H-3313)

Assumptions: H ≈ 0.0994 mm, εr ≈ 4.1, T = 0.035 mm.

| W (mm) | Z₀ (Ω) | S (mm) | Z_diff (Ω) |
|---|---|---|---|
| 0.11 | 58.4 | 0.20 | 108.6 |
| **0.12** | **56.0** | **0.15** | **≈99.4** |
| 0.127 | 54.5 | 0.15 | 96.7 |

**Recommended:** W = 0.12 mm (4.7 mil), S = 0.15 mm, giving about 100 Ω.

The thinner dielectric gives tighter coupling and narrower traces, which makes it easier to fan out from the CM4 connector. It is generally preferred for PCIe.

## Verify before release

These closed-form equations are only accurate to about ±5–8%. Confirm the final geometry with one of:
- your fab's impedance calculator (e.g., JLCPCB),
- Saturn PCB Toolkit, or
- a 2D field solver (Polar Si9000).

Then order an impedance-controlled build with ±10% tolerance.

## PCIe Gen2 routing rules to pair with this

- **Intra-pair skew:** match P/N within ≤5 mil (0.127 mm).
- **Inter-pair matching:** TX vs RX does not need to be matched.
- **AC coupling:** place 100 nF (0402) caps on the TX lanes, near the transmitter. On the CM4, check whether they are already on-module.
- **Reference plane:** keep an unbroken GND plane on L2 under the pairs. Do not cross splits.
- **Spacing:** keep at least 3×W (ideally 5×H) to other signals, and at least 4×W between TX and RX pairs.
- **Vias:** minimize them. If you must change layer, add a GND stitching via next to each signal via.
- **REFCLK:** route as a 100 Ω pair with the same rules.

If you share the board's actual stackup (layer thicknesses, εr, copper weight), I can recompute exact W/S values.