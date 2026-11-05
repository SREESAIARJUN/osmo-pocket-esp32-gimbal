# PCIe Gen2 Differential Microstrip Impedance (Top Layer)

I don't have the stackup for your "Cycle 15" Radxa CM4 carrier board, so I can't give final numbers for it. Below are a worked example on a common stackup and the method to apply to yours.

## ⚠️ Check the target impedance first

- The PCIe Base Spec (Gen2) allows a differential impedance of roughly **85 Ω (nominal) to 100 Ω**.
- The **Raspberry Pi CM4 hardware design guide**, which Radxa's CM4-compatible modules follow, calls for **90 Ω ±10%** on PCIe pairs.
- **100 Ω** is the USB/Ethernet/HDMI-style target. It will usually work for a short Gen2 link, but confirm against the Radxa module datasheet before committing.

## Method: IPC-2141 edge-coupled microstrip

Single-ended impedance:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Differential impedance:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

Where:
- *w* = trace width
- *s* = edge-to-edge gap
- *h* = dielectric height to the reference plane (L2 GND)
- *t* = copper thickness

## Worked example: typical 4-layer, 1.6 mm board

Assumed stackup (similar to JLC 7628):

| Parameter | Value |
|---|---|
| h (L1→L2 prepreg) | 0.2104 mm |
| εr | 4.4 |
| t (1 oz + plating) | 0.035 mm |

**100 Ω target, w = 0.28 mm:**
- 0.8w + t = 0.259 mm
- 5.98h = 1.258 mm
- Z₀ = 36.1 × ln(4.857) ≈ **57.0 Ω**
- With s = 0.30 mm: s/h = 1.426, so the coupling factor = 1 − 0.48·e^(−1.369) = 0.878
- **Z_diff ≈ 100 Ω** ✔

**90 Ω alternative, w = 0.32 mm:**
- Z₀ ≈ 52.8 Ω
- With s = 0.25 mm: coupling factor ≈ 0.847
- **Z_diff ≈ 89.5 Ω** ✔

| Target | Width (w) | Gap (s) | Z_diff |
|---|---|---|---|
| 100 Ω | 0.28 mm (11 mil) | 0.30 mm (12 mil) | ~100 Ω |
| 90 Ω (CM4 guide) | 0.32 mm (12.6 mil) | 0.25 mm (10 mil) | ~90 Ω |

## Corrections and practical notes

1. **Solder mask** lowers microstrip impedance by about 2–5 Ω. Field solvers usually suggest narrowing the trace slightly to compensate.
2. **IPC formulas are accurate to about ±5%.** Confirm with a 2D field solver (Saturn PCB Toolkit, Polar Si9000, or your fab's calculator), then specify **controlled impedance** on the fab notes.
3. **Routing rules for PCIe Gen2 (5 GT/s):**
   - Intra-pair skew under 5 mil (0.127 mm).
   - Keep a continuous GND reference on L2 with no splits under the pairs.
   - Space pairs from other signals by at least 3–5w.
   - Place AC coupling caps (100 nF, 0201/0402) on TX only, near the transmitter.
   - Minimize vias, and add GND stitching vias next to any signal vias.
   - The CM4 guide limits total length mismatch and recommends keeping the REFCLK pair at the same impedance.

## To compute values for your board, send:
- The layer stackup (prepreg thickness L1→L2, εr/material, copper weight)
- Whether the top layer has solder mask over the traces
- The fab house, since its own stackup and calculator should be the final reference

With those, I can recompute exact w/s values for 85, 90, or 100 Ω.