# PCIe Gen2 Differential Microstrip (100 Ω) – Top Layer

I don't have your board's stackup or what "Cycle 23" refers to, so the numbers below use common 4-layer stackups. Replace them with your fab's actual values and confirm in a field solver.

## 1. Check the impedance target first

PCIe Gen2 interconnects are usually specified at **85 Ω differential**, with 100 Ω tolerated in many designs. The Raspberry Pi CM4 hardware design guide, which Radxa CM-format boards follow, gives its own target for the PCIe pairs. Confirm whether your design should use **85, 90, or 100 Ω** before you lock in geometry. The calculation below uses 100 Ω as you asked.

## 2. Method (IPC-2141 approximation)

Single-ended edge-coupled microstrip:

$$Z_0 = \frac{87}{\sqrt{\varepsilon_r + 1.41}}\ \ln\!\left(\frac{5.98\,h}{0.8\,w + t}\right)$$

Differential:

$$Z_{diff} = 2Z_0\left(1 - 0.48\,e^{-0.96\,s/h}\right)$$

Where:
- h = dielectric height to the L2 reference plane
- w = trace width
- t = copper thickness
- s = edge-to-edge gap

## 3. Results for typical stackups

Common to both cases: εr ≈ 4.4, 1 oz outer copper (t ≈ 0.035 mm), solid GND on L2.

**Case A: thin prepreg (3313-type, h ≈ 0.10 mm)**
- w = 0.11 mm, s = 0.15 mm
- Z₀ = 87/2.41 × ln(0.594/0.123) ≈ **56.9 Ω**
- Coupling factor = 1 − 0.48·e^(−1.45) ≈ 0.887
- **Z_diff ≈ 101 Ω** → about **4.3 mil / 6 mil (w/s)**

**Case B: thick prepreg (7628-type, h ≈ 0.21 mm)**
- w = 0.24 mm, s = 0.20 mm
- Z₀ = 36.1 × ln(1.258/0.227) ≈ **61.8 Ω**
- Coupling factor = 1 − 0.48·e^(−0.913) ≈ 0.807
- **Z_diff ≈ 100 Ω** → about **9.5 mil / 8 mil (w/s)**

Solder mask over the traces lowers impedance by about 2–4 Ω. Narrow w by roughly 5–10% to compensate, or let the solver account for it.

## 4. Verify and route

- **Accuracy:** IPC-2141 is only about ±5–10% accurate. Run the final geometry through a solver such as Saturn PCB Toolkit, Polar Si9000, or your fab's impedance calculator.
- **Fabrication:** order the board with impedance control at ±10%.
- **Length matching:** match length within a pair to ≤5 mil. Match between pairs loosely.
- **Reference plane:** keep a continuous GND reference under the pairs, with no plane splits.
- **Spacing:** keep at least 3×s spacing to other signals.
- **Capacitors:** place the AC-coupling caps (TX side, ~100 nF, 0201/0402) near the source.
- **Vias:** minimize vias and add GND stitching vias at any layer transitions.

If you share your actual stackup (layer thicknesses, prepreg type, copper weight, εr, solder mask), I can compute exact w/s values for your board.