# 100 Ω Differential Microstrip for PCIe Gen2 (Top Layer)

## I don't have this board's stackup

I don't have access to the stackup for the Radxa CM4 carrier board or a "Cycle 22" revision. That may be an internal or project-specific designation. The trace geometry depends entirely on the dielectric height, Er, copper weight and solder mask. Please get the actual stackup from the design files or your fab house.

Below is the method, worked through on a common example stackup so you can substitute your real numbers.

## PCIe Gen2 Requirements

| Parameter | Target |
|---|---|
| Differential impedance | 100 Ω ±10% (85 Ω is also allowed by spec, but 100 Ω is typical for CM4-style designs) |
| Data rate | 5 GT/s (Nyquist 2.5 GHz) |
| Intra-pair skew | < 5 mil (0.127 mm) |
| AC coupling caps | On TX side, 100 nF (0201/0402) |

## Formulas (IPC-2141, edge-coupled microstrip)

**Single-ended:**
Z₀ = 87 / √(Er + 1.41) × ln[5.98·H / (0.8·W + T)]

**Differential:**
Z_diff = 2·Z₀ × [1 − 0.48·e^(−0.96·S/H)]

Where:
- H = dielectric height to the reference plane
- W = trace width
- T = copper thickness
- S = edge-to-edge gap

## Worked Example

This uses a typical 4-layer, 1.6 mm board (JLC04161H-7628-style):

- H = 0.2104 mm (7628 prepreg, L1→L2 GND)
- Er = 4.4
- T = 0.035 mm (1 oz finished outer copper)

| W (mm) | S (mm) | Z₀ (Ω) | Z_diff (Ω) |
|---|---|---|---|
| 0.20 | 0.20 | 67.3 | 108.7 |
| 0.22 | 0.18 | 64.5 | 101.8 |
| **0.23** | **0.18** | **63.1** | **≈99.6 ✓** |

**Result for this example:** W ≈ 0.23 mm (9 mil), S ≈ 0.18 mm (7 mil).

## Important Caveats

1. **Formula accuracy.** IPC-2141 closed-form equations are only accurate to about ±5–10%. Verify with a field solver such as Saturn PCB Toolkit, Polar Si9000, or your fab's impedance calculator (JLCPCB has a free one).
2. **Solder mask.** Mask over the traces typically lowers Z_diff by 2–5 Ω. Field solvers account for this; the formula above does not.
3. **Fab control.** Order with controlled impedance so the fab tunes the width to their actual laminate.
4. **Reference plane.** Keep an unbroken GND plane on L2 under the entire pair. Do not route across splits.
5. **Pair-to-pair spacing.** Keep at least 3×W (better, 5×H) between TX/RX pairs and other signals.

If you share your actual stackup (layer count, prepreg type/thickness, Er, copper weight), I can calculate the specific width and gap.