# PCIe Gen2 Differential Microstrip: Top Layer Calculation

I don't have the actual "Cycle 7" stackup for your Radxa CM4 carrier board. The calculation below uses a **common 4-layer 1.6 mm stackup (JLCPCB-style 7628 prepreg)** as a worked example. Replace the inputs with your fab's numbers before release.

## 1. Check the Target Impedance First

100 Ω may not be the right target for PCIe:

| Interface | Typical target |
|---|---|
| PCIe (CEM / most SoC guides) | **85 Ω** diff (±10–15%) |
| RPi CM4-class carrier guidelines (PCIe, USB) | **90 Ω** diff |
| Ethernet MDI, HDMI, MIPI CSI/DSI | 100 Ω diff |

Check the Radxa CM design guide and the SoC hardware guide for the module you're using. Below I give 100 Ω as requested, plus 90 Ω and 85 Ω alternatives.

## 2. Assumed Stackup (Top Layer, L1 over L2 GND)

| Parameter | Value |
|---|---|
| Dielectric height *h* (L1 to L2) | 0.2104 mm (7628 prepreg) |
| εr | 4.4 |
| Copper *t* | 0.035 mm (1 oz finished) |
| Reference | Solid GND on L2, no splits under the pair |

## 3. Equations (IPC-2141 approximation)

Single-ended:
Z₀ = 87 / √(εr + 1.41) · ln[5.98h / (0.8w + t)]

Edge-coupled differential:
Z_diff = 2Z₀ · (1 − 0.48·e^(−0.96·s/h))

With εr = 4.4: 87/√5.81 = **36.1**

## 4. Solving for 100 Ω

Try **w = 0.25 mm**:
- 0.8w + t = 0.235
- 5.98h = 1.258
- ln(5.353) = 1.678
- **Z₀ ≈ 60.6 Ω**

Then **s = 0.22 mm**:
- s/h = 1.046
- e^(−1.004) = 0.366
- Z_diff = 121.2 × (1 − 0.176)
- **Z_diff ≈ 99.9 Ω** ✔

## 5. Results Summary

| Target | Trace width *w* | Gap *s* | Z₀ (SE) | Z_diff |
|---|---|---|---|---|
| **100 Ω** | 0.25 mm (9.8 mil) | 0.22 mm (8.7 mil) | ~61 Ω | **~100 Ω** |
| 90 Ω | 0.30 mm (11.8 mil) | 0.22 mm | ~55 Ω | ~90 Ω |
| 85 Ω | 0.30 mm | 0.18 mm | ~55 Ω | ~87 Ω |

Solder mask over the traces typically lowers the impedance by **2–4 Ω**. You may need to narrow the trace by about 0.01–0.02 mm to compensate.

## 6. Verification and Layout Rules for PCIe Gen2 (5 GT/s)

- **Verify with a field solver.** Use Saturn PCB, Polar SI9000, or the JLC/your fab's impedance calculator. IPC-2141 can be off by ±5–10%. Request controlled impedance (±10%) on the fab order.
- **Intra-pair skew:** ≤ 5 mil (0.127 mm). Match lengths near the mismatch source.
- **Inter-pair skew** (TX vs RX): not critical, but keep each pair ≤ ~150 mm total channel.
- **AC coupling caps** on TX (typically 100 nF, 0201/0402) placed near the transmitter. Use symmetric pads, and consider a GND void on L2 under the pads.
- **Spacing to other signals:** ≥ 3× *w* (or ≥ 5*h*). Keep TX and RX pairs separated.
- **Return path:** no plane splits under the pair. Add GND stitching vias next to every signal via on a layer change.
- **Routing:** minimize vias (≤ 2 per lane), avoid 90° bends, and keep the REFCLK pair at 100 Ω (85 Ω acceptable per CEM).

If you share the actual Cycle 7 stackup (prepreg thickness, εr, copper weight, mask), I can recompute exact w/s values.