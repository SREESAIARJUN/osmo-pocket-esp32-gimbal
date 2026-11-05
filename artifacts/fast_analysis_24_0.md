# PCIe Gen2 Differential Microstrip (100 Ω Target): Top Layer

**I don't have the actual stackup for your Radxa CM4 carrier board (Cycle 24).** The result depends entirely on dielectric height, Er, and copper thickness. Below is the method, applied to a common 4-layer 1.6 mm stackup as a worked example. Substitute your fab's numbers.

---

## 1. Assumed Stackup (example: JLCPCB JLC7628, 4-layer, 1.6 mm)

| Parameter | Symbol | Value |
|---|---|---|
| Dielectric height (L1→L2 GND) | h | 0.2104 mm (8.28 mil) |
| Dielectric constant (7628) | Er | ~4.4 |
| Outer copper (1 oz finished) | t | 0.035 mm (1.4 mil) |
| Reference plane | — | Solid GND on L2 |

---

## 2. Formulas (IPC-2141 approximations)

**Single-ended microstrip:**

$$Z_0 = \frac{87}{\sqrt{E_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

**Edge-coupled differential:**

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

---

## 3. Calculation

**Try w = 8 mil:**

- √(4.4 + 1.41) = 2.410, so 87 / 2.410 = 36.1
- 0.8(8) + 1.4 = 7.8 mil
- 5.98 × 8.28 = 49.5 mil
- ln(49.5 / 7.8) = ln(6.35) = 1.848
- **Z₀ ≈ 66.7 Ω**

**Coupling with s = 6 mil:**

- s/h = 0.725
- e^(−0.696) = 0.499
- 1 − 0.48 × 0.499 = 0.761
- Z_diff = 133.4 × 0.761 ≈ **101.5 Ω**

**Sensitivity to spacing:**

| w (mil) | s (mil) | Z_diff (Ω) |
|---|---|---|
| 8 | 5 | ~97 |
| 8 | 6 | ~101 |
| 8 | 8 | ~108 |

**Starting geometry: w ≈ 8 mil (0.20 mm), s ≈ 6 mil (0.15 mm).**

---

## 4. Corrections Before Committing

1. **Solder mask:** LPI mask (~0.5–1 mil, Er ~3.5) lowers Z_diff by about 2–5 Ω. You may need to tighten the trace slightly or widen the gap.
2. **IPC-2141 accuracy:** The formula is only good to about ±5–10%. Verify with a 2D field solver: Saturn PCB Toolkit, Polar Si9000, KiCad's calculator, or your fab's online impedance tool (JLC's uses their exact prepreg data).
3. **Request controlled impedance** from the fab. They will tweak w/s to hit 100 Ω ±10%.

---

## 5. Check Your Target Impedance

Confirm 100 Ω is correct before routing. PCIe nominally specifies **85 Ω differential**, though 85–100 Ω is generally tolerated at Gen2 rates. Check the Radxa CM module's hardware design guide for its required value, and match the impedance used on the module side of the connector.

---

## 6. PCIe Gen2 Routing Rules (5 GT/s)

- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Match with small serpentines near the mismatch source.
- **Inter-pair matching (TX vs RX):** Not required. Lanes are independently deskewed.
- **AC coupling caps:** 100 nF (0201/0402) on TX lines, placed near the transmitter or connector per the host guide.
- **Reference plane:** Continuous GND under the pair. Don't cross splits. Add GND stitching vias next to any signal-via layer transitions.
- **Pair-to-pair spacing:** ≥ 3× the in-pair gap (about 20 mil), and wider from other aggressors.
- **Via count:** Minimize, ideally ≤ 2 per line, and keep any vias symmetric within the pair.
- **REFCLK pair:** Route as 100 Ω differential with the same rules.

---

If you share your actual stackup (fab, layer count, prepreg type/thickness, copper weight), I can recompute the exact w/s for your board.