# 2S Li-ion Pack Design: IP2326 Charger, Protection, Balancing and USB-C Input

I've treated "Cycle 22" as a revision label. If it refers to a specific requirement set, send it and I'll adjust the design.

## 0. Corrections to the Brief

Three assumptions in the brief need correcting before the schematic:

1. **The IP2326 is a charger, not a protector.** It is a synchronous boost charger: 5 V in, 2S (8.4 V) out, up to about 15 W. It does not provide independent OV, UV, OC or short-circuit cutoff. A separate 2S protection IC and MOSFETs are required.
2. **The IP2326 accepts only 5 V input.** It cannot use 9, 12, 15 or 20 V from Power Delivery. Negotiating PD would gain nothing, and it could damage the IC if a PD sink controller requested more than 5 V. The correct USB-C sink is a plain Type-C sink with Rd resistors, which gets 5 V at up to 3 A from any compliant source.
   - If you actually want PD fast-charge (for example 9–20 V in, 20–60 W), switch to a buck-boost PD charger such as the IP2368, SW6306 or BQ25713 with a PD controller. I can redesign around that.
3. **Do not rely on the charger for balancing.** Some IP232x variants offer a balance function, so check your exact datasheet revision. Either way, use a dedicated per-cell balancer so balancing still works when the charger is idle or absent.

## 1. Architecture

```
USB-C ──► VBUS filter/TVS ──► IP2326 (boost CC/CV 8.4 V) ──► PACK+ ──► Cell2(+)
  CC1/CC2: 5.1k Rd                                                 │
                                                          BAL tap (mid)
                                    HY2213 x2 (per-cell bleed)     │
                                    HY2120 (2S protector) ── Cell1(−) ── B−
                                                                    │
                                    Dual N-FET (DSG/CHG) ──► PACK−
```

## 2. USB-C Sink (5 V, up to 3 A)

| Item | Value | Notes |
|---|---|---|
| CC1, CC2 | 5.1 kΩ ±1 % to GND (one each) | Mandatory Rd. Do not tie CC1 and CC2 together. |
| VBUS TVS | SMBJ5.0A or SMF5.0A | Clamps hot-plug ringing. |
| VBUS bulk | 10 µF + 100 nF at the connector | Keep ≤10 µF at the connector for USB inrush compliance. More capacitance goes after the IP2326 input. |
| Optional OVP | AW32901 or SY6280 load switch | Protects against non-compliant 9 V+ chargers. Recommended because the IP2326's input absolute maximum is low. |
| Source current limit | IP2326 input-current/power setting | Set about 2 A for 10 W, or 3 A for 15 W only if you detect a 3 A source. |

Optional: read the source's CC voltage (vRd-USB, 1.5 A or 3.0 A level) with an MCU ADC to pick the input-current limit. This is still not PD, and none is needed.

## 3. IP2326 Boost Charger

| Function | Typical implementation | Verify in datasheet |
|---|---|---|
| Inductor | 1.5–2.2 µH, Isat ≥ 6 A, DCR ≤ 15 mΩ (e.g., 6×6 or 7×7 shielded) | Recommended L value |
| Input caps | 2 × 22 µF X5R 10 V + 100 nF | — |
| Output (BAT) caps | 2–3 × 22 µF X5R 16 V (25 V preferred for DC-bias margin) | — |
| Charge current | Set via the ISET/resistor pin to ≈0.5 C. Example: 2× 2500 mAh cells → about 1.2–1.5 A at 8.4 V ≈ 10–12 W | Resistor-to-current table |
| CV voltage | 8.4 V (4.2 V/cell) | Option pins for 8.6 or 8.7 V variants |
| NTC | 10 kΩ B3435 NTC on the cell body to the NTC pin | Thresholds (0–45 °C charge) |
| LED/status | Per the datasheet LED pins | — |
| Thermal | Exposed pad to a large GND pour with ≥9 vias | — |

Layout priorities:
- Minimise the hot loop from the switch node through the output cap to GND.
- Keep the switch-node copper small.
- Place the input and output caps directly at the IC pins.

## 4. Protection: HY2120 Family (2S) with a Dual N-MOSFET

Pick the HY2120 suffix for these thresholds:

| Parameter | Target |
|---|---|
| Overcharge detect / release | 4.25–4.30 V / 4.10–4.15 V |
| Over-discharge detect / release | 2.5–2.8 V / 3.0 V |
| Discharge overcurrent | Set by MOSFET Rds(on) × trip voltage |
| Short circuit | Built-in, about 1.0–1.35 V across the FETs |

**Discharge MOSFETs.** Use a common-drain dual N-FET in the low side, such as the FS8205A for packs up to about 4 A. For higher currents use 2× AON7410-class FETs, or another back-to-back dual with 3–5 mΩ Rds(on). The OC trip current is roughly V_OCD / (2 × Rds(on)).
- Example: 0.15 V / (2 × 10 mΩ) ≈ 7.5 A.
- Account for Rds(on) rising with temperature.

**Support components:**
- VDD RC: 330 Ω and 100 nF.
- Cell-sense resistors: 1 kΩ per tap.
- VM resistor: 2 kΩ.
- Gates: DO drives the discharge FET and CO drives the charge FET. Add a 1 MΩ gate pull-down on each FET.

**Placement.** The IP2326's BAT− and GND connect to the cell side (B−). Its BAT+ connects to PACK+. This keeps the protector in the path for both charge and load.
- If the charger must stay behind the protector so it can be cut off on overcharge, put the IP2326 GND on PACK− instead.
- PACK− is the safer choice for an independent second-level OV cutoff.

## 5. Cell Balancing: Per-Cell Shunt Bleed

| Item | Value |
|---|---|
| IC | HY2213-BB3A or a similar single-cell balancer, one per cell |
| Balance threshold | ≈4.20 V (pick the suffix) |
| Bleed resistor | 33–47 Ω, 0.5 W (≈90–125 mA at 4.2 V) |
| Bleed FET | Internal or small external N-FET per the variant |
| Mid tap | Cell1+/Cell2− through 1 kΩ to the protector, and directly to the balancers |

Balancing happens at the top of charge. With about 100 mA of bleed, a 50–100 mAh mismatch corrects within one or two cycles. If you need active balancing for large-capacity packs, use a switched-cap balancer such as the LTC3305-class instead.

Thermal: about 0.5 W per resistor at the CV phase. Space the resistors away from the NTC and the cells.

## 6. Safety Summary

| Fault | Primary response | Backup response |
|---|---|---|
| Overcharge | IP2326 CV at 8.4 V | HY2120 OV at 4.28 V/cell; optional 3rd-level fuse or SCP |
| Over-discharge | HY2120 UV | Load-side UVLO |
| Overcurrent / short | HY2120 via FET Rds(on) | PTC or fuse (e.g., 10 A) in PACK+ |
| Imbalance | HY2213 bleed | HY2120 per-cell OV stops charge |
| Temperature | IP2326 NTC | Protector-side NTC (if variant supports) |

## 7. Bring-Up Checklist

1. **USB-C sink.** With the cells removed, confirm 5 V on VBUS from a C-to-C cable. If VBUS is 0 V, the Rd resistors are wrong.
2. **Charger output.** Use a battery simulator or a 2S dummy load with a 7 V supply. Verify the CC current, the CV at 8.4 V ±0.5 %, and termination.
3. **Protection thresholds.** Drive each cell tap from a programmable supply and check the OV and UV trip/release points per cell.
4. **Short circuit.** Short PACK+ to PACK− through a current probe. The FETs should open in under 500 µs.
5. **Balancing.** Offset the cells by 0.1 V and confirm bleed starts on the high cell at the threshold.
6. **Thermal.** Run 1 hour at maximum charge power and check the IC, inductor and bleed-resistor temperatures. Target ΔT < 40 °C.

If