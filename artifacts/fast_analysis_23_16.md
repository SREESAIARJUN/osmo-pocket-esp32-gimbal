# 2S Li-ion Pack Design: IP2326 Charger, Protection, Balancing, and USB-C Input (Rev. Cycle 23)

## 0. Design Assumptions and Caveats

**About the IP2326:** It is a **5 V-input, synchronous-boost, 2S charger** rated at about 10–15 W. It does **not** include:
- Cell protection (OV/UV/OC/short-circuit cutoff)
- Active or passive cell balancing
- USB PD protocol logic

So "protection," "balancing," and "PD sink" each need their own block.

**About PD:** The IP2326 boosts from 5 V, so PD negotiation adds little. A plain Type-C sink with 5.1 kΩ Rd resistors already gets 5 V at up to 3 A (15 W), which matches the IP2326 ceiling. A PD sink controller is still included below as an option, for two cases:
- You want guaranteed 5 V/3 A from PD-only sources.
- You plan to later migrate to a buck-boost charger that uses 9/12 V.

**About part data:** Pin numbers are intentionally omitted. Pull them from the current Injoinic IP2326 datasheet (the Chinese version is the most complete). Treat every value marked *"verify"* as a starting point.

**Target specs:**

| Parameter | Value |
|---|---|
| Pack | 2S1P, 18650/21700 or pouch, 4.20 V/cell (8.40 V full) |
| Charge current | ~1.5 A at 8.4 V (≈12.6 W in → ~14 W from USB) |
| Discharge | up to 5 A continuous |
| Balancing | passive bleed, ~60–80 mA, near top of charge |
| Input | USB-C, 5 V/3 A sink |

---

## 1. System Block Diagram

```
 USB-C ──► [CC/PD sink] ──► VBUS 5V ──► [TVS + input cap] ──► IP2326 (boost charger)
                                                               │  VOUT ~8.4V CC/CV
                                                               ▼
                         ┌──────────────────── PACK+ ◄─────────┘
                         │
                    Cell 2 (top)  ◄── balance bleed (Q_B2 + R_B2)
                     BM (mid-tap) ──► protector VC/balancer sense
                    Cell 1 (bottom) ◄─ balance bleed (Q_B1 + R_B1)
                         │
                       B−  ── [2S protector: DO/CO] ── back-to-back NMOS ── PACK−
                                       │
                                     NTC (shared or separate)
```

---

## 2. USB-C Input / Sink Negotiation

### Option A: Recommended for the IP2326 (no PD IC)
- CC1 → 5.1 kΩ 1% → GND
- CC2 → 5.1 kΩ 1% → GND
- The source advertises its current via Rp. Optionally monitor CC voltage with a small MCU/comparator to detect Default/1.5 A/3 A, and lower the IP2326 charge current when only 500–900 mA is available. Some IP2326 variants auto-adapt via input-voltage droop (VIN regulation), which is usually enough.

### Option B: PD Sink Controller
Use **CH224K**, **IP2721**, or **HUSB238**:
- Configure the request strap for **5 V** (CH224K: CFG pin strap; HUSB238: Vset resistor).
- Use the PG output to enable the IP2326 (EN/KEY pin, *verify*) only after the contract is established.
- **Do not request 9 V/12 V** unless the IP2326 datasheet explicitly allows that VIN. Its abs-max is set around a 5 V boost design (*verify*).

### Input Protection (both options)
- **TVS:** SMBJ5.0A (or SMF6.5A for margin) VBUS→GND, at the connector.
- **Polyfuse or eFuse:** 3 A hold (e.g., 1812L300), or a TPS25940-class eFuse for hot-plug inrush control.
- **Capacitance:** 10 µF X5R (≥16 V) at the connector and 22 µF ×2 at the IP2326 VIN. Keep connector-side capacitance ≤10 µF to stay within the USB inrush spec.
- **ESD:** a 2-ch array (e.g., ESD5V3U2U) on CC1/CC2.

---

## 3. IP2326 Charger Stage

### Key Components
| Item | Value | Notes |
|---|---|---|
| Boost inductor | 2.2–3.3 µH, Isat ≥ 6 A, DCR < 15 mΩ | Peak input current is ≈3.2 A plus ripple |
| VIN cap | 2×22 µF X5R 10 V | Placed tight to VIN/PGND |
| VOUT/BAT cap | 2×22 µF X5R **25 V** | Accounts for DC-bias derating at 8.4 V |
| Charge-current set | per datasheet resistor (*verify*) | Set ~1.5 A, or lower for small cells (≤0.5 C) |
| NTC | 10 kΩ B=3435, on the cell | Charge disabled outside 0–45 °C |
| LEDs | 4× status, per datasheet | Optional |

### Settings
- **CV voltage:** 8.40 V (the 4.20 V/cell variant). Confirm the suffix and option, since some variants support 8.7 V for 4.35 V cells.
- **Trickle:** the IC handles pre-charge below about 6 V. This works with protector UV recovery, covered in the integration section.
- **Thermal:** QFN exposed pad to a ≥4-via array into a 2 oz ground pour. Expect about 0.8–1.2 W dissipation at full power.

---

## 4. 2S Protection

### Recommended Protector ICs
| IC | Balancing | Comment |
|---|---|---|
| **HY2120-CB** (HYCON) | No | Cheap; pair it with a separate balancer |
| **S-8252** (ABLIC) | No | Precise ±25 mV OV |
| **BQ29209** (TI) | **Yes** (2nd-level OV + auto balancing) | Use it as a balancer/2nd protector alongside HY2120 |
| **HY2213-BB3A** ×2 | Per-cell balancer only | 4.20 V balance threshold |

**Baseline design:** HY2120 (primary protection) + 2× HY2213 (balancing).
**Alternative:** BQ29209 + HY2120 for redundant OV.

### Thresholds (choose a variant code)
| Parameter | Target |
|---|---|
| Overcharge detect | 4.275–4.30 V/cell |
| Overcharge release | 4.10–4.15 V |
| Overdischarge detect | 2.80–3.00 V |
| Overcurrent (VINI) | ~100–150 mV across FET Rds(on) |
| Short circuit | ~0.5–1 V, <500 µs |

### Protection FETs
- Use a dual common-drain NMOS, 20 V, ≤5 mΩ per FET (e.g., an AO8814/FS8205A class part for ≤3 A, or 2× SiR-class 30 V 2 mΩ parts for 5 A).
- **OC trip setting:** with VINI = 0.15 V and 2×4 mΩ = 8 mΩ in the path, the trip is ≈ 18 A. That is too high for a 5 A design.
  - Either use a protector with a sense-resistor input,
  - or choose higher-Rds FETs (2×12 mΩ → ~6 A trip, accounting for the Rds tempco).
- Gate pins: DO → discharge FET gate, CO → charge FET gate. Add 1 kΩ series resistors, plus 1 MΩ gate-source resistors if the datasheet recommends them.

### RC Filters
- VDD: 330 Ω + 0.1 µF from the top cell.
- VC (mid-tap): 1 kΩ + 0.1 µF.
- VM (current sense): 2 kΩ to PACK−.

---

## 5. Cell Balancing (Passive)

### HY2213 Per Cell
- Detection is ~4.20 V ±25 mV. Above that, its OUT pin drives an external FET.
- **Bleed path per cell:** an AO3400 NMOS plus a 56 Ω 1 W resistor gives I ≈ 4.2/56 ≈ 75 mA and P ≈ 0.31 W.
- Use 2× 110 Ω 0.5 W in parallel to spread heat, and keep them away from the NTC.

### How Balancing Works with the IP2326 CC/CV Loop
- The IP2326 regulates **total** pack voltage. During CV taper, a high cell crosses 4.20 V, and the HY2213 bleeds it while the low cell continues charging.
- The charger's termination current must be **greater than the bleed current**, or the charger may never terminate. Set termination to about 150–200 mA, or reduce bleed to ~50 mA.
- **Effective balance capacity:** 75 mA × ~30 min of CV ≈ 37 mAh per cycle. This is fine for matched cells. For mismatched or aged packs, expect convergence over several cycles.

### Alternative
Use the **BQ29209** in place of the HY2213s. It cell-balances internally at low current (~10 mA internal, external FET possible) and also gives redundant OV protection.

---

## 6. Integration Details

1. **Charger connection:** The IP2326 BAT/VOUT connects to **PACK+**, and its GND connects to **PACK−**, outside the protection