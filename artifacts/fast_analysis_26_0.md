# PCIe Gen2 Differential Microstrip Impedance (Top Layer)

I don't have the actual stackup for your "Radxa CM4 carrier board (Cycle 26)", so the numbers below use an **assumed typical 4-layer 1.6 mm stackup**. Send me your fab's stackup and I'll recompute with your values.

## 1. Check the target impedance first

- The **PCIe Base Spec (Gen2/Gen3)** uses an **85 Ω** differential reference. 100 Ω is widely used and generally works, but it is not the spec nominal.
- **Check the module's own design guidelines.** The Raspberry Pi CM4 datasheet (and Radxa's equivalent for its CM-format modules) states a recommended PCIe impedance. Match the module's internal routing to avoid a discontinuity at the connector.

I calculate both 100 Ω and 85 Ω below.

## 2. Assumed stackup (top layer, L1 over L2 GND)

| Parameter | Value |
|---|---|
| Dielectric height H (L1→L2, 7628 prepreg) | 0.21 mm |
| Dielectric constant εr (FR-4) | 4.4 |
| Copper thickness T (1 oz finished) | 0.035 mm |
| Reference plane | Solid GND on L2 |

## 3. Formulas (IPC-2141 / Wadell approximation)

Single-ended edge-coupled microstrip:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}} \ln\!\left(\frac{5.98H}{0.8W + T}\right)$$

Differential:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,S/H}\right)$$

## 4. Calculation for 100 Ω (gap S = 0.20 mm)

- Prefactor: 87 / √5.81 = **36.1**
- Coupling factor: 1 − 0.48·e^(−0.96·0.952) = **0.807**

| W (mm) | Z₀ (Ω) | Z_diff (Ω) |
|---|---|---|
| 0.20 | 67.3 | 108.6 |
| 0.22 | 64.4 | 104.0 |
| **0.24** | **61.8** | **≈ 99.8** ✅ |

**Result: W ≈ 0.24 mm, S ≈ 0.20 mm gives about 100 Ω.**

For 85 Ω with the same gap, W ≈ 0.32 mm gives Z_diff ≈ 85 Ω.

## 5. Corrections and caveats

- **Soldermask** over the traces typically lowers Z_diff by about 2–5 Ω. You may need to narrow W slightly (for example to about 0.22 mm).
- The IPC formulas are only accurate to about ±5–10%. Verify with a field solver (Saturn PCB Toolkit, Polar Si9000, or your fab's impedance calculator).
- **Order impedance control** from the fab and specify "100 Ω ±10% differential, L1, ref L2". The fab will adjust the final W/S to their actual materials.

## 6. PCIe Gen2 layout rules to pair with this

- Intra-pair length match: ≤ 5 mil (0.127 mm).
- AC coupling caps on TX (typically 100 nF, 0201/0402), placed near the transmitter.
- Keep a continuous GND reference under the pairs. Do not cross plane splits.
- Use GND stitching vias next to any signal-layer transitions.
- Keep pair-to-pair and pair-to-other-signal spacing at ≥ 3× the trace width (≥ 5H preferred for TX vs. RX).
- Minimize vias. Back-drill or keep stubs short if layer changes are needed.

If you share the actual prepreg thickness, εr, copper weight, and soldermask spec for the Cycle 26 build, I can give you the exact W/S.