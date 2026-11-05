# 6-Layer Thermal-Optimized Stackup for a 25.5 × 25.5 mm AIO FC + 4-in-1 ESC

I don't have the context from earlier cycles, so this design rests on these assumptions. Adjust as needed.

| Parameter | Assumed value |
|---|---|
| Board | 25.5 × 25.5 mm, 20 × 20 mm mounting pattern |
| ESC topology | 4 ESCs × 6 discrete N-FETs = 24 MOSFETs |
| FET package | 3.3 × 3.3 mm PDFN, 30–40 V |
| Battery | 2S–4S LiPo |
| Current per ESC | ~25–35 A burst |
| Target thickness | ~1.1–1.2 mm |

---

## 1. Layer Stack

| # | Layer | Function | Cu (finished) | Dielectric below | Thickness |
|---|---|---|---|---|---|
| L1 | **Top** | 24 FETs, gate drivers, phase pours, motor pads | **2 oz** (70 µm) | — | 0.070 |
| — | Prepreg | **Thin, thermally enhanced** (1× 1080 or thermal PP) | — | k ≥ 1.0 W/m·K | **0.075** |
| L2 | **GND** | Solid plane, primary heat spreader | **2 oz** | — | 0.070 |
| — | Core | FR-4 High-Tg | — | — | 0.200 |
| L3 | **VBAT** | Solid battery plane, high-side drain heat sink | **2 oz** | — | 0.070 |
| — | Prepreg | FR-4 | — | — | 0.200 |
| L4 | **GND** | Second plane, heat spreading + shielding | **2 oz** | — | 0.070 |
| — | Core | FR-4 High-Tg | — | — | 0.200 |
| L5 | **Signal/Power** | 5 V / 3.3 V islands, current sense, UART/SPI | 1 oz | — | 0.035 |
| — | Prepreg | 1× 1080 | — | — | 0.075 |
| L6 | **Bottom** | MCU, gyro, baro, OSD, regulators, connectors | 1 oz | — | 0.035 |

**Total ≈ 1.10 mm (≈1.15 mm with mask).** The stack is symmetric in dielectric thickness, so warpage risk is low. The 2/2/2/2/1/1 oz copper split is slightly asymmetric; ask the fab to balance it with copper thieving on L5/L6.

**Copper totals:** 8 oz in the top four layers gives roughly 280 µm of lateral heat-spreading copper.

---

## 2. Why This Arrangement

**FETs on L1, sensitive electronics on L6.**
- The top side faces prop downwash, which gives the FETs forced convection.
- Separating the two sides keeps the gyro away from the hottest copper.

**75 µm dielectric between L1 and L2. This is the key thermal path.**

The exposed pad of a PDFN FET is its **drain**:
- The high-side drain is VBAT, so it can be stitched directly to L3.
- The low-side drain is the **phase node**, which is switching. It cannot be via'd to any plane.

The low-side FETs therefore have to dump heat *through the dielectric* into the L2 GND plane underneath the phase pour. Dielectric thickness dominates that path:

| Dielectric under a ~9 mm² phase pad | R_th through dielectric |
|---|---|
| 200 µm standard FR-4 (k≈0.3) | ~74 K/W |
| 75 µm standard FR-4 (k≈0.4) | ~21 K/W |
| **75 µm thermal PP (k≈1.0–2.2)** | **~4–8 K/W** |

The real phase pour is much larger than the pad, so effective resistance is lower still. Electrical isolation at 75 µm is still above 2 kV, which is ample for 4S (16.8 V) or even 6S.

**VBAT sandwiched between two GND planes (L2 / L3 / L4).**
- This forms buried plane capacitance and minimizes the commutation loop inductance, which reduces ringing and switching loss (and therefore heat).
- The high-side drains get a direct heavy-copper path into L3.

**L5/L6 thin prepreg.** This gives a tight reference for the MCU and gyro SPI routing.

---

## 3. Thermal Via Strategy

| Location | Via spec | Count per FET | Connects |
|---|---|---|---|
| High-side drain pad | 0.20 mm drill / 0.40 mm pad, **VIPPO** (IPC-4761