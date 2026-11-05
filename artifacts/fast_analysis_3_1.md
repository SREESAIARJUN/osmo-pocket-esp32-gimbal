# PCIe Gen2 Differential Microstrip Impedance: Top Layer

I don't have your Cycle 3 stackup, so the numbers below use a **common 4-layer, 1.6 mm stackup (JLC04161H-7628 style)**. Replace the inputs with your fab's actual values and re-run the same formulas.

## 1. Check the target impedance first

**100 Ω is probably not the right target for PCIe.**

| Source | PCIe differential target |
|---|---|
| PCIe Base Spec (Gen2/Gen3 guidance) | **85 Ω ±15%** |
| Raspberry Pi CM4 design guidelines (followed by Radxa CM-series carriers) | **90 Ω ±10%** |
| 100 Ω | Used for HDMI, MIPI, Ethernet MDI. Not typical for PCIe. |

A 100 Ω PCIe pair will usually still link at Gen2 over short carrier-board runs, but it adds return-loss margin risk. I recommend **90 Ω** to match the CM4 module side. All three targets are calculated below.

## 2. Assumed stackup (top layer, L1 referenced to L2 GND)

| Parameter | Value |
|---|---|
| Dielectric height *h* (7628 prepreg) | 0.21 mm |
| εr | 4.4 |
| Copper thickness *t* (1 oz finished) | 0.035 mm |
| Reference plane | Solid GND on L2, no splits under the pair |

## 3. Formulas (IPC-2141 / Hammerstad approximations)

Single-ended microstrip:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Edge-coupled differential:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

With these inputs, the prefactor is 87/√5.81 ≈ 36.1.

## 4. Worked calculation for 100 Ω

Try a trace width of *w* = 0.24 mm and a spacing of *s* = 0.20 mm.

**Single-ended impedance:**
- 0.8·0.24 + 0.035 = 0.227
- 5.98·0.21 = 1.256
- ln(1.256 / 0.227) = ln(5.53) = 1.71
- Z₀ = 36.1 × 1.71 ≈ **61.8 Ω**

**Coupling factor:**
- *s/h* = 0.952
- 0.96 × 0.952 = 0.914
- e^(−0.914) = 0.401
- 1 − 0.48 × 0.401 = 0.808

**Differential impedance:**
- Z_diff = 2 × 61.8 × 0.808 ≈ **99.9 Ω** ✔

## 5. Results summary

| Target | Width *w* | Gap *s* | Calculated Z_diff |
|---|---|---|---|
| 100 Ω | 0.24 mm (9.4 mil) | 0.20 mm (7.9 mil) | ~100 Ω |
| **90 Ω (recommended)** | 0.25 mm (9.8 mil) | 0.15 mm (5.9 mil) | ~92 Ω |
| 85 Ω (PCIe spec) | 0.28 mm (11 mil) | 0.15 mm (5.9 mil) | ~86 Ω |

**Solder mask effect:** LPI mask over the traces typically lowers Z_diff by about 2–4 Ω. Narrow *w* by about 0.01 mm to compensate, or use your fab's calculator, which accounts for mask.

## 6. PCIe Gen2 layout rules to apply alongside

- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Match within each TX and RX pair.
- **Inter-pair matching:** Not required between TX and RX pairs.
- **AC coupling caps:** 100 nF (0201 or 0402) on the TX pairs, placed near the transmitter. Add a GND-plane cutout under the pads if the stackup allows.
- **Clearance:** ≥ 3× *h* (about 0.6 mm) to other signals, and ≥ 5× *h* to clocks.
- **Vias:** Minimize layer changes. If you must change layers, place a GND stitching via within about 0.5 mm of each signal via.
- **Reference plane:** Never route over plane splits or gaps on L2.
- **REFCLK pair:** Same 85–100 Ω rules apply. Keep it away from TX/RX pairs.

## 7. Verify before fab

1. Get your fab's exact stackup for Cycle 3: prepreg type, pressed thickness, εr at about 2.5 GHz, and finished copper.
2. Re-run the numbers in a field solver (Saturn PCB Toolkit, Polar Si9000, or the JLC/PCBWay impedance calculator). The closed-form formulas above are accurate to roughly ±5%.
3. Order controlled-impedance fabrication with a TDR coupon, and specify the target as "90 Ω ±10% differential, L1 ref L2".

If you send the actual Cycle 3 stackup (layer thicknesses, material, copper weight), I can recompute exact width and gap values.