# 100 Ω Differential Microstrip for PCIe Gen2 (Top Layer)

I don't have your board's actual stackup ("Cycle 16" isn't something I can look up), so the numbers below use a **typical 4-layer, 1.6 mm stackup**. Replace the inputs with your fab's values and confirm in a field solver before routing.

## 1. Inputs

| Parameter | Assumed value |
|---|---|
| Structure | Edge-coupled surface microstrip, L1 over L2 GND |
| Dielectric height *h* (L1→L2 prepreg, 7628) | 0.21 mm (8.3 mil) |
| εr | 4.4 |
| Copper *t* (1 oz + plating) | ~35 µm (1.4 mil) |
| Target Z_diff | 100 Ω ±10% |

## 2. Single-ended impedance (Hammerstad, thickness-corrected)

Try **W = 10 mil**. With the thickness correction, W_eff ≈ 10.8 mil, so W/h ≈ 1.30.

**Effective permittivity**

ε_eff = (εr+1)/2 + (εr−1)/2 · (1 + 12h/W)^(−½)
= 2.70 + 1.70 × 0.313
≈ **3.23**

**Characteristic impedance** (formula valid for W/h ≥ 1)

Z₀ = 120π / [√ε_eff · (W/h + 1.393 + 0.667·ln(W/h + 1.444))]
= 209.8 / (1.30 + 1.393 + 0.674)
≈ **62 Ω**

## 3. Differential impedance

Try **S = 8 mil**, so s/h ≈ 0.97.

Z_diff ≈ 2Z₀ · (1 − 0.48·e^(−0.96·s/h))
= 124.6 × (1 − 0.48 × 0.396)
≈ **101 Ω**

## 4. Result

| Stackup | Trace W | Gap S | Z_diff |
|---|---|---|---|
| 7628 prepreg, h ≈ 8.3 mil | ~10 mil (0.25 mm) | ~8 mil (0.20 mm) | ≈ 100 Ω |
| 3313 prepreg, h ≈ 3.9 mil (thin) | ~4.5–5 mil | ~7 mil | ≈ 100 Ω (estimate) |

**Solder mask** over the traces lowers Z_diff by about 2–5 Ω. Narrow W by roughly 0.5 mil, or widen S, to compensate.

## 5. PCIe Gen2 routing notes

- **Check the target impedance.** The PCIe base spec is built around 85 Ω differential. Many CM4 carrier designs use 85–90 Ω, so confirm against the Radxa/CM4 hardware design guide before committing to 100 Ω.
- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Use serpentine compensation near the mismatch source.
- **AC coupling:** 100 nF caps on the TX pairs only, placed close to the transmitter. Use 0402 or smaller pads and void the reference plane under them if needed.
- **Reference plane:** keep a continuous GND under the pair, with no split crossings.
- **Vias:** minimize them. Add GND stitching vias next to any signal via.
- **Spacing to other nets:** ≥ 3W, or ≥ 20 mil to other high-speed pairs.
- **Polarity inversion** within a pair is allowed by PCIe. Lane reversal depends on the controller.

## 6. Verify

- Run your fab's exact stackup through a field solver: Saturn PCB Toolkit, the JLCPCB/PCBWay impedance calculators, Polar SI9000, or the KiCad/Altium stackup manager.
- Order **controlled-impedance** fabrication with a TDR coupon.

If you share your actual stackup (dielectric height, εr, copper weight, and solder mask), I can recompute W and S specifically for it.