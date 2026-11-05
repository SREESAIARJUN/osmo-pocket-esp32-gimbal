# 2S Li-ion Charger + Protection + Balancing with IP2326 and USB-C Sink

I don't know what "Cycle 25" refers to. I've treated it as a design revision label. Tell me if it means a specific requirement set, such as a cycle-life target or a project spec.

## 1. Feasibility check: what the IP2326 does and doesn't do

The IP2326 (Injoinic) is a **5 V-input synchronous boost charger for 2S Li-ion (8.4 V)**, rated around 15 W.

| Requirement | IP2326 alone? | Solution |
|---|---|---|
| 2S CC/CV charging from 5 V | ✅ Yes | IP2326 |
| Cell over-voltage, under-voltage, over-current, and short-circuit protection | ❌ No (it is a charger, not a protector) | Dedicated 2S protector + dual N-MOSFET |
| Cell balancing | ❌ No | Per-cell bleed balancer ICs |
| USB-C PD sink negotiation | ❌ No (5 V input only) | See section 4 |

**Key consequence:** PD negotiation to 9/12/15/20 V is **pointless and dangerous** here, because the IP2326 input is a 5 V rail. Negotiating a higher voltage would destroy it.

The correct USB-C approach is a **Type-C sink with 5.1 kΩ Rd on CC1/CC2**. This lets the source advertise 5 V at up to 3 A. If you truly need PD (more than 15 W, faster charging), swap the charger (section 4).

## 2. System block diagram

```
USB-C ──VBUS──[Fuse 3A]──[TVS SMAJ5.0A]──┬── IP2326 VIN (boost charger)
 CC1 ──5.1k── GND                        │          │
 CC2 ──5.1k── GND                        │       BAT+ (8.4V CV)
                                         │          │
                                ┌────────┴──────────┴──────────┐
                                │ PACK+                         │
                                │  Cell 2 (top)  ── Balancer B2 │
                                │  ── VMID (tap) ──             │
                                │  Cell 1 (bot)  ── Balancer B1 │
                                │ B−                            │
                                │  [Protector IC]──CO/DO──┐     │
                                │        Q1/Q2 back-to-back N-FET│
                                │ PACK−                         │
                                └───────────────────────────────┘
```

## 3. Detailed circuit design

### 3.1 USB-C input (5 V sink, no PD)
- **CC1, CC2:** 5.1 kΩ ±1% to GND each. Use separate resistors, never one shared resistor.
- **VBUS protection:** 3 A polyfuse or fast fuse, then a 5 V TVS (SMAJ5.0A), then 10 µF + 100 nF ceramic at the connector.
- **Optional current-advertisement read-back:** feed the CC voltages to an MCU ADC to detect Default/1.5 A/3 A. Then set the IP2326 input current limit so a 500 mA/900 mA legacy port isn't overloaded.
- **USB 2.0 D+/D−:** leave unconnected, or short D+/D− for BC1.2 DCP detection if the charger supports it.

### 3.2 IP2326 charger
Pin names and numbers vary by package revision, so **verify against the current Injoinic datasheet**. Typical external parts:

- **Boost inductor:** 2.2–4.7 µH, Isat ≥ 5 A, low DCR (<20 mΩ). Example: 4.7 µH 6×6 mm shielded.
- **VIN capacitance:** 2× 10 µF X5R 10 V + 100 nF.
- **BAT output capacitance:** 2× 10 µF X5R **25 V** (derate for DC bias at 8.4 V).
- **Charge voltage:** 8.4 V (4.20 V/cell). Some variants allow 8.7 V for 4.35 V cells; use the 8.4 V option for standard cells.
- **Charge current:** set via its configuration resistor. Target ≤0.5–0.7 C of the cell capacity. With a 2000 mAh pack, about 1 A is reasonable (8.4 V × 1 A ≈ 8.4 W, well within a 5 V/3 A input).
- **NTC:** 10 kΩ B=3435 thermistor bonded to the cells on the NTC pin, for JEITA-style charge cut-off.
- **LED pins:** status LEDs with series resistors per the datasheet.
- **Layout:**
  - Keep the VIN cap → SW → inductor → BAT cap loop tight.
  - Use a solid ground pour with thermal vias under the exposed pad.

### 3.3 2S protection (independent of the charger)
Use a dedicated 2S protector, e.g. **HY2120-xB** family or **Seiko S-8252/S-8254**, with dual N-MOSFETs in the B− path.

| Parameter | Typical threshold choice |
|---|---|
| Overcharge detect (per cell) | 4.28–4.30 V (above charger CV, so it only acts on fault) |
| Overcharge release | ~4.10 V |
| Over-discharge detect | 2.8–3.0 V |
| Discharge over-current | Set by FET Rds(on) × sense threshold |
| Short-circuit | Fast (~µs) trip |

- **MOSFETs:** dual N-channel, common-drain, VDS ≥ 20 V, Rds(on) ≤ 10 mΩ (e.g. 8205A-class for <3 A, or AON/Vishay 30 V dual for higher current). The over-current trip point ≈ V_OCD / (2 × Rds(on)).
- **Cell sense filtering:** 330 Ω–1 kΩ series + 0.1 µF on each VC pin.
- **Supply:** 330 Ω + 0.1 µF RC on the protector VDD.
- **Charger connection:** IP2326 BAT+ goes to **PACK+**, and its GND goes to **PACK−** (after the FETs). The protector can then block charging on overcharge.

### 3.4 Cell balancing
Passive bleed balancing, one IC per cell. An example is **HY2213-BB3A** (≈4.20 V balance start), driving an external transistor or bleed resistor.

- **Bleed resistor:** about 4.2 V / R. For ~60–100 mA, use R ≈ 47–68 Ω, rated 1 W (0.26–0.38 W dissipated; use 2512 or 2× 1206 in parallel).
- **Effectiveness:** balancing only acts near full charge, so it corrects small drift (tens of mV). It cannot rescue mismatched cells. **Use matched cells** (capacity within 2%, IR within 5 mΩ).
- **Charger interaction:** the IP2326 terminates on total pack voltage. At CV with a low termination current (~C/10), balance bleed has some time to act. If drift is a concern, prefer a lower termination current.
- **Alternative:** some 2S protectors integrate balance (e.g. TI BQ29209 for overvoltage protection plus balance). This simplifies the BOM if you add separate UV/OC protection.

### 3.5 Balance tap wiring
- Run the midpoint (VMID) tap to both the protector VC pin and the balancer ICs with its own trace, not the high-current path.
- Add a 0.5–1 A fuse or a 0 Ω/PTC in the tap lead to protect against a tap short.

## 4. If you actually need USB-C PD
To charge faster than about 15 W, replace the IP2326 rather than bolting PD onto it:

- **Integrated option:** **IP2368** (Injoinic, 2–6S buck-boost, built-in PD/PPS sink and source, up to ~100 W). This gives the same vendor ecosystem and a single chip for PD plus charging.
- **Discrete option:** a PD sink controller (**CH224K**, **HUSB238**, or **AP33772**) requesting 9 V or 12 V, feeding a buck-boost charger such as **BQ25713** / **BQ25703A**. That charger accepts 9–20 V and charges 2S directly.

The protection and balancing sections (3.3–3.5) stay identical in either case.

## 5. Abbreviated BOM (5 V / IP2326 version)

| Ref | Part | Note |
|---|---|---|
| J1 | USB-C 16-pin receptacle | Power + CC |
| R1, R2 | 5.1 kΩ 1% | CC Rd |
| F1 | 3 A fuse | VBUS |
| D1 | SMAJ5.0A | VBUS TVS |
| U1 | IP2326 | Charger |
| L1 | 4.7 µH, Isat ≥ 5 A | Boost inductor |
| C_in | 2× 10 µF 10 V | |
| C_out | 2× 10 µF 25 V | |
| TH1 | 10 kΩ NTC B3435 | On cells |
| U2 | HY2120 / S-8252 | 2S protector |
| Q1 | Dual N-FET 20–30 V, ≤10 mΩ | B− switch |
| U3, U4 | HY2213-BB3A | Balancers |
| R_bal | 2× 56 Ω 1 W | Bleed |

## 6. Validation checklist
1. **Charge profile:** verify CV is 8.40 V ±0.5% at no load and the termination current is correct.
2. **Overcharge protection:** force one cell to