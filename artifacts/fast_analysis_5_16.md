# 2S Li-ion Charger with Protection, Balancing and USB-C Sink: Design Rev. 5 (IP2326-based)

## 0. Feasibility Check

Three points constrain this design. Verify each against the current Injoinic datasheet before freezing it.

| Requirement | IP2326 native? | Resolution |
|---|---|---|
| 2S charge (5 V → 8.4 V boost, CC/CV) | **Yes** | Core function |
| Cell-level OV/UV/OC/SC protection | **No.** It is a charger, not a protector | Add a dedicated 2S protection IC and back-to-back FETs |
| Cell balancing | **No** | Add per-cell shunt balancers |
| USB-C PD sink | **No PD.** The input is 5 V only (~4.5–5.5 V window) | Use Type-C Rd sink (5 V up to 3 A advertised). A PD controller adds nothing because the IP2326 cannot use 9/12 V |

**If you need more than about 10 W charge power from PD (9–20 V)**, switch to a buck-boost PD charger such as the IP2368, SW6306 or BQ25713 + PD controller. This document assumes the 5 V / ~10 W ceiling is acceptable.

---

## 1. Block Diagram

```
USB-C ──► Rd sink + TVS + input FET/eFuse ──► IP2326 (boost CC/CV, 8.4 V)
                                                 │
                                                 ▼ BAT+ (pack top)
                         ┌───────────── Cell 2 ── HY2213 balancer #2
                         │  (midpoint) ─ sensed by protector VC1
                         └───────────── Cell 1 ── HY2213 balancer #1
                                                 │
                                          BAT− (pack bottom)
                                                 │
                         2S protector (HY2120 / S-8252) ── Q1/Q2 dual NMOS ── P− (system GND)
```

---

## 2. USB-C Sink Front End

| Ref | Part | Purpose |
|---|---|---|
| J1 | USB-C 16-pin receptacle | VBUS ×2, GND ×2, CC1, CC2, D+/D− (D+/D− NC or to IP2326 if your variant does fast-charge detection) |
| R1, R2 | 5.1 kΩ 1% to GND on CC1, CC2 | Rd sink identification. Mandatory, or C-to-C sources give 0 V |
| D1 | SMBJ6.0A (or SMF5.0A) TVS VBUS–GND | ESD/surge clamp |
| U1 (optional) | TPS25940 / SY6280 eFuse, OVP ≈ 6 V | Protects against a non-compliant source pushing 9 V+ |
| C1 | 10 µF/16 V X5R + 100 nF | Input bulk, next to IP2326 VIN |

**Current-capability detection (optional):** read the CC voltage with an MCU ADC.

| CC voltage | Source advertisement |
|---|---|
| 0.25–0.61 V | Default (500/900 mA) |
| 0.70–1.16 V | 1.5 A |
| > 1.31 V | 3 A |

On a default source, reduce the IP2326 charge current via ISET so you don't collapse VBUS.

---

## 3. IP2326 Charger

| Ref | Value | Notes |
|---|---|---|
| L1 | 2.2–4.7 µH, Isat ≥ 4 A, DCR < 20 mΩ | Use the datasheet-recommended value. Shielded |
| C_out | 2× 22 µF/16 V X5R at the BAT pin | Boost output ripple |
| ISET resistor | Per datasheet table | Set charge current. Start at ~1 A (≈ 8.4 W in) for thermal margin |
| NTC | 10 kΩ B3435 thermistor on the cells | Charge-temperature window (≈ 0–45 °C) |
| LEDs | 1–4 LEDs with resistors | Charge state per datasheet mode |
| Exposed pad | ≥ 9 thermal vias to GND pour | Required for 10 W |

**Charge termination voltage:** keep 8.4 V (4.20 V/cell). The balancer threshold must sit *at or just below* the per-cell CV target, as set in Section 5.

---

## 4. 2S Protection

**IC:** HY2120-CB (or Ablic S-8252, or equivalent 2S protector). Pick the suffix that gives these thresholds:

| Parameter | Target |
|---|---|
| Overcharge detect (per cell) | 4.25–4.28 V, ~1 s delay |
| Overcharge release | ~4.08 V |
| Over-discharge detect | 2.7–2.9 V |
| Discharge overcurrent | Set by FET Rds(on) × current (e.g., 0.15 V threshold) |
| Short circuit | ~0.5–0.9 V across FETs, µs delay |

**Circuit:**

- **VDD:** connect to BAT+ via 330 Ω, with 0.1 µF to VSS.
- **VC (midpoint):** connect via 1 kΩ, with 0.1 µF to VSS. This RC filters balancer switching noise.
- **VSS:** connect to BAT−.
- **Q1/Q2:** common-drain dual N-MOSFET (e.g., AO8810, FS8205A-class), Vds ≥ 20 V, Rds(on) ≈ 10–20 mΩ each.
  - DO drives the discharge FET.
  - CO drives the charge FET.
- **VM:** connect to P− through 2 kΩ.
- **Overcurrent thresholds:** with ~25 mΩ total path and a 0.15 V threshold, the trip is about 6 A. Scale the FETs to your load.

**Important:** the IP2326 charges through P+/P−, so it sits *outside* the protection FETs. The protector is the independent last line of defense if the charger fails.

---

## 5. Cell Balancing

Use one HY2213-BB3A-class IC (or equivalent single-cell shunt balancer) per cell.

| Element | Value |
|---|---|
| Balance threshold | ~4.20 V (choose a suffix *below* the 4.25 V protector OVP) |
| Bypass resistor | 39–47 Ω, 0.5 W |
| Bypass current | ≈ 100 mA at 4.2 V |
| Bypass FET | Small NMOS (e.g., AO3400) if the IC needs one |

- **Cell 1:** VDD at the midpoint, VSS at BAT−.
- **Cell 2:** VDD at BAT+, VSS at the midpoint.

**How it works:** this is top-balancing during CV. When the high cell hits ~4.2 V, it bleeds ~100 mA while the other catches up. It is effective for mismatch of a few %. Large imbalance requires matched cells at assembly.

**Interaction to verify:** in CV the IP2326 regulates the pack at 8.4 V and taper current eventually drops below the bleed current. Charging may then appear to "never terminate." To avoid this:

- Set the termination current above ~100 mA, or
- Reduce bleed to ~50 mA (82 Ω).

**Thermal limits:** bleed dissipates ~0.4 W per resistor. Space the resistors and keep them away from the NTC.

---

## 6. Layout Rules

1. Keep the boost hot loop tight: IP2326 SW → L1 → output cap → GND.
2. Route the protector VC/VDD sense traces as **Kelvin connections** directly at the cell tabs, not along the power path.
3. Place Q1/Q2 on a wide copper pour with ≥ 2 oz copper or polygon reinforcement for >3 A.
4. Place the TVS within 5 mm of the USB-C VBUS pins.
5. Mount the NTC thermally on the cells, not on the PCB near the inductor.
6. Use separate GND regions for BAT−/P− with a single defined junction at the protection FETs.

---

## 7. Cycle 5 Verification Checklist

| Test | Pass criterion |
|---|---|
| C-to-C charger, e-marked cable | Source supplies 5 V. Rd recognized |
| Charge CC → CV | 8.40 V ±1% at end of charge. Current matches ISET |
| Deliberate 150 mV imbalance | Balancers converge to < 20 mV within N cycles |
| Cell OVP (force a cell to 4.30 V with a supply) | CO opens within spec delay |
| Over-discharge | DO opens at 2.8 V ±50 mV |
| Short P+/P− | Trips in < 500 µs. No FET damage after 10 repeats |
| 9 V injected on VBUS | eFuse/OVP blocks. IP2326 survives |
| Thermal, 1 h at full charge, 25 °C ambient | IC < 85 °C. Bleed resistors < 100 °C |
| NTC open/hot | Charging suspended |

---

## 8. Changes for Rev. 5 (Recommended)

- Added an explicit input OVP. The IP2326 is not tolerant of PD voltages from faulty sources.
- Set balancer thresholds below the protector OVP to remove the race condition.
- Added Kelvin sense routing for VC.
- Documented the 5 V-only limitation. Escalate to a PD buck-boost charger if charge power above ~10 W is required.

I can produce a netlist or KiCad symbol mapping, or redo this around an IP2368 for true 9–20 V PD input.