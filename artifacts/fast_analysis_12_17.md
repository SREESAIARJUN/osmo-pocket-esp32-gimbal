# 2S Li-ion Charger + Protection + Balancing with IP2326 and USB-C Input

## 1. Design Corrections to the Brief

Three assumptions in the request need adjusting before the circuit will work.

| Assumption | Reality | Resolution |
|---|---|---|
| IP2326 does PD negotiation | IP2326 is a **5 V-input synchronous boost charger** for 2S packs. It has no PD/CC logic. | Use a USB-C **sink with Rd = 5.1 kΩ** on CC1/CC2. PD is unnecessary and counterproductive here (see below). |
| IP2326 provides protection | It is a charger only. It provides no overcharge-disconnect, overdischarge or short-circuit protection on the pack. | Add a dedicated **2S protector (HY2120) + dual N-MOSFET**. |
| IP2326 balances cells | It regulates only the total pack voltage (~8.4 V). | Add **per-cell bleed balancers (HY2213 ×2)**. |

**Why not request 9 V/12 V via PD?** A boost converter needs V_IN < V_OUT. A 9 V input exceeds a 2S pack voltage (6.0–8.4 V), so the IP2326 cannot regulate. Its input is rated around 5 V, so 9 V would likely damage it.

If you genuinely need PD input (faster charging, >10 W), switch to a buck-boost PD charger such as **Injoinic IP2368/IP2369** or **TI BQ25713 + PD controller**. The protection and balancing sections below still apply unchanged.

I'm interpreting "Cycle 12" as a design-iteration label. If it refers to a specific requirement set, send it and I'll align the numbers.

---

## 2. Block Diagram

```
USB-C ──► VBUS ─► TVS/PTC ─► IP2326 (5V→8.4V boost, CC/CV) ─► PACK+ (B2+)
  │CC1/CC2: 5.1k Rd to GND                                       │
  │                                              ┌───────────────┤
  │                                              │  Cell 2 ─ HY2213 + bleed R
  │                                              ├── BM (mid-tap)
  │                                              │  Cell 1 ─ HY2213 + bleed R
  │                                              └── B−
  │                                                   │
  │                                     HY2120 2S protector
  │                                     DO/CO ─► 8205A dual NMOS
  │                                                   │
  └──────────────────────────────── GND ◄── P− (system / charger ground)
```

---

## 3. Section-by-Section Design

### 3.1 USB-C Sink Input

- **CC1, CC2:** 5.1 kΩ ±1% to GND each (Rd). This is mandatory, or C-to-C chargers won't output VBUS.
- **VBUS protection:**
  - SMBJ5.0A TVS (or SMF5.0A).
  - 1.5–2 A resettable PTC, or an eFuse such as TPS25940 for a robust design.
- **Bulk capacitance:** 10 µF X5R plus 100 nF at the connector. Keep it ≤10 µF to respect the USB inrush limit.
- **Optional current advertisement:** monitor the CC pin voltage with an MCU ADC to determine source capability:
  - ~0.41 V = Default USB
  - ~0.92 V = 1.5 A
  - ~1.68 V = 3.0 A

  Use this to lower the charge current on weak sources.

### 3.2 IP2326 Charger

Verify pin names and settings against the current Injoinic datasheet. Variants and revisions differ.

| Parameter | Typical design value |
|---|---|
| Input | 5 V (≈4.5–5.5 V) |
| Charge voltage | 8.4 V (4.2 V/cell); some variants select 8.7 V for 4.35 V cells |
| Input power | ~10 W (5 V/2 A) |
| Charge current | ≈ (5 V × 2 A × η 0.9) / 8.4 V ≈ **1.07 A** at CV entry; higher at low pack voltage |
| Inductor | 2.2 µH, I_sat ≥ 4 A, low DCR (<20 mΩ) |
| Input caps | 2× 10 µF + 100 nF ceramic |
| Output caps | 2× 22 µF, 16 V X5R (de-rated) |
| NTC | 10 kΩ B=3380 thermistor to NTC pin, bonded to the cells |
| LEDs | Status LEDs per datasheet charge-state scheme |

Check that the cell's maximum charge rate (e.g., 0.5 C for a 2500 mAh cell = 1.25 A) is at least the peak IP2326 output current.

### 3.3 2S Protection: HY2120 + 8205A

- **HY2120 connections:**
  - VDD to B2+ via a 330 Ω / 100 nF RC.
  - VC (mid) to BM via 1 kΩ / 100 nF.
  - VSS to B−.
- **Suffix selection.** Pick the suffix whose thresholds match your cells, for example:
  - Overcharge (V_OC) ≈ 4.28–4.30 V/cell
  - Overdischarge (V_OD) ≈ 2.8–3.0 V/cell
  - Overcurrent sense on V− via MOSFET R_DS(on)
- **MOSFETs:** 8205A (dual 20 V NMOS, ~25 mΩ each), or AO8810/FS8205 equivalents.
  - Source-to-source between B− and P−.
  - Gates driven by DO (discharge FET) and CO (charge FET).
- **V− pin:** 1–2 kΩ to P−.
- **Overcurrent check:** the trip voltage (e.g., 0.15 V) divided by the total R_DS(on) (2 × 25 mΩ = 50 mΩ) gives about 3 A trip. Adjust the FET choice to set your trip point.

**Threshold coordination** (each layer must trip after the one before it):

1. Charger CV: 4.20 V/cell
2. Balancer: ~4.20 V
3. Protector overcharge: 4.28–4.30 V/cell

### 3.4 Cell Balancing: HY2213 per Cell

- One HY2213 per cell: VDD/VSS across that cell. The top unit references BM-to-B2+; the bottom unit references B−-to-BM.
- Choose a suffix with a balance threshold of ~4.20 V (e.g., HY2213-BB3A; check the variant table).
- The OUT pin drives a small NMOS (e.g., AO3400) that switches the bleed resistor across the cell.

**Bleed resistor sizing (target ≈ 65 mA):**
- R = 4.2 V / 0.065 A ≈ **65 Ω**. Use 2× 130 Ω 1206 in parallel.
- Total power: P = 4.2² / 65 ≈ **0.27 W**, i.e., 0.135 W per resistor (1206 rated 0.25 W).
- Add thermal relief copper and keep the resistors away from the NTC.

**Interaction with charge termination:** make the IP2326 termination current comfortably larger than the bleed current (e.g., ~100–150 mA vs 65 mA). This keeps one cell bleeding while the other finishes charging.

Bleed balancing is slow (~65 mA). It suits matched cells and maintenance, not badly mismatched packs.

---

## 4. Key BOM

| Ref | Part | Qty |
|---|---|---|
| U1 | IP2326 | 1 |
| U2 | HY2120 (suffix per cell chemistry) | 1 |
| U3, U4 | HY2213 (~4.20 V variant) | 2 |
| Q1 | 8205A dual NMOS | 1 |
| Q2, Q3 | AO3400 | 2 |
| L1 | 2.2 µH, ≥4 A | 1 |
| J1 | USB-C 16-pin receptacle (power-only) | 1 |
| R | 5.1 kΩ 1% ×2; 130 Ω 1206 ×4; 330 Ω, 1 kΩ ×2 | — |
| D