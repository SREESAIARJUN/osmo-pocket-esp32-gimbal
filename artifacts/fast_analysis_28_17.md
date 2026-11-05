# 2S Li-ion Charger + Protection + Balancing Design (IP2326-based)

## 1. Constraints to Resolve First

Your request bundles three functions that the IP2326 does not provide on its own.

| Requirement | IP2326 capability | Consequence |
|---|---|---|
| 2S charging | **Yes.** Synchronous boost from 5 V to 8.4 V, up to about 15 W | Use it as the charger |
| USB-C PD sink negotiation | **No.** It is a 5 V input device | Use a 5 V/3 A Type-C sink (Rd resistors). Use real PD only if you change the charger (see §7) |
| Cell protection (OV/UV/OC/SC) | **No.** It is a charger, not a protector | Add a 2S protection IC and back-to-back MOSFETs |
| Cell balancing | **No** | Add a balancer IC or a passive bleed circuit |

Verify every pin assignment and register or resistor setting against the current Injoinic IP2326 datasheet. Injoinic parts come in variants, and pinouts differ between revisions.

"Cycle 28" isn't a term I can map to a known standard or revision. If it refers to your internal design iteration or a specific requirement set, share those constraints and I'll adjust.

## 2. System Block Diagram

```
USB-C Receptacle
  │ VBUS (5V)        CC1/CC2 ──5.1kΩ──GND (each)  → requests 5V @ up to 3A
  ├── TVS (SMBJ5.0A / SMF5.0A) + 10µF
  ▼
IP2326 boost charger (5V → 8.4V CC/CV)
  │ SW → 2.2µH–3.3µH inductor (Isat ≥ 6A)
  │ VOUT/BAT+ ── 22µF×2 (25V X7R)
  ▼
PACK+ ───────────────────────────────┐
  │                                  │
 Cell 2 (top) ─┐                     │
               ├─ Mid-tap (VC1) ─────┤── Protection IC (2S) + Balancer
 Cell 1 (bot) ─┘                     │
  │                                  │
 B− ── [MOSFET DSG]─[MOSFET CHG] ── PACK− (charger GND & load GND)
```

## 3. Charger Stage: IP2326

| Item | Value / Choice | Notes |
|---|---|---|
| Input cap | 10 µF + 0.1 µF, 16 V X7R, at VIN pin | Keep the input loop short |
| Inductor | 2.2–3.3 µH, Isat ≥ 6 A, DCR < 20 mΩ | Inductor current ≈ 3 A × 8.4/5 / η → ~5 A peak |
| Output cap | 2 × 22 µF, 25 V X7R | Derate for DC bias |
| Charge voltage | 8.4 V (4.2 V/cell) | Some variants offer 8.3 V or 8.7 V options. Use 8.4 V for standard LCO/NMC cells |
| Charge current | Set via resistor or default per datasheet | Start at about 1–1.5 A. Raise only after thermal validation |
| NTC | 10 kΩ B=3435 NTC on the pack, to the NTC pin | Required for 0–45 °C charge window |
| Status LEDs | Per datasheet LED pins | Optional |

**Thermal note:** at 15 W the IP2326 dissipates about 1–1.5 W. Use an exposed-pad footprint with at least 6–9 thermal vias to an internal ground plane.

## 4. USB-C Sink Interface (5 V Only)

Because the IP2326 only accepts 5 V, no PD protocol IC is needed.

- **CC1 and CC2:** each gets a 5.1 kΩ ±1% resistor to GND (Rd). This makes the board a compliant sink. Any source, including PD chargers, then supplies 5 V and advertises its current capability.
- **Current advertisement (optional):** read the CC voltage to detect how much the source offers.
  - About 0.41 V means default USB current.
  - About 0.92 V means 1.5 A.
  - About 1.68 V means 3.0 A.

  If your IP2326 variant lacks CC sensing, add a small comparator or ADC. Then cap the charge current at 5 W with legacy sources to avoid browning out weak adapters.
- **ESD:** a low-capacitance TVS array on CC1/CC2 (e.g., TPD2E2U06 or ESD5V3).
- **VBUS:** TVS rated about 5 V standoff. Add a polyfuse (about 3 A hold) if this is a consumer product.

## 5. 2S Protection Stage

Use a dedicated 2-series protection IC driving two N-channel MOSFETs in the negative path. Candidate parts:

- Seiko/ABLIC S-8252
- Ricoh R5460 series
- HYCON 2S protectors
- TI BQ77xx family

Typical thresholds for NMC/LCO cells:

| Parameter | Threshold | Delay |
|---|---|---|
| Overcharge (per cell) | 4.25–4.30 V | ~1 s |
| Overcharge release | 4.05–4.10 V | — |
| Over-discharge | 2.5–2.8 V | ~100 ms |
| Discharge overcurrent | Set by Rds(on) × 2 vs. detect voltage (e.g., 0.1 V) | ~10 ms |
| Short circuit | ~0.5–1.0 V across MOSFETs | ~250 µs |

**MOSFETs:** use a common-drain dual N-FET, 20 V / 8 A class, with Rds(on) ≤ 10 mΩ at Vgs = 4.5 V. Examples are the AO8810 class and the FS8205A for light loads.

- **Overcurrent sizing:** the trip current is I_trip = V_det / (2 × Rds(on)).
- **Worked example:** with 0.1 V detect and 2 × 10 mΩ, the trip is about 5 A. Recheck it at hot temperature, because Rds(on) rises about 1.5×.

**Supporting parts:**

- RC filter on VDD: 330 Ω + 0.1 µF.
- 1 kΩ on the VC (mid-tap) sense line.
- 2 kΩ on the VM pin (current sense).

**Secondary protection (recommended for products):** add an independent OVP. The TI BQ29209 is a good fit because it also provides balancing (see §6). It can blow a chemical fuse or drive a separate FET at about 4.35 V/cell.

## 6. Cell Balancing

**Option A: BQ29209 (recommended).** This is a 2S overvoltage protector with integrated balance control.

- Bleed resistor: one per cell, about 39–68 Ω, rated 0.25 W. This gives about 60–100 mA of bleed at 4.2 V.
- Balancing activates on cell voltage mismatch near the top of charge.
- External balance MOSFETs are used if you need higher bleed current. Check the datasheet application circuit.

**Option B: discrete passive top-balance.** Per cell, use:

- a TL431 or a 4.20 V voltage detector,
- a small P/N-FET,
- a 47 Ω bleed resistor.

Each cell is clamped individually at 4.20 V. It's cheap but less precise; set the threshold tolerance to ±10 mV or better.

**Sizing rule:** a passive balancer only corrects small drift. For 2,000 mAh cells, 80 mA of bleed removes about 1% SOC per 15 minutes at the top of charge. That is adequate for matched cells. Badly mismatched cells need replacement, not balancing.

**Important interaction:** the IP2326 terminates on total pack voltage (8.4 V). Without balancing, one cell can sit at 4.25 V while the other sits at 4.15 V. This is why the balancer and the per-cell OVP above are not optional.

## 7. If You Truly Need PD Negotiation

For 9 V or 12 V input (faster charging, less boost stress), replace the IP2326 rather than bolting on a PD controller.

| Option | Description |
|---|---|
| Injoinic IP2368 / IP2366 | Integrated PD sink plus buck-boost charger for 2–6S. Single chip |
| TI BQ25887 | 2S boost charger from 5 V with integrated cell balancing. Removes the separate balancer |
| PD sink controller + 2S buck charger | Use a CH224K, STUSB4500, or AP33772 to request 9 V. Pair with a buck charger such as the BQ25713 or a 2S buck charger. Requires a fallback path if the source is 5 V only |

## 8. Layout Checklist

- **Boost power loop:** keep the VIN cap → IC → inductor → output cap loop under about 15 mm². Use a solid ground plane underneath.
- **Kelvin sense:** route cell sense lines (B+, mid-tap, B−) from the cell terminals, not from the power path.
- **Protection FETs:** use wide copper (≥ 3 mm for 5 A on 1 oz copper) and thermal relief only where needed.
- **Balance resistors:** keep them away from the NTC and the cells, since they can reach 60 °C+.
- **Mid-tap wire:** fuse it or use a thin trace as a fusible link. Add a series resistor at the IC.

## 9. Validation Plan

