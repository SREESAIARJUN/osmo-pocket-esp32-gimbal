# PCIe Gen2 Differential Microstrip Calculation (Top Layer, 100 Ω Target)

**I don't have your actual Cycle 1 stackup**, so I can't give final numbers for your board. Below is the calculation method, worked for the two most common 4-layer, 1.6 mm stackups used on CM4 carriers. Substitute your fab's dielectric height (h), Er, and copper weight, then confirm with a field solver.

---

## 1. Method (IPC-2141 / Wadell edge-coupled microstrip)

**Single-ended:**

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

**Differential:**

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

Where:
- w = trace width
- s = edge-to-edge gap
- h = dielectric height from L1 to the L2 GND plane
- t = copper thickness

**Solder mask correction:** subtract about 2–4 Ω from Zdiff (the formula assumes bare copper in air).

---

## 2. Case A: Thick 7628 prepreg (e.g., JLC04161H-7628)

**Inputs:** h = 0.2104 mm (8.28 mil), Er ≈ 4.4, 1 oz outer copper (t ≈ 35 µm / 1.38 mil)

**Step 1, common factor:** √(4.4+1.41) = 2.410, so 87/2.410 = 36.10

**Step 2, trial geometries:**

| w (mm) | s (mm) | Z0 (Ω) | Zdiff bare (Ω) | Zdiff with mask (Ω) |
|---|---|---|---|---|
| 0.20 | 0.20 | 67.3 | 108.7 | ~105.5 |
| **0.23** | **0.20** | **63.1** | **101.8** | **~99** ✔ |
| 0.25 | 0.20 | 60.6 | 97.8 | ~95 |

**Worked example for w = 0.23 mm:**
- 0.8w + t = 7.25 + 1.38 = 8.63 mil
- 5.98h = 49.5 mil
- ln(49.5/8.63) = 1.747
- Z0 = 36.10 × 1.747 = 63.1 Ω
- s/h = 0.951, so e^(−0.913) = 0.401
- Coupling factor = 1 − 0.48 × 0.401 = 0.807
- Zdiff = 2 × 63.1 × 0.807 = **101.8 Ω bare, about 99 Ω under mask**

**Recommended: w = 0.23 mm (9 mil), s = 0.20 mm (8 mil)**

---

## 3. Case B: Thin 3313 prepreg (e.g., JLC04161H-3313)

**Inputs:** h = 0.0994 mm (3.91 mil), Er ≈ 4.1, 1 oz copper

**Worked example for w = 0.11 mm, s = 0.15 mm:**
- Common factor: 87/√5.51 = 37.07
- 0.8w + t = 3.46 + 1.38 = 4.84 mil
- ln(23.4/4.84) = 1.575
- Z0 = 58.4 Ω
- s/h = 1.51, so the coupling factor is 0.887
- Zdiff = **103.6 Ω bare, about 100 Ω under mask**

**Recommended: w = 0.11 mm (4.3 mil), s = 0.15 mm (6 mil)**

The thin stackup gives tighter coupling and narrower pairs, which makes breakout from the CM4 Hirose connectors easier.

---

## 4. Verification and notes

- **Accuracy:** IPC-2141 is only accurate to about ±5–7%. Confirm with a field solver (Saturn PCB Toolkit, Polar Si9000, or your fab's calculator, e.g., JLC's). Then specify **100 Ω ±10% controlled impedance** on the fab notes so the fab tunes the width.
- **Check the target:** PCIe's native reference is 85 Ω differential. 100 Ω pairs are commonly used and work well at Gen2 (5 GT/s) over short carrier traces. Still, check the Radxa/CM4 hardware design guide for the impedance it specifies for PCIe and match it.

## 5. PCIe Gen2 routing checklist for Cycle 1

- Intra-pair length match within about 5 mil (0.127 mm). Inter-pair matching is not required.
- Keep a continuous GND reference on L2 under the pairs, with no splits.
- Place GND stitching vias next to any layer-change vias.
- Place the 100 nF AC-coupling caps (0201/0402) on the TX lines, near the transmitter if they aren't already on the module.
- Keep 3W spacing (about 3× trace width) to other signals, and run REFCLK as its own 100 Ω pair.
- Avoid stubs and minimize vias; use 45° or arc bends.

If you send the actual Cycle 1 stackup (layer heights, Er, copper weight, mask thickness), I can recompute the exact width and gap for your board.