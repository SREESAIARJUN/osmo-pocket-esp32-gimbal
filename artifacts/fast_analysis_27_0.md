# 100 Ω Differential Microstrip for PCIe Gen2 on a CM4-Style Carrier (Top Layer)

I don't have your actual stackup or any document called "Cycle 27," so I can't give final dimensions for your board. Below is the method, a worked example on a common fab stackup, and what to confirm before routing.

## 1. Check the impedance target first

- The PCIe base spec uses **85 Ω ±15 %** differential.
- The Raspberry Pi CM4 hardware design guidance, which Radxa's CM4-compatible modules follow, calls for **90 Ω** differential on PCIe. 100 Ω is the usual target for Ethernet and HDMI.
- Check Radxa's design guide for your specific module (CM3, CM5, etc.). If it says 85 or 90 Ω, routing at 100 Ω adds avoidable mismatch, though Gen2 at 5 GT/s over a short carrier link usually tolerates it.

## 2. Formulas (IPC-2141 edge-coupled microstrip)

Single-ended:
Z₀ = 87 / √(εr + 1.41) × ln[5.98·h / (0.8·w + t)]

Differential:
Z_diff = 2·Z₀ × [1 − 0.48·e^(−0.96·s/h)]

Where:
- h = dielectric height from top copper to the reference plane (L2 GND)
- w = trace width
- s = edge-to-edge gap between the pair
- t = copper thickness

## 3. Worked example: typical 4-layer, 1.6 mm board (e.g. JLC 7628 stackup)

Assumed values:
- h = 0.2104 mm
- εr ≈ 4.4
- t = 0.035 mm (1 oz finished copper)
- s = 0.20 mm

| w (mm) | Z₀ (Ω) | Z_diff (Ω) |
|---|---|---|
| 0.20 | 67.3 | 108.6 |
| 0.22 | 64.4 | 104.0 |
| **0.24** | ~62 | **~100** |
| 0.25 | 60.5 | 97.7 |
| 0.29 | ~56 | ~90 |
| 0.30 | 54.9 | 88.6 |

Starting point for this stackup:
- **100 Ω:** about 0.24 mm wide with a 0.20 mm gap.
- **90 Ω:** about 0.29 mm wide with a 0.20 mm gap.

Limitations of these numbers:
- The IPC closed-form equations are only accurate to roughly ±5–10 %.
- Soldermask over the traces lowers Z_diff by about 2–5 Ω, so trim the width slightly to compensate.

## 4. Before committing

1. Get the exact stackup from your fab: prepreg type, pressed thickness, εr at about 2.5 GHz, and finished copper weight.
2. Re-run the calculation in a field solver such as Saturn PCB Toolkit, Polar Si9000, KiCad's calculator, or the fab's own impedance tool.
3. Order controlled impedance with coupons if the budget allows.

## 5. PCIe Gen2 layout reminders

- Intra-pair skew: ≤ 5 mil (0.127 mm). Match lengths near the source of the mismatch.
- AC coupling caps (typically 100 nF, 0201/0402) go on the TX side. Check whether the CM4 module already includes them.
- Keep a solid, unbroken GND reference under the pairs on L2. Do not route over plane splits.
- Use a GND via next to every signal-layer transition.
- Keep spacing to other signals at 3–5× the gap (s).
- Minimize vias and stubs.
- Route the REFCLK pair with the same impedance rules.

If you share your actual layer stackup (dielectric thicknesses, materials, copper weights) and which Radxa module you're using, I can calculate exact width and gap values for 85, 90, or 100 Ω.