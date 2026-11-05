# 2S Li-ion Charger + Protection + Balancing with IP2326 and USB-C Input

## 0. Design-Basis Corrections

Before the schematic, several assumptions in the request need adjusting:

| Assumption | Reality | Design consequence |
|---|---|---|
| IP2326 does the protection | IP2326 (Injoinic) is a **5 V-input synchronous boost charger** for 2S packs. It is not a cell-protection IC. | Add a dedicated 2S protector (HY2120 / S-8252 class) plus back-to-back FETs. |
| IP2326 balances cells | Its charge control works on total pack voltage. Per-cell balancing is not a core function, so it cannot be relied on. | Add per-cell balancing (HY2213 ×2), or use a charger with integrated balancing (see §7). |
| PD sink negotiation is needed | IP2326 is designed for **5 V input**. Requesting 9/12 V by PD would exceed its input rating. | Use **CC pull-downs only (5 V / 3 A Type-C current)**. A PD sink controller adds risk and no benefit here. |
| "Cycle 24" | Not a recognized spec, revision, or standard. | Treated as a project or revision label. Tell me if it refers to a specific requirement. |

**Verify against the current IP2326 datasheet:** pinout, input OVP limit, and maximum charge current. Injoinic datasheets vary by revision, and some details below (e.g., current-set resistor value) must be taken from your copy.

---

## 1. System Architecture

```
USB-C ──► CC 5.1k Rd ──► VBUS TVS/fuse ──► IP2326 boost charger ──► PACK+ (8.4 V)
                                                 │
                              ┌──────────────────┴─────────────────┐
                         Cell 2 (B2)  ── HY2213 balancer #2         │
                         Cell 1 (B1)  ── HY2213 balancer #1         │
                              │                                     │
                      HY2120 2S protector ── DO/CO ──► Dual N-FET (low side) ──► PACK−
```

- **Input:** 5 V @ up to 3 A (15 W max available).
- **Charge:** 8.4 V CV, around 1–1.5 A CC (limited by 5 V × ~90 % efficiency).
- **Protection:** per-cell OV/UV, discharge overcurrent, short circuit.
- **Balancing:** per-cell bleed at 4.2 V threshold.

---

## 2. USB-C Sink Front End (No PD)

| Ref | Part | Value | Note |
|---|---|---|---|
| J1 | USB-C 16-pin receptacle | — | Power + CC only |
| R1, R2 | CC1, CC2 → GND | **5.1 kΩ 1 %** | Advertises sink; source supplies 5 V |
| F1 | PTC or fuse | 3 A hold / 4 A trip | VBUS |
| D1 | TVS | SMBJ5.0A (or SMAJ6.0A) | VBUS to GND, close to connector |
| C1 | Bulk | 10 µF X7R + 0.1 µF | At connector |

**Optional:** read the CC voltage with an MCU to detect source capability:

| CC voltage | Source advertisement | Action |
|---|---|---|
| 0.25–0.61 V | Default USB | Charge current about 0.5 A |
| 0.70–1.16 V | 1.5 A | Charge current about 1 A |
| >1.31 V | 3 A | Full current |

If you need more than 15 W, see §7.

---

## 3. IP2326 Charger Stage

Typical topology (confirm pin names with the datasheet):

| Function | Component | Guideline |
|---|---|---|
| VIN decoupling | C2 | 2×22 µF X5R 10 V + 0.1 µF |
| Boost inductor | L1 | 2.2–4.7 µH, Isat ≥ 5 A, DCR < 20 mΩ (e.g., 1040 / 1265 size) |
| Output (BAT) caps | C3 | 2×22 µF 16 V X5R minimum (25 V rating preferred for DC-bias derating) |
| Charge-current set | R_ISET | Per datasheet table; start around 1 A |
| Status LEDs | LED1/2 + 1 kΩ | Charging / full |
| NTC | 10 kΩ B3435 on pack | Enables JEITA-style cutoff, if supported |

**Thermal budget:** at 12 W in and about 90 % efficiency, about 1.2 W is dissipated in the IC plus inductor. Use the exposed pad with at least 6 thermal vias to an inner GND plane.

---

## 4. 2S Protection (HY2120 or equivalent)

**Typical thresholds:**

| Parameter | Typical |
|---|---|
| Overcharge (VOC) | 4.25–4.30 V/cell |
| Overdischarge (VOD) | 2.5–2.9 V/cell |
| Overcurrent | Set by FET Rds(on) |

| Ref | Part | Value |
|---|---|---|
| U2 | HY2120-CB (choose variant for your thresholds) | — |
| R3 | VDD filter | 330 Ω |
| C4 | VDD filter | 0.1 µF |
| R4 | Mid-cell (VC1) | 1 kΩ |
| C5 | VC1 | 0.1 µF |
| R5 | VM sense | 2 kΩ |
| Q1 | Dual N-MOSFET common-drain | e.g., AO8810 / FS8205A, Rds(on) ≈ 10–20 mΩ |

**Overcurrent trip:**

I_trip ≈ V_OCD / (2·Rds(on))

Example: 0.15 V / (2×12 mΩ) ≈ 6 A. Choose the FET to give 1.5–2× your peak load current.

**Wiring:** Q1 sits between B− (cell negative) and P− (pack output negative). The IP2326 GND and the load both reference **P−**, so the protector can interrupt charging as well as discharging.

---

## 5. Cell Balancing (HY2213 per cell)

| Ref | Part | Value |
|---|---|---|
| U3, U4 | HY2213-BB3A (4.20 V start) | One across each cell |
| Q2, Q3 | P- or N-FET per app note (e.g., SI2301 / AO3400) | — |
| R_bal | Bleed resistor | 39 Ω 1 W → about 108 mA at 4.2 V |

**Behavior:** when a cell reaches 4.2 V, its bypass sheds about 100 mA. That lets the lower cell catch up during the CV taper.

**Thermal:** each R_bal dissipates about 0.45 W. Use 2512 resistors or two 1206s in parallel, and keep them away from the cells.

**Why balancing is only a top-off:** charge current (1 A+) far exceeds bleed current (~0.1 A). The pack balances properly during the taper phase, not during CC. For poorly matched cells, use matched cells or active balancing.

---

## 6. Layout and Validation

**Layout:**
1. Keep the boost hot loop (IC SW → L1 → output caps → GND) under 1 cm².
2. Use Kelvin-sense routing for the protector's VC1 / VDD taps directly at cell tabs.
3. Make the B− → Q1 → P− path a wide copper pour (≥ 2 oz recommended).
4. Place the TVS within 5 mm of the USB-C pins.
5. Keep the balancing resistors away from the NTC and the cells.

**Test plan:**

| Test | Pass criterion |
|---|---|
| Plug into non-PD 5 V/3 A, PD charger, and legacy USB-A→C cable | Always 5 V; no PD contract above 5 V |
| CV accuracy | 8.40 V ±0.5 % |
| Imbalance: cells at 3.9 V / 4.1 V | Both within 20 mV after taper ends |
| OV injection (power supply on one cell, 4.35 V) | CO opens, charge stops |
| Short P+ to P− | Trip in under 500 µs, auto-recovery on load removal |
| UV discharge | DO opens at VOD |
| Thermal at full charge, 25 °C ambient | IC under 85 °C, R_bal under 90 °C |

---

## 7. If You Truly Need PD or More Power

- **Integrated balancing, 5 V input:** TI **BQ25887**, a 2S boost charger with built-in cell balancing. It is a drop-in replacement for IP2326 + HY2213 ×2.
- **PD 9–20 V input:** use a buck charger, such as **BQ25713 / BQ25798** with a PD sink controller (**TPS25730, STUSB4500, CH224K, or IP2721**). Request 12 V and buck down to 8.4 V for 20–45 W charging. Keep the HY2120 protector and external balancing.
- **Injoinic all-in-one:** **IP2368**, a PD 2–6S buck-boost charger with integrated PD.

---

Send your target charge current, pack capacity, peak load current, and the meaning of "Cycle 24", and I'll tighten the component values (R_ISET, FET selection, protector variant) and produce a full netlist.