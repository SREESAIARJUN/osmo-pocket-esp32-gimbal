# 2S Li-ion Charger, Protection, Balancing and USB-C PD Sink Design

## 0. Constraints That Shape the Design

**IP2326 is a charger, not a protection IC.** It is a USB-C, 5 V input, synchronous boost charger for 2S packs (8.4 V CV), roughly 10 to 15 W. It does not provide:
- Independent cell overvoltage, undervoltage or overcurrent cutoff.
- Per-cell balancing.
- USB PD negotiation. It is a 5 V-only Type-C sink.

**A boost charger cannot accept PD voltages.** A 9 V, 12 V or 15 V PD contract would exceed the 8.4 V battery. Forcing 9 V into a boost-only topology is unsafe, because the input passes straight through the inductor and high-side switch to the battery.

You therefore have two valid architectures. Pick one before laying out the board.

| Option | Input | Charger | PD | Max charge power |
|---|---|---|---|---|
| **A: Keep IP2326** | 5 V Type-C (no PD) | IP2326 boost | None. Rd = 5.1 kΩ sink only | ~10 to 15 W |
| **B: True PD sink** | 5 to 20 V PD | Buck-boost charger | PD sink controller | 30 to 60 W |

Both options share the same protection and balancing stage (Section 2).

"Cycle 16" isn't a term I recognize in IP2326 or PD documentation. If it refers to your internal design revision or a specific requirement set, send the requirements and I'll adjust.

---

## 1. Block Diagram

```
USB-C ──► [CC handling / PD sink] ──► [Charger IC] ──► PACK+ ─┬─ Cell 2 (top)  ─┐
                                                              │   [Balancer 2]  │
                                                              ├─ Cell 1 (bot)  ─┤
                                                              │   [Balancer 1]  │
                                                    [2S Protector + dual N-FET] │
                                                              │                 │
                                                           PACK– ◄──────────────┘
```

---

## 2. Protection and Balancing Stage (Common to Both Options)

### 2.1 Primary Protection: HY2120 (or S-8252 / BQ29700-class 2S equivalent)

Use a variant with these thresholds:

| Parameter | Typical setting |
|---|---|
| Cell overvoltage (OVP) | 4.28 V ±25 mV, release 4.08 V |
| Cell undervoltage (UVP) | 2.90 V, release 3.00 V |
| Discharge overcurrent | ~150 mV across FET Rds(on) |
| Short circuit | ~1.35 V, < 500 µs |
| Charger detect / overcurrent | Built into V– pin |

### 2.2 Protection FETs

Use an **FS8205A** (dual common-drain N-MOSFET, ~25 mΩ per FET at 4.5 V) or a lower-Rds(on) part such as the AON7410 pair for currents above 5 A.

The overcurrent trip is set by the FET resistance:

> I_trip ≈ V_OC / (2 × Rds(on)) = 0.15 V / 0.05 Ω ≈ **3 A**

For a higher trip point, choose lower-Rds(on) FETs. Rds(on) drifts with temperature, so allow about ±30% tolerance.

### 2.3 Protector Wiring

```
B+ (top of Cell2) ──R1 330Ω──┬── VDD (HY2120)
                             C1 0.1µF
B_mid ─────────R2 330Ω───────┬── VC (cell-1 sense)
                             C2 0.1µF
B– ───────────────────────── VSS
DO ──► Gate of discharge FET (Q1)
CO ──► Gate of charge FET (Q2)
V– ──R3 2.2kΩ──► PACK–
Q1/Q2 common drain; Q1 source = B–, Q2 source = PACK–
```

### 2.4 Cell Balancing: One HY2213-BB3A per Cell (~4.20 V Bypass Threshold)

```
Cell n+ ──R_bal──┬── VDD (HY2213)
                 │   OUT ──► Gate of small N-FET (e.g., AO3400) ──► R_bypass to Cell n–
Cell n– ─────────┴── VSS
```

- **R_bypass:** 56 to 62 Ω, 0.5 W (1206 or 2512 package). This gives about 68 to 75 mA of bleed at 4.2 V, or about 0.3 W per resistor.
- **Bleed current limit:** keep the bleed current at least 3% of the CV termination current. Otherwise the charger can terminate before the cells are balanced.
- **Alternative:** the BQ29209 combines 2S overvoltage protection with automatic balancing (external R/FET) in one IC. It does not cover undervoltage or overcurrent, so you still need the HY2120.

### 2.5 Additional Protection

- **Pack fuse:** add a 1.5× I_max slow-blow fuse or a PTC in series with B+. This is a secondary, non-resettable or thermal backup.
- **Temperature sensing:** place a 10 kΩ NTC (B = 3435) against the cells, wired to the charger's TS/NTC pin.

---

## 3A. Option A: IP2326 Charger (5 V, No PD)

- **CC pins.** IP2326 performs Type-C sink detection internally on most package variants. Verify this in your datasheet revision. If your variant lacks CC pins, put 5.1 kΩ Rd from CC1 and from CC2 to GND.
- **VBUS input.** Use 10 µF + 0.1 µF X7R capacitors. Add a TVS (SMBJ6.0A or SMF5.0A) at the connector.
- **Inductor.** Use about 2.2 µH with Isat ≥ 5 A and low DCR (≤ 20 mΩ), per the datasheet value.
- **Output to battery.** Use 2 × 10 µF X7R, rated 16 V or higher.
- **Charge current.** Set it with the ISET resistor per the datasheet table. Typical values are 1 to 1.5 A for 15 W.
- **LEDs.** Add charge-status LEDs per the reference design.
- **CV target.** 8.4 V. Confirm that the CV accuracy (±0.5%) stays below 2 × OVP (8.56 V) with margin, so the protector never trips in normal operation.

**Do not substitute or add specific pin numbers or register values from memory.** Pull the current Injoinic IP2326 datasheet and reference schematic, because pinouts differ between package variants.

---

## 3B. Option B: True USB-C PD Sink (Recommended If You Need PD)

**Single-chip route.** The **IP2368** (Injoinic) is a buck-boost charger for 2 to 6S packs with integrated PD 3.0 / QC sink and source. Configure it for 2S. This keeps the same vendor ecosystem and has the lowest part count.

**Discrete route.** Use **STUSB4500**, **HUSB238** or **CH224K** as the PD sink, paired with a **BQ25792** buck-boost charger (1 to 4S, 3.6 to 24 V input).

- **PD request.** Request 9 V/3 A or 12 V/3 A. Fall back to 5 V when PD is not available.
  - With CH224K, the CFG resistor selects the voltage. For 9 V, follow the CH224K datasheet table.
  - With STUSB4500, program the PDOs in NVM.
- **Load switch.** Gate VBUS to the charger with a P-FET load switch. Close it only when the PD controller reports "power good", so the charger never sees an unexpected voltage.
- **Charger settings (BQ25792).**
  - VREG = 8.4 V
  - ICHG = 2 to 3 A
  - IINDPM matched to the PD contract
  - Wire the TS pin to the NTC.
- **Input protection.** Use a TVS rated for 20 V PD (SMBJ22A) and a 25 V or higher bulk capacitor.

---

## 4. Layout Guidelines

1. Run Kelvin sense traces from each cell tab to the protector VDD/VC RC filters. Do not share these with power current.
2. Keep the charger switching loop (input capacitor, FETs, inductor) under about 1 cm².
3. Place the protection FETs on wide copper and add thermal vias. The FET current path should be at least 3 mm wide for 3 A.
4. Space the