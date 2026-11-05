# PCIe Gen2 Differential Microstrip: 100 Ω Calculation

## A caveat about the board

I don't have the stackup for a "Radxa CM4 carrier board (Cycle 14)". I don't know its layer count, dielectric heights, materials or copper weights, and I won't invent them. Below is the method and a worked example on a common 4-layer stackup. Substitute your fab's actual numbers, or send me the stackup and I'll redo it.

**Check the target impedance too.** The Raspberry Pi CM4 design guidelines, which Radxa CM-series pinout boards generally follow, specify **90 Ω ±10% differential** for PCIe. The PCIe base spec itself is built around 85 Ω. 100 Ω usually works for Gen2 over short carrier-board runs, but confirm what your module's datasheet calls for.

## Method (edge-coupled surface microstrip, IPC-2141)

**Single-ended impedance:**

Z₀ = 87 / √(εr + 1.41) · ln[5.98·H / (0.8·W + T)]

**Differential impedance:**

Z_diff = 2·Z₀ · [1 − 0.48·e^(−0.96·S/H)]

Where:
- H = dielectric height from the top trace to the reference plane (L2 GND)
- W = trace width
- T = copper thickness
- S = edge-to-edge gap between the two traces of a pair

## Worked example

This uses a typical 4-layer, 1.6 mm board with 7628 prepreg, similar to JLC04161H-7628:

| Parameter | Value |
|---|---|
| H (L1 to L2) | 0.21 mm |
| εr | 4.4 |
| T (1 oz + plating) | 0.035 mm |

**Trial: W = 0.25 mm, S = 0.20 mm**

1. 0.8·W + T = 0.235 mm
2. 5.98·H = 1.256 mm
3. ln(1.256 / 0.235) = ln(5.34) = 1.676
4. Z₀ = (87 / 2.41) × 1.676 ≈ **60.5 Ω**
5. Coupling factor: S/H = 0.952, so 1 − 0.48·e^(−0.914) = 0.808
6. Z_diff = 2 × 60.5 × 0.808 ≈ **97.8 Ω**

Solder mask over the traces typically lowers impedance by about 2–5 Ω. On this stackup, roughly:

| Target | W | S |
|---|---|---|
| 100 Ω | ≈ 0.22–0.25 mm | ≈ 0.20 mm |
| 90 Ω | ≈ 0.28–0.30 mm | ≈ 0.18–0.20 mm |

A thinner prepreg (e.g., 2116, H ≈ 0.1 mm) needs much narrower traces, around 0.10–0.12 mm for 100 Ω.

The IPC formula is only accurate to about ±5–10%. Verify the final geometry with a field solver (Saturn PCB Toolkit, Polar Si9000, or your fab's impedance calculator) and order impedance-controlled fabrication.

## PCIe Gen2 routing rules to pair with it

- **Intra-pair skew:** keep P/N length matched to ≤ 5 mil (0.127 mm).
- **Reference plane:** use a solid, unbroken GND plane on L2 under the pairs, with no split crossings.
- **AC coupling:** place the 100 nF TX AC-coupling caps near the transmitter and keep the pads symmetric.
- **Spacing:** keep pair-to-pair spacing ≥ 3×W (or ≥ 5×H) and keep TX and RX pairs apart.
- **Vias:** minimize them. If a layer change is required, add GND stitching vias next to each signal via.
- **Gen2 polarity:** P/N inversion within a pair is tolerated, but lane order matters for x1 links.
- **Reference clock:** route the REFCLK pair to the same impedance rules.

If you send me your fab's stackup (layer count, prepreg/core thicknesses, εr, copper weight, solder mask), I'll compute exact W/S values for 100 Ω and 90 Ω.