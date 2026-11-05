# 100 Ω Differential Microstrip for PCIe Gen2 TX/RX (Top Layer)

I don't have the actual Radxa CM4 carrier stackup, and I don't recognize "Cycle 20" (a design revision? a fab order?). The calculation below uses a **common 4‑layer 1.6 mm stackup** as a stand-in. Replace the inputs with your fab's stackup before releasing the design.

## 1. Check the target impedance first

- The PCIe Base Spec defines **85 Ω differential** as the nominal channel impedance.
- Many CM4 carrier designs (including the Raspberry Pi CM4 design guide) specify **90 Ω ±10%** for PCIe.
- **100 Ω** works for Gen2 over short carrier traces, but confirm it against Radxa's CM4 hardware design guide or reference schematic.

## 2. Assumed stackup (L1 microstrip over L2 GND)

| Parameter | Value |
|---|---|
| Dielectric height to L2 (h) | 0.21 mm (8.28 mil), 7628 prepreg |
| Dielectric constant (εr) | ~4.4 at 1–2.5 GHz |
| Copper thickness (t) | 1 oz finished ≈ 1.4 mil (0.035 mm) |
| Solder mask | ~0.5 mil LPI over the traces |

## 3. Calculation (IPC‑2141 closed-form)

**Single-ended impedance:**

Z₀ = 87 / √(εr + 1.41) · ln[5.98h / (0.8w + t)]

With w = 8 mil:

- 87 / √5.81 = 36.1
- 5.98 × 8.28 = 49.5
- 0.8 × 8 + 1.4 = 7.8
- ln(49.5 / 7.8) = 1.848
- **Z₀ ≈ 66.7 Ω**

**Differential impedance:**

Z_diff = 2Z₀ · [1 − 0.48·e^(−0.96·s/h)]

For Z_diff = 100 Ω:

- 1 − 0.48·e^(−0.96s/h) = 100 / 133.4 = 0.75
- e^(−0.96s/h) = 0.521
- **s ≈ 5.6 mil**

This closed-form result is approximate. IPC‑2141 tends to overestimate Z₀, and solder mask lowers impedance by about 2–4 Ω.

## 4. Field-solver-corrected recommendation

For the same stackup, a 2D field solver (Saturn PCB, Polar Si9000, or JLC's impedance calculator) gives approximately:

| Target | Trace width (w) | Gap (s) |
|---|---|---|
| **100 Ω diff** | **~5.5–6 mil (0.14–0.15 mm)** | **~7–8 mil (0.18–0.20 mm)** |
| 90 Ω diff (if Radxa specifies it) | ~7 mil | ~6–7 mil |
| 85 Ω diff (PCIe nominal) | ~8 mil | ~6 mil |

## 5. PCIe Gen2 layout rules to apply alongside the impedance

- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Match with small serpentines near the mismatch source.
- **Inter-pair matching:** not required for PCIe, since each lane deskews independently.
- **AC coupling:** 100 nF (0402 or 0201) caps on TX only, placed near the transmitter. Void the reference plane under the cap pads to reduce the capacitive dip.
- **Reference plane:** keep a solid L2 GND under the pairs, with no split-plane crossings.
- **Layer changes:** add GND stitching vias within ~1 mm of every signal via.
- **Spacing to other nets:** ≥ 3× trace width (≥ 20 mil), and ≥ 4–5w between TX and RX pairs.
- **REFCLK:** route as a 100 Ω pair with the same rules.

## Next step

Send me your fab's exact stackup:
- prepreg type and thickness,
- εr,
- copper weight,
- whether there is solder mask over the traces.

With those I can recompute w and s precisely. Then request **controlled-impedance fabrication at ±10%** so the fab fine-tunes the final widths.