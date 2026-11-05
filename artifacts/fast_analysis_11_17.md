# 2S Li-ion Charger + Protection + Balancing with USB-C Input: Design Rev. 11

## 0. Design Constraints

These constraints decide the architecture, so check them before layout.

| Assumption | Reality | Consequence |
|---|---|---|
| IP2326 negotiates USB PD | **IP2326 is a 5 V-input boost charger for 2S cells.** It only presents a Type-C sink (Rd on CC). It does **not** speak USB PD. | PD needs a separate sink controller, and a higher PD voltage must not reach the IP2326. |
| IP2326 protects the pack | It is a **charger**. It does not provide independent cell OVP, UVP, overcurrent or short-circuit cutoff. | Add a dedicated 2S protector and MOSFETs. |
| IP2326 balances cells | Balancing is not something to rely on from the charger. | Add per-cell balancing ICs. |

**Verify every pin number, threshold and absolute-max rating against the current Injoinic IP2326 datasheet and the datasheets of the chosen protector parts.** The values below are typical design targets, not guaranteed part specifications.

---

## 1. Architecture: Two Input Options

### Option A (recommended, simplest): USB-C 5 V only

```
USB-C ──CC1/CC2(5.1k Rd)──┐
VBUS 5V ──TVS──IP2326 (boost 5V→8.4V CC/CV)──► PACK+ (8.4V)
                                               │
                                 [HY2213 ×2 balance] across each cell
                                               │
                         2S protector (S-8252 class) ── dual NFET ── PACK−
```

- USB-C 5 V at up to 3 A gives about 15 W of input, enough for roughly 1.5 A at 8.4 V after boost losses.
- Charge time for a 2S 2500 mAh pack is about 2–2.5 h.

### Option B: PD fast charge (9/12 V)

```
USB-C ──► PD sink controller (CH224K / IP2721 / HUSB238) request 12V
       ──► 2S buck CC/CV charger (8.4V, e.g. IP2368-class or TI BQ2588x-class)
       ──► same protection + balancing stack
```

- **Do not feed PD 9–20 V into the IP2326.** Its input is designed for 5 V.
- If you want PD *and* the IP2326, add an input OVP load switch that limits VBUS to 5 V. In practice Option B replaces the IP2326.

The rest of this document details Option A.

---

## 2. USB-C Front End

| Item | Value / Part | Notes |
|---|---|---|
| CC1, CC2 | 5.1 kΩ ±1% to GND (Rd), unless the IP2326 integrates Rd | Check whether the IP2326 handles CC directly. Never place one shared resistor across both CC pins. |
| VBUS TVS | SMAJ5.0A or a PESD5V-class part | Clamps hot-plug ringing. |
| VBUS bulk capacitance | 10 µF + 0.1 µF X7R | Keep total capacitance modest for inrush; USB allows ≤10 µF at attach. |
| Input current limit | Set by the IP2326 configuration | Detect the source's Rp current advertisement (Default/1.5 A/3 A) so a weak source is not overloaded. |

---

## 3. IP2326 Charger Stage

| Parameter | Target |
|---|---|
| Charge voltage (CV) | 8.40 V (4.20 V/cell), ±0.5% or better |
| Charge current (CC) | 1.0–1.5 A, typically 0.5 C, set per the datasheet |
| Trickle threshold | About 6.0 V pack (3.0 V/cell) |
| Termination | About C/10 |
| Boost inductor | 1–2.2 µH, Isat ≥ 4.5 A, low DCR (≤20 mΩ), shielded |
| Output capacitance | ≥ 2× 22 µF, 16 V X5R/X7R (derate for DC bias) |
| NTC | 10 kΩ B3435 on the pack, wired to the IP2326 NTC pin to stop charging below 0 °C and above 45 °C |
| Status | LED or I/O per the datasheet (charging / full / fault) |

---

## 4. 2S Primary Protection

**Controller:** a dedicated 2S protector, such as the ABLIC S-8252 family, or equivalent from Hycon or Fortune. Select the variant by its thresholds.

| Function | Target threshold | Delay |
|---|---|---|
| Overcharge (per cell) | 4.275–4.30 V | About 1 s |
| Overcharge release | About 4.10–4.15 V | — |
| Overdischarge (per cell) | 2.8–3.0 V | About 100 ms |
| Discharge overcurrent | Set by the voltage threshold ÷ (2 × Rds(on)) | About 10 ms |
| Short circuit | Fast trip | Below 500 µs |

**MOSFETs:**
- Use a dual N-channel, common-drain pair in the low side (for example a 20–30 V 8205A-class part).
- Rds(on) should be ≤ 15 mΩ at Vgs = 4.5 V.
- Example: with Rds = 12 mΩ each (24 mΩ total) and a 0.1 V overcurrent detect threshold, trip current is about 4 A. Size it to roughly 2× your maximum load.

**Supporting components:**
- **Cell sense filters:** 330 Ω–1 kΩ series resistance plus 0.1 µF at each VCx pin.
- **Supply decoupling:** filter the protector VDD the same way.
- **Secondary protection (optional, recommended for products):** an independent 2S OVP (TI BQ2920x/BQ7718-class) driving a fuse, or a simple 5–7 A fuse in series with PACK+.

---

## 5. Cell Balancing

**Topology:** passive top balancing with one balancer per cell, such as the Hycon HY2213 (4.20 V threshold variant).

| Item | Value |
|---|---|
| Balance start | About 4.20 V (variant dependent) |
| Bleed resistor | 39–62 Ω, 1 W 2512 |
| Bleed current | 62 Ω gives about 68 mA at 4.2 V; 39 Ω gives about 108 mA |
| Power per cell | About 0.28–0.45 W, so give the resistors thermal copper |
| Switch | Small N-FET (≥ 20 V, ≤ 100 mΩ) driven by the HY2213 output |

**Charger interaction:**
- Balancing works only near the top of charge.
- The charger's termination current (C/10 ≈ 150 mA) should exceed the bleed current. Otherwise the high cell can stall termination or the protector can trip OVP.
- For cells mismatched by more than 50 mV, expect several cycles before they equalize.

**Cell matching:** start with cells matched within ±20 mV and ±2% capacity. Passive balancing corrects drift, not bad cells.

---

## 6. Bill of Materials (Option A)

| Ref | Part | Qty |
|---|---|---|
| U1 | IP2326 | 1 |
| U2 | 2S protector (S-8252 class) | 1 |
| U3, U4 | HY2213 (4.20 V) | 2 |
| Q1 | Dual N-FET common-drain, 20 V, ≤15 mΩ | 1 |
| Q2, Q3 | Balance N-FET SOT-23 | 2 |
| L1 | 2.2 µH, ≥4.5 A shielded | 1 |
| J1 | USB-C 16-pin receptacle | 1 |
| D1 | TVS SMAJ5.0A | 1 |
| RT1 | 10 k