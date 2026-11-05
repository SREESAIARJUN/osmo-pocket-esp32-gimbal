# 2S Li-ion Charger, Protection, Balancing and USB-C Sink Design (Rev. Cycle 15)

**Verify before layout:** Pin numbers, thresholds and resistor tables below are representative. Confirm every pin-out and value against the current Injoinic IP2326, HYCON HY2120/HY2213 and WCH CH224K datasheets. Injoinic documents in particular change between revisions.

---

## 1. Key Design Decision: Why No PD Voltage Negotiation

The **IP2326 is a 5 V-input synchronous boost charger** for 2S (8.4 V) packs. It does **not** do:
- USB PD negotiation
- Pack protection (OV/UV/OC/short)
- Guaranteed cell balancing

Its maximum input is around 5.5 V (check the datasheet). Requesting 9 V/12 V via PD would destroy it, unless you add a buck stage, which defeats the purpose of choosing a boost charger.

The design therefore uses:

| Function | Device | Role |
|---|---|---|
| USB-C sink | 5.1 kΩ Rd on CC1/CC2 (default), with an optional CH224K locked to 5 V | Obtain 5 V at up to 3 A |
| Charger | IP2326 | 5 V to 8.4 V CC/CV boost charge, about 1.5 A max at the battery |
| Protection | HY2120 (2S protector) with dual N-MOSFET | OVP, UVP, OCP, short-circuit protection |
| Balancing | 2× HY2213 (one per cell) with bleed resistors | Passive top balancing |

**Power budget:** 8.4 V × 1.5 A ≈ 12.6 W at the battery. At about 90% efficiency this draws roughly 14 W from 5 V (about 2.8 A), so you **must** get a 3 A Type-C advertisement. Detect it via CC voltage, or cap ISET lower (for example 1.0 A) if the source only offers 1.5 A.

---

## 2. Block Diagram

```
USB-C ──► VBUS ─┬─ TVS (SMAJ5.0A) ─► Fuse 3A ─► IP2326 VIN
                │                                   │ (boost, SW → L 2.2µH)
CC1/CC2 ─ 5.1k Rd (or CH224K @5V)                  ▼
                                               BAT+ (8.4V) ──────────────► PACK+
                                                   │
                                        ┌──────────┴──────────┐
                                     Cell 2 (top)        HY2213 #2 + bleed R
                                        │  ← mid-tap (VC) →
                                     Cell 1 (bottom)     HY2213 #1 + bleed R
                                        │
                                     B− ─► HY2120 ─► [Q1 DSG | Q2 CHG] ─► PACK−
```

---

## 3. USB-C Sink Front End

**Option A (recommended, simplest):**
- CC1 → 5.1 kΩ 1% → GND
- CC2 → 5.1 kΩ 1% → GND
- The source supplies 5 V. Read the advertised current from the CC voltage:
  - about 0.41 V: default USB
  - about 0.92 V: 1.5 A
  - about 1.68 V: 3 A
- Optionally feed the CC voltage into a comparator or MCU ADC to drop the charge current when the source is not a 3 A source.

**Option B (PD source compatibility):** Use a CH224K with its CFG strapped to request **5 V only**, and use its PG output to enable the IP2326. This is only useful if some PD-only chargers misbehave without a contract. Never strap it for 9 V or higher.

**Input protection:**
- TVS: SMAJ5.0A on VBUS
- PTC or fuse: 3 A
- Bulk capacitance: 10 µF + 0.1 µF ceramic at the connector, plus 22 µF at IP2326 VIN

---

## 4. IP2326 Charger Section

| Item | Value / Note |
|---|---|
| Inductor | 2.2 µH, Isat ≥ 4 A, low DCR (≤ 20 mΩ), shielded |
| VIN caps | 2× 10 µF X5R 10 V |
| VOUT/BAT caps | 2× 10 µF X5R **25 V** (8.4 V plus DC-bias margin) |
| Charge current set | ISET resistor per the datasheet table. Start around 1.0 A and increase after thermal testing. |
| Termination voltage | 8.4 V (4.2 V/cell). Some variants allow 8.7 V for 4.35 V cells. Match to your cells. |
| NTC | 10 kΩ B3950 on the cell, wired to the NTC pin (0–45 °C charge window) |
| LEDs | Charge/full indicator pins per the datasheet |
| Thermal | Use a large GND pad with ≥ 9 vias. The IC will run warm at 1.5 A. |

**Layout notes:**
- Keep the SW node small.
- Place the input cap and boost output cap loops tight to the IC.
- Use a Kelvin connection to BAT+ for voltage sensing.

---

## 5. 2S Protection (HY2120 or equivalent: CM1102, S-8252)

**Typical thresholds (example HY2120-CB suffix; select the suffix to suit):**

| Parameter | Typical |
|---|---|
| Overcharge detect | 4.28 V/cell |
| Overcharge release | 4.08 V/cell |
| Overdischarge detect | 2.80 V/cell |
| Overcurrent sense | about 150 mV across the MOSFET RDS(on) |
| Short circuit | about 1.35 V, < 500 µs |

**Connections:**
- VDD → B+ via 330 Ω, with 0.1 µF to VSS
- VC (mid-tap) → cell junction via 1 kΩ, with 0.1 µF
- VSS → B−
- DO → Q1 gate (discharge FET)
- CO → Q2 gate (charge FET)
- VM → PACK− via 2 kΩ

**MOSFETs:** Use a dual N-ch, common-drain part, for example 8205A for ≤ 3 A or AON7810/FS8205 for more current.
- RDS(on) sets the OCP trip point: I_trip ≈ V_OC / (2 × RDS(on)).
- Example: 150 mV / (2 × 20 mΩ) ≈ 3.75 A.

**Threshold coordination:** The charger CV (4.20 V/cell) must sit below balancer onset (about 4.15–4.20 V) and protection OVP (4.28 V). The charger ends charge first, the balancer trims the high cell, and the protector is only the last line of defense.

---

## 6. Cell Balancing (Passive, HY2213 per cell)

- Use one HY2213-BB3A (balance start about 4.20 V; pick the suffix to match) across each cell.
- Bleed resistor: 68 Ω to 82 Ω, 0.5 W (1206 or 2010).
  - At 4.2 V / 75 Ω the bleed current is about 56 mA, dissipating about 0.24 W per resistor.
- The HY2213 drives an internal or external FET to switch in the bleed resistor when its cell exceeds the threshold.
- **Limitation:** Bleed current (about 56 mA) is far below charge current (about 1 A), so balancing only acts meaningfully during the CV taper and after charge ends.
  - This is fine for matched cells.
  - For poorly matched cells, raise the bleed current to around 100–150 mA and watch the thermals.

**Alternative:** If your IP2326 revision documents a built-in balance function and mid-point pin, you may use it. However, keep external balancing unless its current spec is adequate.

---

## 7. Bill of Materials (core)

| Ref | Part | Qty |
|---|---|---|
| U1 | IP2326 | 1 |
| U2 | HY2120-CB (2S protector) | 1 |
| U3, U4 | HY2213 (cell balancer) | 2 |
| U5 (opt.) | CH224K (5 V lock) | 1 |
| Q1/Q2 | Dual N-MOSFET (8205A or lower RDS(on)) | 1 |
| L1 | 2.2 µH ≥ 4 A shielded | 1 |
| J1 | USB-C 16-pin receptacle | 1 |
| D1 | SMAJ5.0A TVS | 1 |
| F1 | 3 A fuse/PTC | 1 |
| R | 5.1 kΩ ×2, ISET, 330 Ω, 1 kΩ, 2 kΩ, 75 Ω/0.5 W ×2 | — |
| RT1 | 10 kΩ NTC B3950 | 1 |
| C | 10 µF 10 V ×3, 10 µF 25 V ×2, 0.1 µF ×4 | — |

---

## 8. Validation Checklist for This Cycle

1. **CV accuracy:** 8.40 V ± 0.5% at the pack terminals with no load.
2. **Charge current:** Matches the ISET target on both a 3 A source and a 1.5 A source. Confirm no VBUS droop below about 4.5 V.
3. **Thermals:** IP2326 case below about 85 °C at full current after 30 minutes at 25 °C ambient.
4. **OVP:** Force one cell to 4.3 V using a cell simulator. The CO line must go low and charging must stop.
5. **UVP:** Discharge to 2.8 V/cell. DO must cut the load.
6. **Short circuit:** Short PACK+ to PACK−. The FETs must open in under 1 ms, and the FETs and traces must survive 10 repetitions.
7. **Balancing:** Start with a 100 mV