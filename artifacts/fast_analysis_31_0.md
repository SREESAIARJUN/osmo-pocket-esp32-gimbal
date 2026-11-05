# PCIe Gen2 Differential Microstrip Impedance (100 Ω Target, Top Layer)

I don't have the board's stackup or whatever "Cycle 31" refers to, so this uses an assumed stackup. The method below applies directly once you substitute the fab's actual numbers.

## 1. Assumed Stackup

This is a typical 4-layer, 1.6 mm board, similar to JLCPCB's JLC7628 stackup.

| Parameter | Symbol | Value |
|---|---|---|
| Dielectric height (L1 to L2 GND) | h | 0.2104 mm (7628 prepreg) |
| Dielectric constant | εr | 4.4 |
| Copper thickness (1 oz finished) | t | 0.035 mm |
| Solder mask | — | ~15–20 µm LPI, εr ≈ 3.5 |

L2 must be a solid, unbroken GND reference under the pairs.

## 2. Equations (IPC-2141 Edge-Coupled Microstrip)

Single-ended impedance:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}}\ \ln\!\left(\frac{5.98h}{0.8w + t}\right)$$

Differential impedance:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

## 3. Calculation (w = 0.20 mm, s = 0.15 mm)

**Single-ended Z₀:**
- Effective width term: 0.8(0.20) + 0.035 = 0.195 mm
- Height term: 5.98 × 0.2104 = 1.258 mm
- ln(1.258 / 0.195) = ln(6.45) = 1.864
- Dielectric term: 87 / √(4.4 + 1.41) = 87 / 2.410 = 36.10
- **Z₀ = 36.10 × 1.864 ≈ 67.3 Ω**

**Coupling factor:**
- s/h = 0.15 / 0.2104 = 0.713
- e^(−0.96 × 0.713) = e^(−0.684) = 0.505
- 1 − 0.48 × 0.505 = 0.758

**Differential impedance:**
- **Z_diff = 2 × 67.3 × 0.758 ≈ 102 Ω** (bare microstrip)
- Solder mask typically lowers this by about 2–4 Ω, giving **about 99 Ω**, which is within the ±10% tolerance.

## 4. Sensitivity (Same Stackup)

| w (mm) | s (mm) | Z_diff (no mask) | Approx. with mask |
|---|---|---|---|
| 0.15 | 0.20 | 122 Ω | ~118 Ω |
| **0.20** | **0.15** | **102 Ω** | **~99 Ω ✔** |
| 0.25 | 0.15 | 92 Ω | ~89 Ω |
| 0.28 | 0.13 | 84 Ω | ~81 Ω |

## 5. Important Notes

1. **Check the target impedance.** The PCIe base spec nominally uses **85 Ω** differential, and the Raspberry Pi CM4 datasheet/design guide recommends a specific value (85–90 Ω) for its PCIe lanes. Confirm against the Radxa or CM4 hardware design guide before committing to 100 Ω. If 85 Ω is required, use about w = 0.27 mm and s = 0.14 mm on this stackup.
2. **The IPC formulas are accurate to only about ±5–8%.** Verify with a 2D field solver (Saturn PCB Toolkit, Polar Si9000, or the fab's impedance calculator), and request impedance-controlled fabrication with a coupon.
3. **PCIe Gen2 routing rules (5 GT/s):**
   - Intra-pair skew: ≤ 5 mil (0.127 mm)
   - Place the AC coupling caps (100 nF, 0201/0402) on the TX side, near the transmitter or connector, as per the spec.
   - Keep pair-to-pair spacing ≥ 3× s (≥ 4× h is better), and separate TX and RX pairs.
   - Avoid crossing reference-plane splits. If a pair changes layers, place GND stitching vias next to the signal vias.
   - Match REFCLK pairs to the same impedance.

If you send the actual stackup (layer heights, εr, copper weight, and whether it's 1 oz or 0.5 oz plus plating), I'll recompute the exact width and spacing.