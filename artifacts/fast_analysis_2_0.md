# PCIe Gen2 Differential Pair Calculation: 100 Ω Edge-Coupled Microstrip (Top Layer)

I don't have the actual stackup for your "Cycle 2" Radxa CM4 carrier board. The numbers below assume common 4-layer, 1.6 mm fab stackups. Replace them with your fab's real dielectric values before releasing gerbers.

## 1. Check the target impedance first
- The PCIe base spec for Gen2 and later uses **85 Ω differential**. 100 Ω is also widely used and works over short carrier-board runs.
- Check the CM4 / Radxa CM hardware design guide for the module-side routing impedance, and match it so you don't add an impedance step at the connector.

## 2. Formulas (IPC-2141 approximation)
Single-ended microstrip:

Z₀ = 87 / √(εr + 1.41) · ln[5.98·H / (0.8·W + T)]

Edge-coupled differential:

Z_diff = 2·Z₀ · (1 − 0.48·e^(−0.96·S/H))

Where:
- W = trace width
- S = gap between the two traces
- H = dielectric height to the reference plane
- T = copper thickness

A solder mask over the traces lowers Z_diff by roughly 2–5 Ω.

## 3. Worked results

### Option A: thick prepreg (e.g., 7628)
Assumptions: H = 0.21 mm, εr ≈ 4.4, T = 35 µm (1 oz).

| W | S | Z₀ | Z_diff (bare) | With mask |
|---|---|---|---|---|
| 0.20 mm (7.9 mil) | 0.15 mm | 69.6 Ω | 105.5 Ω | ~101 Ω |
| 0.20 mm | 0.13 mm | 69.6 Ω | 102 Ω | ~98 Ω |

**Recommended: W = 0.20 mm, S = 0.15 mm (≈100 Ω)**

### Option B: thin prepreg (e.g., 3313)
Assumptions: H = 0.10 mm, εr ≈ 4.1, T = 35 µm.

| W | S | Z₀ | Z_diff (bare) | With mask |
|---|---|---|---|---|
| 0.12 mm (4.7 mil) | 0.15 mm | 58.2 Ω | 103 Ω | ~99 Ω |

**Recommended: W = 0.12 mm, S = 0.15 mm (≈100 Ω)**

The thin-prepreg option gives tighter coupling and less crosstalk, which suits the PCIe area.

## 4. PCIe Gen2 routing rules to pair with this
- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Match length near the mismatch source.
- **Inter-pair skew:** TX-to-RX matching is not required.
- **Reference plane:** keep a solid, unbroken GND plane on L2 under the pairs. Do not cross plane splits.
- **AC-coupling caps:** use 100 nF (0402 or 0201) on TX, placed near the transmitter or connector. Use symmetric pads, and consider a plane cutout under the pads.
- **Vias:** keep to ≤ 2 per pair. Place GND stitching vias next to any signal vias.
- **Spacing to other signals:** ≥ 3×H, or ≥ 5W from aggressors.
- **REFCLK:** route it as a 100 Ω differential pair with the same rules.

## 5. Verify
Because the IPC formulas carry ±5–10% error:
1. Enter the exact stackup into your fab's impedance calculator (e.g., JLC, PCBWay) or a field solver (e.g., Saturn PCB, Polar Si9000).
2. Order impedance-controlled fabrication with a ±10% tolerance, and specify "100 Ω diff, L1, ref L2" on the fab notes.

If you share your Cycle 2 stackup (layer count, prepreg type and thickness, copper weight), I can recompute exact W and S values.