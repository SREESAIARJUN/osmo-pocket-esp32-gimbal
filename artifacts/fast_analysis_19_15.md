# 2S Li-ion Charger + Protection + Balancing with IP2326 and a USB-C Sink

Note: "Cycle 19" has no defined meaning here, so this is a standalone design. If it refers to a revision in an ongoing project, send the previous state and I'll adjust.

---

## 1. Constraints That Shape the Design

| Item | Reality | Design consequence |
|---|---|---|
| **IP2326** (Injoinic) | 5 V-input synchronous **boost** charger for 2S Li-ion, about 8.4 V CV, up to roughly 1.5 A charge (about 10–15 W class). Has NTC and LED status pins. | It is only a charger. You still need a separate **2S protector**. |
| **Cell balancing** | IP2326 is not documented to perform active balancing. Check your datasheet revision. | Add **per-cell passive bleed balancers**. |
| **USB-C PD** | IP2326 takes **5 V only** (absolute max is around 6–7 V, so check the datasheet). | Do **not** negotiate 9/12/15/20 V. A 5 V Type-C sink (Rd = 5.1 kΩ) is correct and safest. |

**If you truly need PD at 9–20 V**, IP2326 is the wrong part. Use one of these instead:
- **IP2368 / SW6306**: PD-capable multi-cell chargers.
- **BQ25887**: 2S charger with integrated cell balancing.
- **CH224K/IP2721 trigger + buck 2S charger**: PD trigger plus a buck 2S charger such as BQ25703A or MP2672A (MP2672A also has balancing).

---

## 2. Block Diagram

```
USB-C ──► CC 5.1k Rd ──► VBUS ─► TVS + PTC/fuse ─► IP2326 (boost CC/CV 8.4V)
                                                       │
                                                       ▼
                                  BAT+ ───────┬────────────────── PACK+
                                              │
                         Cell 2 ──┬── HY2213 #2 (bleed balancer)
                                  │
                       midpoint ──┼── HY2213 #1 ── Cell 1 ── BAT−
                                  │
                       HY2120 (2S protector) senses VC1/VC2
                                  │
                       BAT− ─► dual N-MOSFET (DO/CO) ─► PACK−
```

---

## 3. USB-C Sink Front End (5 V only)

- **CC1 and CC2**: each gets 5.1 kΩ ±1% to GND (Rd). Use separate resistors; never tie CC1 and CC2 together.
- **Source current capability (optional)**: read the CC voltage with an ADC or comparators to detect what the source can supply.
  - 0.25–0.61 V means Default USB (500/900 mA).
  - 0.70–1.16 V means 1.5 A.
  - Above 1.31 V means 3.0 A.
  - Use this to limit IP2326 input current. The IP2326 input-voltage-adaptive (VIN-droop) loop also handles weak sources automatically.
- **VBUS protection**:
  - SMBJ5.0A TVS.
  - 2 A resettable PTC or a fast fuse.
  - 10 µF + 100 nF ceramic close to the connector.
- **ESD**: low-capacitance TVS on CC lines (e.g., ESD5Z5V or TPD2E2U06).
- **D+/D−**: leave them unconnected, or short them for BC1.2 DCP detection if you want legacy-charger current.
- **Reverse protection**: Type-C cannot reverse polarity, but a P-MOSFET ideal-diode stage protects against backfeed when the pack is powering the load.

---

## 4. IP2326 Charger Stage

Take the values below from the datasheet reference design and follow its layout.

| Pin / function | Typical component |
|---|---|
| VIN | 10–22 µF X5R, at least 10 V |
| Inductor (SW) | 1.0–2.2 µH, Isat ≥ 4 A, low DCR (≤ 20 mΩ) |
| BAT output | 2 × 10 µF, 16 V rated (8.4 V is too close to 10 V when derated) |
| NTC | 10 kΩ B=3950 thermistor on the cells (gives 0–45 °C charge window) |
| LEDs | Status/charge indication per datasheet |
| Charge current | Set by resistor or default. Choose **≤ 0.5C** of the cell capacity, e.g., 1 A for 2000 mAh cells. |

**Layout**: keep the SW–inductor–VIN–GND loop tight. Use the exposed pad to a solid ground pour with thermal vias.

**Input power check**: 8.4 V × 1.5 A ≈ 12.6 W at the output, which is about 14 W in at roughly 90% efficiency. That needs a **3 A** USB-C source. On a 1.5 A source, limit charge to about 0.75 A.

---

## 5. 2S Protection: HY2120 (or S-8252 / DW01-pair alternative)

**Thresholds** (choose a suffix variant to match):

| Function | Threshold |
|---|---|
| Overcharge detect | 4.25–4.30 V/cell |
| Overcharge release | ~4.15 V |
| Over-discharge detect | 2.8–3.0 V/cell |
| Overcurrent / short-circuit | Set by sensing the MOSFET R_DS(on) |

**Connections:**
- VC1 to the midpoint and VDD to BAT+, each through RC filters (330 Ω + 0.1 µF, typical).
- DO and CO pins drive a **common-drain dual N-MOSFET** in the BAT− path.

**MOSFET choice:**
- Use a dual N-FET with R_DS(on) ≤ 10 mΩ at V_GS = 4.5 V, such as AO8810 or FS8205A-class. Upsize if load current exceeds 3 A.
- Overcurrent trip: I_trip ≈ V_OC1 / (2·R_DS(on)). For example, 0.15 V / (2 × 10 mΩ) ≈ 7.5 A.
- If you need a precise trip point, use a sense-resistor protector instead.

This protector is the **independent safety layer**. It must trip above the charger's CV point but below the cell's absolute maximum.

---

## 6. Cell Balancing (passive bleed, one per cell)

Use one **HY2213-BB3A**-type single-cell balancer per cell (balance threshold about 4.20 V):

- **VDD/VSS** go across each cell: Cell 1 is midpoint–BAT−, Cell 2 is BAT+–midpoint.
- The **OUT** pin drives an N-MOSFET (e.g., AO3400) that switches a bleed resistor across the cell.

**Bleed resistor sizing:**
- R = 47 Ω gives I ≈ 4.2 / 47 ≈ 90 mA and P ≈ 0.38 W. Use a 1 W resistor or two 100 Ω 0.5 W in parallel.
- The resistor must dissipate on its own. Keep it away from the NTC and cells, or account for its heat.

**How it works with a CC/CV charger:**
- When the higher cell reaches 4.2 V, it bleeds while the lower cell keeps charging.
- During CV termination, the charge current tapers toward the bleed current, so the cells converge.
- With 90 mA of bleed, mismatches of up to about 5% capacity are corrected within a few charge cycles.

**Caution:** the balancer must not bleed below roughly 4.1 V, or it will drain idle packs. The HY2213 has a defined threshold and hysteresis, which prevents this.

---

## 7. Bill of Materials (core)

| Ref | Part | Qty |
|---|---|---|
| U1 | IP2326 | 1 |
| U2 | HY2120 (2S protection, suffix per thresholds) | 1 |
| U3, U4 | HY2213-BB3A | 2 |
| Q1 | Dual N-MOSFET, ≤ 10 mΩ (AO8810 / FS8205A) | 1 |
| Q2, Q3 | AO3400 (balancer switches) | 2 |
| R_bleed | 47 Ω 1 W | 2 |
| R_CC | 5.1 kΩ 1% | 2 |
| L1 | 1.0–2.2 µH, Isat ≥ 4 A | 1 |
| D1 | SMBJ5.0A | 1 |
| F1 | 2 A PTC/fuse | 1 |
| NTC1 | 10 kΩ B3950 | 1 |
| J1 | USB-C 16-pin receptacle (power + CC) | 1 |

---

## 8. Validation Checklist

1. **USB-C sink**: CC reads correctly on 0.5 A, 1.5 A, and 3 A sources. No