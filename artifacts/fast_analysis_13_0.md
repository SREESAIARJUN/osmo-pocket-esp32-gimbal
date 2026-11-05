# PCIe Gen2 Differential Microstrip Impedance (Top Layer)

I don't have the actual stackup for the Radxa CM4 carrier board or the "Cycle 13" revision. Impedance depends entirely on dielectric height, Er, and copper weight, so I can't give you the board's real trace geometry. Below is the method, a worked example using a common assumed stackup, and what you need to finalize it.

## ⚠️ Check the target impedance first
- **The PCIe spec nominally targets 85 Ω differential**, not 100 Ω.
- **The Raspberry Pi CM4 design guidance (which Radxa CM4-compatible carriers typically follow) specifies about 90 Ω ±10% for PCIe pairs**, as I recall. Check the CM4 datasheet's routing section to confirm.
- 100 Ω is usually used for Ethernet, HDMI and MIPI. A 100 Ω PCIe link will often work at Gen2 (5 GT/s), but verify which target your design actually uses.

## Assumed stackup (example: typical 4-layer, 1.6 mm, 7628 prepreg)
| Parameter | Value |
|---|---|
| Dielectric height H (L1→L2 GND) | 0.21 mm (8.28 mil) |
| Er (7628 @ ~1–2.5 GHz) | ~4.4 |
| Outer copper T (1 oz) | 35 µm (1.38 mil) |
| Reference plane | Solid GND on L2, no splits under pairs |

## Equations (IPC-2141 approximation)
Single-ended microstrip:
$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98H}{0.8W + T}\right)$$

Edge-coupled differential:
$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,S/H}\right)$$

## Worked example (W = 8 mil, S = 6 mil)
1. 87 / √(4.4+1.41) = 87 / 2.410 = **36.1**
2. 5.98 × 8.28 / (0.8×8 + 1.38) = 49.5 / 7.78 = 6.36, so ln = 1.851
3. **Z₀ ≈ 66.8 Ω**
4. S/H = 0.725, so e^(−0.696) = 0.499, giving coupling factor 1 − 0.239 = 0.761
5. **Z_diff ≈ 2 × 66.8 × 0.761 ≈ 101.7 Ω**

**Result: about 100 Ω at W ≈ 8 mil (0.20 mm), S ≈ 6 mil (0.15 mm)** for this assumed stackup.

For comparison, the same geometry with W = 7 mil gives about 107.6 Ω, so increasing width lowers impedance. To reach 90 Ω, widen to about 9.5–10 mil or tighten the gap.

## Caveats
- **IPC-2141 is only accurate to ±5–10%**, and is worse when S/H < 1. Confirm with a field solver such as Saturn PCB Toolkit, Polar Si9000, or your fab's calculator (JLCPCB and PCBWay have free ones).
- **Solder mask lowers the impedance by about 2–5 Ω.** Include it in the solver.
- **Ask your fab for a controlled-impedance build** with their exact stackup, and let them tweak the width.

## PCIe Gen2 routing reminders
- Intra-pair length match within about 5 mil (0.127 mm).
- Place AC coupling caps (100 nF, 0201/0402) on the TX lines near the transmitter.
- Keep spacing to other signals of at least 3× the dielectric height (H).
- Avoid vias. If you must use them, use them symmetrically and add GND stitching vias.
- Don't cross plane splits.
- Keep the REFCLK pair at the same impedance target.

If you share the actual stackup (layer heights, material, copper weight) and target impedance, I can compute the exact width and spacing.