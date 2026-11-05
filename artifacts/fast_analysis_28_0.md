# PCIe Gen2 Differential Microstrip Impedance (100 Ω Target)

I don't have the actual stackup for your Radxa CM4 carrier board or "Cycle 28." The calculation below uses a **common assumed stackup**. Replace the inputs with your fab's real stackup before committing the routing.

## 1. Assumed Stackup (Top Layer → L2 GND)

This is a typical 4-layer, 1.6 mm board (e.g., JLC 7628-style):

| Parameter | Symbol | Value |
|---|---|---|
| Dielectric height (prepreg to L2 GND) | h | 0.2104 mm (8.3 mil) |
| Dielectric constant (FR-4 / 7628) | εr | 4.4 |
| Copper thickness (1 oz finished) | t | 0.035 mm |
| Solder mask | — | Ignored at first; corrected in section 5 |

## 2. Single-Ended Microstrip Impedance (Hammerstad / IPC-2141)

$$
Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)
$$

Try a trace width of **w = 0.25 mm**:

- √(4.4 + 1.41) = 2.41, so 87 / 2.41 = 36.1
- 0.8w + t = 0.200 + 0.035 = 0.235 mm
- 5.98h = 1.258 mm
- ln(1.258 / 0.235) = ln(5.35) = 1.677

**Z₀ ≈ 36.1 × 1.677 ≈ 60.5 Ω**

## 3. Edge-Coupled Differential Impedance

$$
Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)
$$

Try a gap of **s = 0.22 mm**:

- s/h = 0.22 / 0.2104 = 1.046
- e^(−0.96 × 1.046) = e^(−1.004) = 0.366
- 1 − 0.48 × 0.366 = 0.824

**Z_diff ≈ 2 × 60.5 × 0.824 ≈ 99.7 Ω ✅**

## 4. Result (Assumed Stackup)

| Parameter | Value |
|---|---|
| Trace width (W) | **0.25 mm (~9.8 mil)** |
| Gap (S) | **0.22 mm (~8.7 mil)** |
| Z_diff | **≈ 100 Ω** |
| Z_odd (single-ended, coupled) | ≈ 50 Ω |

## 5. Corrections and Checks

- **Solder mask** (~15–25 µm LPI over the traces) typically lowers Z_diff by about 2–5 Ω. To compensate, narrow W to about 0.23 mm or widen S to about 0.25 mm.
- **Formula accuracy:** these closed-form equations are roughly ±5–10% accurate. Verify with a 2D field solver such as Saturn PCB Toolkit, Polar Si9000, or your fab's own impedance calculator. Then request **controlled impedance (100 Ω ±10%)** on the fab order.
- **Check the impedance target itself.** The PCIe CEM spec and many SoC/CM design guides specify **85 Ω** (or 85–100 Ω) differential for PCIe. Confirm what the Radxa CM4 / Raspberry Pi CM4 hardware design guide requires for PCIe on your carrier.

## 6. PCIe Gen2 Routing Rules (5 GT/s)

- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Use serpentine length matching near the mismatch source.
- **Clearance:** keep ≥ 3W (better 5W) to other signals, and ≥ 2× h from the reference-plane edges.
- **Reference plane:** solid, unbroken L2 GND under the pairs. Do not cross plane splits.
- **AC coupling caps:** 100 nF (0201/0402) on the **TX** pairs only, placed symmetrically. Add a GND void under the pads if the pads are much wider than the trace.
- **Vias:** minimize them. If you must change layers, add GND stitching vias adjacent to each via pair.
- **Loss budget:** keep total length reasonable; under ~150 mm on FR-4 is comfortable for Gen2.

If you share your actual stackup (dielectric thicknesses, εr, copper weight, mask), I can recompute W/S precisely for 100 Ω or 85 Ω.