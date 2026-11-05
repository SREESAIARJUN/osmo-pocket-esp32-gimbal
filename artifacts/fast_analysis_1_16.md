# 2S Li-ion Charger + Protection + Balancing with IP2326 and USB-C: Design Review, Cycle 1

## 0. Feasibility Check

The requirements conflict with what IP2326 can do, so the architecture needs adjusting first. These points are from memory of the Injoinic datasheet. Verify them against the current revision before layout.

| Requirement | IP2326 capability | Consequence |
|---|---|---|
| 2S charging | Yes. Synchronous **boost** charger, 5 V in to 8.4 V, about 1.5 A max charge (about 10 W class) | Core function OK |
| USB-C PD sink | **No PD protocol engine.** Input is 5 V only (abs max is low, roughly 6–7 V) | Requesting 9/12/15 V would damage it |
| Cell balancing | Verify per datasheet revision. Do **not** rely on it | Add dedicated balancing |
| Pack protection (OV/UV/OC/short) | Charge-side protection only. No discharge-path protection | Add a 2S protector IC and FETs |

**Decision for Cycle 1:**
- Keep IP2326.
- Implement USB-C as a **Type-C current-advertisement sink (5 V, up to 3 A)**, not PD.
- If PD at 9 V or more is truly required, see Section 6 for alternative architectures.

---

## 1. Block Diagram

```
USB-C ──► VBUS TVS ──► (optional eFuse/PFET) ──► IP2326 boost charger ──► PACK+ (8.4 V)
 CC1/CC2: 5.1k Rd                                     │
                                                      ├── BAT mid-tap sense
                                      ┌───────────────┴──────────────┐
                                    Cell 2 ── HY2213 balancer (4.20 V)
                                    Cell 1 ── HY2213 balancer (4.20 V)
                                      │
                    2S protector (HY2120 class) ──► dual N-MOSFET (8205A / AON7xxx) in PACK– path
                                      │
                                    PACK– ──► Load
```

---

## 2. USB-C Input (Sink, 5 V)

- **CC1 and CC2:** 5.1 kΩ ±1 % pull-down on each (Rd). Together these identify the device as a sink.
- **Optional CC sensing:** Read CC voltage with an ADC or comparator to detect source capability:
  - 0.25–0.61 V means Default USB power. Limit input to 500 mA / 900 mA.
  - 0.70–1.16 V means 1.5 A.
  - 1.31–2.04 V means 3.0 A.
- **Feed result to IP2326:** Use it to set or limit input current, through the IP2326 input-current-limit/ISET resistor or a GPIO-switched resistor. Without sensing, set charge current so the input draw stays at or below 2 A, and accept that legacy 500 mA ports will brown out.
- **VBUS protection:**
  - SMBJ5.0A or SMF5.0A TVS.
  - 10 µF + 0.1 µF X7R at the connector.
  - Optional overvoltage cutoff: a PFET plus a 5.6 V zener comparator, or a load switch with OVP such as SGM2551 or TPS2595. This guards against non-compliant chargers.
- Tie **D+/D–** together or leave them per IP2326 fast-charge detection. Do not enable any QC request feature, because it would raise VBUS.

---

## 3. IP2326 Charger Stage

| Item | Value / Guidance |
|---|---|
| Input cap | 2× 10 µF 16 V X7R near VIN |
| Inductor | 2.2–4.7 µH (per datasheet), Isat ≥ 4 A, low DCR (<20 mΩ) |
| Output cap (BAT) | 2× 22 µF 25 V X7R |
| Charge current | Set via ISET resistor. Start at **1.0 A** for Cycle 1 (≈ 8.4 V × 1 A / 0.9 eff ≈ 1.9 A input) |
| Termination | 8.40 V (4.20 V/cell). Confirm there is no 8.7 V option misconfiguration |
| NTC | 10 kΩ B3435 thermistor on pack, to IP2326 NTC pin. Gives JEITA-style 0–45 °C charge window |
| LEDs | Status pins per datasheet |
| Thermal | Exposed pad to GND pour with ≥ 9 vias |

---

## 4. Pack Protection (2S)

**IC:** HY2120 family, or an equivalent such as S-8252 or BQ2970-class in 2S variants. Choose the suffix for these thresholds:

| Parameter | Target |
|---|---|
| Overcharge detect | 4.25–4.30 V/cell |
| Overcharge release | 4.05–4.15 V |
| Over-discharge | 2.8–3.0 V/cell |
| Overcurrent / short | Set by FET Rds(on). Target about 6–8 A OCP, short-circuit >15 A |

**Circuit:**
- **VDD:** 330 Ω + 0.1 µF RC filter from PACK+.
- **VC (mid-tap):** 1 kΩ + 0.1 µF filter.
- **MOSFETs:** Common-drain dual N-MOSFET in the PACK– path. 8205A works at ≤ 3 A load. Use a lower-Rds(on) part, such as an AON6 or AON7 series dual at about 5 mΩ, if the load exceeds 3 A.
- **Sense resistor:** 2 kΩ on the V– sense line.
- **Charger placement:** IP2326 charges PACK+/PACK–, *outside* the protector, so the protector can block charging on a cell fault.

---

## 5. Cell Balancing

**Approach:** Use a passive bleed balancer with one **HY2213-BB3A** (balance start 4.20 V) per cell. Each IC drives a bypass resistor through an internal or external FET.

| Item | Value |
|---|---|
| Bleed resistor | 62 Ω 0.5 W (≈ 68 mA at 4.2 V, 0.28 W) |
| Placement | Away from NTC, with thermal relief |
| Expected effect | Corrects roughly 70 mAh of imbalance per hour at top of charge |

**Interaction with the charger:** Balancing only happens in the CV phase, so it needs time at 8.4 V. With 1 A charge and a ~100 mA termination, the CV tail gives modest balancing. For better balance, lower the termination current or use 100 mA bleed.

**Cell matching:** Use matched cells (≤ 20 mV, same lot). Passive balancing is maintenance-only and cannot correct a mismatched pack.

---

## 6. If USB-C PD (9/12/15 V) Is Mandatory

Choose one of these architectures instead:
1. **IP2366 / IP2368.** Injoinic parts with integrated PD sink/source and a buck-boost charger supporting 2S and above. This is the cleanest single-chip option.
2. **CH224K (PD trigger, set to 9 V or 12 V) + buck 2S charger,** such as BQ24133 or CN3302-class. Add OVP on VBUS sized for 20 V.
3. **Keep IP2326,** but never request above 5 V. This is the Cycle 1 baseline.

---

## 7. Layout Notes

- Keep the boost loop (SW node, inductor, output cap, GND) minimal. Run SW over a solid GND, away from the CC lines.
- Use Kelvin routing for the battery sense and mid-tap.
- Put protection MOSFETs on wide copper with a thermal pour.
- Keep the 62 Ω balancing resistors ≥ 10 mm from cells and NTC.

---

## 8. Cycle 1 Bring-up and Test Plan

1. **Power-up:** Current-limited 5 V supply, no battery. Check IP2326 standby and LED states.
2. **Charge profile:** Use an electronic load or battery simulator. Verify CC current, 8.40 V ±0.5 % CV, and termination.
3. **USB-C sources:** Test with a legacy USB-A to C cable and a 3 A PD charger. Confirm VBUS stays at 5 V and the input current matches the advertised capability.
4. **Protection trip points:** Use adjustable supplies per cell to check OV, UV, OCP, and short-circuit. Short-circuit response time should be < 500 µs.
5. **Balancing:** Pre-imbalance cells by 100 mV. Charge, then log cell voltages and bleed current.
6. **Thermal:** IR camera at 1 A and 1.5 A charge. IP2326 should stay below 85 °C and the bleed resistors below 100 °C.
7. **NTC:** Cold and hot inhibit checks.

## 9. Open Items for Cycle 2

- Confirm IP2326 pin map, ISET formula, and whether there is internal balancing (current datasheet revision).
- Decide whether to add CC-voltage-based input current limiting.
- Freeze protector IC suffix thresholds with the cell vendor's spec.
- Decide whether PD at 9 V or more is a real requirement. If yes, migrate to IP2368.

If you share cell chemistry/capacity, target charge time