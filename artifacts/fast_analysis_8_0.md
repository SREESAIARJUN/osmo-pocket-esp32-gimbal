# PCIe Gen2 Differential Microstrip Impedance (Top Layer)

I don't have your "Cycle 8" stackup, so the numbers below use an assumed common 4-layer stackup. Replace them with your fab's values. I've also flagged a target-impedance issue you should check first.

## 1. Check the 100 Ω target

- The **PCIe Base Spec** targets about **85 Ω** differential.
- **CM4-class carrier guidelines** (Raspberry Pi CM4 and Radxa CM3/CM5 derivatives) typically call for **90 Ω ±10%** on PCIe and USB. 100 Ω is used for HDMI and Ethernet.

100 Ω will usually still work at Gen2 (5 GT/s) over short carrier-board runs, but **90 Ω is the safer target**. Confirm against your module's hardware design guide. Both cases are calculated below.

## 2. Assumed stackup (example: 1.6 mm 4-layer, 7628 prepreg)

| Parameter | Value |
|---|---|
| Dielectric height to L2 GND, H | 0.2104 mm (8.28 mil) |
| Dielectric constant, Er | 4.4 |
| Copper thickness, T (1 oz finished) | 0.035 mm (1.38 mil) |
| Reference plane | Solid GND on L2, no splits under the pair |

## 3. Formulas (IPC-2141 approximations)

**Single-ended microstrip:**

Z₀ = [87 / √(Er + 1.41)] · ln[5.98H / (0.8W + T)]

**Edge-coupled differential:**

Z_diff = 2·Z₀ · [1 − 0.48·e^(−0.96·S/H)]

The common term is 87 / √5.81 = **36.10**.

## 4. Results

**For 100 Ω, with W = 8 mil and S = 6 mil:**
- 0.8W + T = 7.78 mil
- ln(49.51 / 7.78) = ln(6.364) = 1.851
- Z₀ = 36.10 × 1.851 = **66.8 Ω**
- Coupling factor: S/H = 0.725, so 1 − 0.48·e^(−0.696) = 0.761
- **Z_diff ≈ 2 × 66.8 × 0.761 ≈ 101.6 Ω** ✔

**For 90 Ω (recommended), with W = 10 mil and S = 6 mil:**
- 0.8W + T = 9.38 mil
- Z₀ = 36.10 × ln(5.278) = **60.1 Ω**
- **Z_diff ≈ 2 × 60.1 × 0.761 ≈ 91.4 Ω** ✔

| Target | Width W | Spacing S | Calculated Z_diff |
|---|---|---|---|
| 100 Ω | 8 mil (0.20 mm) | 6 mil (0.15 mm) | ~101 Ω |
| 90 Ω | 10 mil (0.25 mm) | 6 mil (0.15 mm) | ~91 Ω |

## 5. Corrections and verification

- **Solder mask** over the traces lowers impedance by roughly 2–4 Ω, so these results run slightly low in practice.
- **IPC-2141 accuracy** is only about ±5–10%. Verify with a field solver (Saturn PCB Toolkit, Polar Si9000, or your fab's calculator, e.g. JLCPCB) using the fab's actual prepreg thickness and Er.
- **Controlled impedance** should be specified on the fab drawing: "100 Ω (or 90 Ω) ±10% differential, L1 referenced to L2".

## 6. PCIe Gen2 routing rules

- **Intra-pair skew:** < 5 mil (0.127 mm). Use serpentine compensation near the mismatch source.
- **Inter-pair skew:** not critical for x1, but keep TX and RX lengths reasonable.
- **AC coupling:** 100 nF (0402 or smaller) caps on TX only, placed near the transmitter. Void GND under the pads if the pads are much wider than the trace.
- **Spacing:** keep ≥ 3W (or ≥ 20 mil) to other signals, with GND stitching vias at layer transitions.
- **Reference plane:** route over continuous GND only, with no crossing of plane splits.

If you share your actual stackup (layer count, prepreg type and thickness, Er, copper weight), I can recompute the exact W/S.