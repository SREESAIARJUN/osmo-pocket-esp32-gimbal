# PCIe Gen2 Differential Microstrip Impedance (100 Ω Target)

I don't have your project's "Cycle 12" stackup or the Radxa carrier board fab notes. Below is the method and a worked example using a common 4-layer stackup. **Replace the inputs with your fab's actual values and confirm with a field solver before release.**

---

## 1. Confirm the Target First

- The PCIe Base Spec allows **85 Ω** differential, but many SoC and module vendors specify **85–100 Ω ±10%**.
- Check the Radxa CM module datasheet or hardware design guide (RK3566/RK3568/RK3588 family) for the required PCIe impedance.
- Use the module's number if it differs from 100 Ω. Matching the module's routing matters more than the spec nominal.

## 2. Inputs (Example: JLCPCB JLC04161H-7628, 1.6 mm, 4-layer)

| Parameter | Symbol | Example Value |
|---|---|---|
| Dielectric height (L1→L2 GND) | h | 0.2104 mm (7628 prepreg) |
| Dielectric constant | εr | 4.4 |
| Copper thickness (1 oz outer) | t | 0.035 mm |
| Trace width | w | solve for this |
| Pair gap (edge-to-edge) | s | 0.20 mm (chosen) |

## 3. Equations (IPC-2141 Edge-Coupled Microstrip)

**Single-ended impedance:**

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

**Differential impedance:**

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

## 4. Worked Calculation

**Constants:**
- 87 / √(4.4 + 1.41) = 87 / 2.410 = **36.10**
- 5.98 × 0.2104 = **1.258 mm**
- Coupling factor at s = 0.20 mm: s/h = 0.951, so e^(−0.913) = 0.4015
  - 1 − 0.48 × 0.4015 = **0.807**

**Iterating on trace width:**

| w (mm) | 0.8w + t | ln(ratio) | Z₀ (Ω) | Z_diff (Ω) |
|---|---|---|---|---|
| 0.20 | 0.195 | 1.864 | 67.3 | 108.7 |
| 0.22 | 0.211 | 1.786 | 64.5 | 104.1 |
| **0.24** | **0.227** | **1.713** | **61.8** | **≈ 99.8** |

**Result (example stackup): w ≈ 0.24 mm (9.4 mil), s = 0.20 mm (7.9 mil) → ~100 Ω differential.**

## 5. Corrections and Caveats

- **Solder mask** over the top-layer traces lowers impedance by about 2–4 Ω. You may need to narrow the trace slightly (~0.22–0.23 mm), or ask the fab to compensate.
- **Plating** makes outer copper thicker than 35 µm (often 45–50 µm). This lowers impedance slightly.
- **Trapezoidal etch** raises impedance slightly.
- **IPC-2141 accuracy** is about ±5–10%. Verify with a 2D field solver (Saturn PCB, Polar Si9000, KiCad's calculator, or your fab's online impedance calculator).
- **Order an impedance-controlled build** and specify "100 Ω ±10% differential, L1, referenced to L2." The fab will tune w and s to its actual process.

## 6. PCIe Gen2 Routing Checklist

- **Reference plane:** solid, unbroken GND on L2 under the pairs. Never route over splits.
- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Match length at the mismatch point.
- **Inter-pair matching:** TX and RX pairs don't need to match each other. REFCLK has its own pair rules.
- **AC coupling caps:** 0.1 µF (0201/0402) on TX lines, placed symmetrically near the transmitter or connector, with a GND void under the pads if needed.
- **Pair-to-pair spacing:** ≥ 3–5× h (roughly ≥ 0.6–1.0 mm) to other pairs and aggressors.
- **Vias:** minimize them. If used, place GND stitching vias adjacent to the signal vias.
- **Bends:** avoid 90° bends. Use 45° or arcs.

If you share your actual stackup (dielectric thickness, εr, copper weight, and mask), I can recompute the exact width and gap.