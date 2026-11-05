# PCIe Gen2 Differential Microstrip Calculation (100 Ω, Top Layer)

I don't have your "Cycle 21" stackup data, so the calculation below uses a **typical 4-layer, 1.6 mm stackup** (JLCPCB JLC04161H-7628 style). Replace the inputs with your fab's actual values and rerun the same steps.

## 1. Assumed Inputs

| Parameter | Symbol | Value |
|---|---|---|
| Dielectric height (L1 to L2 GND, 7628 prepreg) | h | 0.2104 mm (8.28 mil) |
| Copper thickness (1 oz finished) | t | 0.035 mm (1.38 mil) |
| Dielectric constant | εr | 4.4 |
| Target differential impedance | Zdiff | 100 Ω ±10% |

## 2. Formulas (IPC-2141)

Single-ended microstrip:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Edge-coupled differential:

$$Z_{diff} \approx 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

## 3. Solve for Trace Width

The constant term is 87 / √(4.4 + 1.41) = 87 / 2.410 = **36.1**.

**Option A: spacing s = 0.20 mm (7.9 mil)**
- Coupling factor = 1 − 0.48·e^(−0.96 × 0.20 / 0.2104) = 1 − 0.48 × 0.4015 = 0.807
- Required Z0 = 100 / (2 × 0.807) = 61.9 Ω
- ln(5.98h / (0.8w + t)) = 61.9 / 36.1 = 1.715, so 5.98h / (0.8w + t) = 5.556
- 0.8w + 0.035 = 1.258 / 5.556 = 0.2264, so **w ≈ 0.24 mm (9.4 mil)**

**Option B: spacing s = 0.15 mm (5.9 mil)**
- Coupling factor = 0.758, so the required Z0 = 65.9 Ω
- This gives **w ≈ 0.21 mm (8.3 mil)**

## 4. Result

| Geometry | Width | Gap | Zdiff (est.) |
|---|---|---|---|
| A | 0.24 mm / 9.4 mil | 0.20 mm / 7.9 mil | ~100 Ω |
| B | 0.21 mm / 8.3 mil | 0.15 mm / 5.9 mil | ~100 Ω |

IPC-2141 closed-form equations are only accurate to about ±5–10%. Solder mask typically lowers impedance by 2–4 Ω. **Verify with a field solver** (Saturn PCB, Polar SI9000, or your fab's impedance calculator) and order controlled-impedance fabrication.

## 5. Important Design Notes

- **Check the impedance target.** The PCIe specification calls for **85 Ω** differential for Gen2 and later. Many CM4-class carrier designs use **85–90 Ω** for PCIe, while 100 Ω is commonly used for Ethernet and HDMI. Confirm against the Radxa CM4 / Raspberry Pi CM4 hardware design guide before committing. For 90 Ω with Option A spacing, widen the trace to roughly 0.29 mm.
- **Length matching:**
  - Intra-pair (P/N): within ≤5 mil (0.127 mm).
  - Inter-pair: TX and RX do not need to match each other.
  - REFCLK pair: also 100/85 Ω, matched within the pair.
- **AC coupling:** Place 100 nF (0402/0201) capacitors on the TX lines near the transmitter. Use symmetric pad placement and consider a GND void under the pads.
- **Reference plane:** Route over a solid, unbroken GND on L2. Avoid crossing plane splits.
- **Spacing to other signals:** Keep at least 3× the gap (≥ 3s, ideally 5h) from other signals.
- **Vias:** Use stitching GND vias next to any layer-change vias. Minimize stubs.
- **Bends:** Use 45° or arc bends. Place serpentine length tuning near the mismatch source.

If you share your actual stackup (fab, prepreg type, copper weight, εr), I can recompute the exact width and gap.