# PCIe Gen2 Differential Microstrip Impedance (Top Layer)

I don't have your board's actual stackup or what "Cycle 9" refers to, so the numbers below use a **common assumed stackup**. Swap in your fab's values and re-run, or better, use a field solver.

## ⚠️ Check the target impedance first

The PCIe base spec uses **85 Ω differential** for Gen2/Gen3. Many CM4-class module design guides (e.g., Raspberry Pi CM4) specify **90 Ω ±10%** for PCIe. Some boards use 100 Ω for stackup convenience, and Gen2 at 5 GT/s tolerates this over short runs. Before fixing on 100 Ω, confirm the requirement in the Radxa module's hardware design guide.

## Assumed stackup (typical 4-layer, 1.6 mm, e.g., JLC 7628)

| Parameter | Value |
|---|---|
| Dielectric height to L2 GND (H) | 0.2104 mm (8.28 mil) |
| Dielectric constant (Er) | 4.4 (7628 prepreg) |
| Copper thickness (T) | 1 oz finished ≈ 1.4 mil |
| Solder mask | Ignored (lowers Z by about 2–4 Ω) |

## Step 1: Single-ended Z₀ (Hammerstad, with thickness correction)

**Effective width:**
ΔW = (T/π)(1 + ln(2H/T)) = 0.446 × 3.47 ≈ 1.55 mil

For W = 10 mil, W_eff ≈ 11.55 mil, so W_eff/H ≈ 1.40.

**Effective dielectric constant:**
ε_eff = (Er+1)/2 + (Er−1)/2 · (1 + 12H/W)^−½ ≈ 3.21

**Single-ended impedance (W/H > 1 form):**
Z₀ = 377 / [√ε_eff · (W/H + 1.393 + 0.667 ln(W/H + 1.444))]
Z₀ = 377 / (1.793 × 3.484) ≈ **60.4 Ω**

## Step 2: Edge-coupled differential impedance

Z_diff ≈ 2Z₀ · (1 − 0.48 e^(−0.96·S/H))

| Width W | Space S | Z_diff |
|---|---|---|
| 10 mil | 8 mil | ~98 Ω |
| **10 mil** | **9 mil** | **~100 Ω** |
| 10 mil | 10 mil | ~103 Ω |

## Result (for the assumed stackup)

**W ≈ 10 mil (0.254 mm), S ≈ 9 mil (0.23 mm) gives ≈ 100 Ω differential.**

If you need 90 Ω instead, widen to about 12 mil with an 8 mil gap, or tighten the spacing.

These closed-form equations are only accurate to about ±5–10%. Before release:
1. Verify with a field solver (Saturn PCB Toolkit, Polar Si9000, or your fab's impedance calculator, such as JLCPCB's).
2. Include solder mask and the real etch trapezoid.
3. Specify controlled impedance on the fab order.

## PCIe Gen2 routing reminders

- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Match at the mismatch source.
- **AC coupling caps:** 100 nF (0402 or 0201) on the TX pair only, placed near the transmitter.
- **Reference plane:** keep a solid GND under the pairs, with no split crossings. Add GND stitching vias next to any layer-change vias.
- **Spacing to other signals:** ≥ 3W (about 30 mil) from other nets and between TX and RX pairs.
- **Polarity:** P/N may be swapped (PCIe supports polarity inversion), but don't swap TX and RX.
- **REFCLK:** route as a 100 Ω (or 85 Ω) diff pair with the same rules.

If you share your actual stackup (layer count, prepreg/core thicknesses, Er, copper weight), I can recalculate the exact W/S for 85, 90, or 100 Ω.