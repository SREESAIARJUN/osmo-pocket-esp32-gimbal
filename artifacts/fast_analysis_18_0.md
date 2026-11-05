# 100 Ω Differential Microstrip for PCIe Gen2 (Top Layer)

**I don't have the actual stackup for your Radxa CM4 carrier board or the "Cycle 18" revision.** Impedance depends on the dielectric height, Dk, and copper thickness, so I can't give a definitive trace geometry for your board. Below is the method and a worked example for a common 4-layer stackup. Substitute your fab's numbers.

## Assumed Stackup (JLCPCB-style 4-layer, 1.6 mm, 7628 prepreg)

| Parameter | Value |
|---|---|
| H (L1 to L2 GND prepreg) | 0.2104 mm (8.28 mil) |
| εr (7628 prepreg) | ~4.4 |
| T (1 oz outer copper, finished) | 0.035 mm (1.38 mil) |
| Reference plane | Solid GND on L2 |

## Formulas (IPC-2141 approximation)

Single-ended:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\left(\frac{5.98H}{0.8W + T}\right)$$

Edge-coupled differential:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,S/H}\right)$$

## Iteration (S = 0.20 mm)

| W (mm) | Z₀ (Ω) | Coupling factor | Z_diff (Ω) |
|---|---|---|---|
| 0.20 | 67.3 | 0.807 | 108.7 |
| 0.22 | 64.5 | 0.807 | 104.1 |
| **0.24** | **61.8** | **0.807** | **≈ 99.7** |

**Result (example stackup):** W ≈ 0.24 mm (9.4 mil), S ≈ 0.20 mm (7.9 mil) gives ≈ 100 Ω differential.

## Corrections and Caveats

- **Solder mask** over the traces typically lowers Z_diff by about 2–5 Ω. To compensate, narrow W slightly, to about 0.22 mm.
- **Accuracy:** the IPC formulas are only accurate to about ±5–10%. Verify with a field solver such as Polar Si9000, Saturn PCB Toolkit, or your fab's impedance calculator, and order controlled-impedance fabrication.
- **Target impedance:** PCIe CEM nominally targets 85 Ω differential, but 85–100 Ω is generally tolerated at Gen2 rates. Check the Raspberry Pi CM4 datasheet and design guidelines, as well as Radxa's documentation, for the target they specify, and use that value.

## PCIe Gen2 Layout Checklist

- **Intra-pair skew:** match P/N lengths to ≤ 5 mil (0.127 mm).
- **AC coupling:** place 100 nF (0201/0402) caps on the TX pairs from the CM4, close to the transmitter side or as specified. The RX caps live on the endpoint device.
- **Reference plane:** keep a continuous GND under the pairs, with no plane splits.
- **Layer changes:** add GND stitching vias next to any signal vias, and minimize via stubs.
- **Spacing:** keep ≥ 3×W (or ≥ 5H) to other signals, and ≥ 4–5×W between the TX and RX pairs.
- **Insertion loss:** keep total trace length reasonable, typically ≤ ~100 mm on FR-4 for Gen2 within budget.
- **Reference clock:** route REFCLK as a 100 Ω diff pair with the same rules.

If you share your actual stackup (layer thicknesses, prepreg/core type, Dk, copper weight, solder mask), I can recompute W/S specifically for your board.