# 2S Li-ion Charger + Protection + Balancing with IP2326 and USB-C Sink

The label "Cycle 29" isn't something I can tie to a standard or prior context. I've treated it as a design revision tag. Tell me if it refers to specific requirements.

## 0. Constraints That Shape the Architecture

| Fact | Consequence |
|---|---|
| The **IP2326** (Injoinic) is a **5 V-input synchronous boost charger** for 2S Li-ion (8.4 V), up to about 15 W. Its VIN must stay around 4.5–5.5 V. | You **must not** negotiate 9 V/12 V PD into it. The useful "PD" outcome is **5 V at up to 3 A**. |
| The IP2326 is a **charger**, not a protector. It has no OV/UV/OC disconnect FETs on the pack. | You need a **separate 2S protection IC + back-to-back MOSFETs**. |
| The IP2326 does not do real per-cell balancing (it regulates total pack voltage). | You need an **external balancer**. |

These points reflect my understanding of the IP2326. Confirm pin numbers, ISET/NTC behaviour and the max VIN against the current Injoinic datasheet before layout.

If you actually want 20–30 W charging from 9/12 V PD, swap the IP2326 for a buck-boost/buck charger (e.g., IP2368, BQ25887 at 5 V, BQ25713, SW6306) driven by a PD sink controller. I can redo the design that way.

---

## 1. Block Diagram

```
USB-C ──► CC sink (5.1k Rd or HUSB238 @5V/3A) ──► VBUS TVS + fuse ──► IP2326 boost ──► PACK+ (8.4 V)
                                                                                       │
                     ┌─────────────── 2S pack ──────────────────────────────────────────┤
                     │  Cell2 (top) ── BAL2 ─┐                                          │
                     │  Cell1 (bot) ── BAL1 ─┤  balancers (HY2213 ×2)                   │
                     │                       │                                          │
                     └── B− ─► Protection FETs (2× N-MOS, common drain) ─► PACK− ───────┘
                                 ▲ driven by HY2120 / S-8252 (2S protector)
```

---

## 2. USB-C Sink Front End

**Option A: Simplest, 5 V only (recommended for the IP2326)**
- CC1 → 5.1 kΩ ±1 % → GND; CC2 → 5.1 kΩ ±1 % → GND (Rd, sink).
- Optionally read the CC voltage with an MCU or comparator to detect source capability:
  - 0.25–0.61 V → Default USB (500/900 mA)
  - 0.70–1.16 V → 1.5 A
  - >1.31 V → 3.0 A
- Set the IP2326 input current limit to match. Default to about 1.5 A if the CC level is unknown.

**Option B: True PD negotiation (5 V/3 A contract, e-marker not needed)**
- Use **HUSB238** (or IP2721/CH224K) as the sink controller.
- Configure it to request **5 V** only. On the HUSB238, the VSET strap selects 5 V, and the ISET strap selects 3 A.
- Use its PG/EN output to enable the IP2326 only after the contract is established.
- Never let it default to 9 V. Add a **6.2 V TVS + OVP load switch** (e.g., an SY6280-class switch or a P-FET + comparator) as a hard backstop.

**VBUS protection (both options)**
- 3 A PTC or 4 A fast fuse.
- SMBJ5.0A TVS on VBUS.
- 10 µF + 100 nF ceramic at the connector.
- ESD diodes on CC1/CC2 (e.g., TPD2E2U06).

---

## 3. IP2326 Boost Charger Stage

| Item | Value / Choice | Notes |
|---|---|---|
| Inductor | 2.2 µH, Isat ≥ 6 A, DCR < 15 mΩ | 5 V → 8.4 V at 1.5 A charge gives about 3 A input current plus ripple |
| VIN caps | 2× 22 µF X5R 10 V + 100 nF | Place close to the VIN/PGND pins |
| VOUT/BAT caps | 2× 22 µF X5R **16 V** | Accounts for DC-bias derating at 8.4 V |
| Charge voltage | 8.4 V (4.2 V/cell) | Some variants have an 8.6/8.7 V option; keep 8.4 V for life |
| Charge current | ~1.5 A (≈ 13 W) via ISET resistor | Use the datasheet table; derate to ~1 A if only 1.5 A input is available |
| NTC | 10 kΩ B=3435 on the cell | Wire to the IP2326 NTC pin for 0–45 °C charge window |
| LEDs | Per datasheet (charging/full) | — |

Charge time for a 2600 mAh 2S pack is about 2 h CC + 0.5 h CV.

---

## 4. 2S Protection (Primary Safety Layer)

**IC:** HY2120-CB or Ablic **S-8252** (2-series protector).

**FETs:** 2× N-MOSFET in common-drain configuration in the B− path. Examples are FS8205A (dual, 20 V, ~25 mΩ) or AO8810. For more than 3 A discharge, use 2× AON6970-class parts.

| Parameter | Typical threshold (choose suffix) |
|---|---|
| Overcharge (per cell) | 4.28–4.30 V, ~1 s delay |
| Overcharge release | 4.08–4.10 V |
| Over-discharge | 2.80–3.00 V |
| Over-current (discharge) | V_DS sense ≈ 0.15–0.2 V gives ~3–4 A with 2×25 mΩ |
| Short circuit | ~250 µs trip |

**Support passives (typical):**
- VDD: 330 Ω + 100 nF.
- VC (midpoint): 1 kΩ + 100 nF.
- VM: 2.2 kΩ.

The **protector OV threshold (4.28 V) must sit above the charger CV (4.20 V)** and above the balancer start point. That keeps the protector from false-tripping during normal top-off.

---

## 5. Cell Balancing

**Recommended: passive top-balancing, one balancer per cell.**
- Use an **HY2213-BB3A** across each cell. It turns on at about 4.20 V and turns off at about 4.15 V (check the suffix).
- Bleed resistor: **39 Ω, 1 W**.
  - At 4.2 V this bleeds about 108 mA, roughly 0.45 W per resistor.
- Cell 1 balancer references B−/BM. Cell 2 balancer references BM/B+.
- Each balancer needs its own 100 Ω + 100 nF filter.

**How the balancing works with the charger:**
- During CV the IP2326 holds 8.40 V total.
- If one cell reaches 4.20 V first, its bypass bleeds about 100 mA so the lagging cell catches up.
- Bleed current must be a meaningful fraction of the CV taper. Termination at about 10 % of 1.5 A = 150 mA, so ~100 mA works.

**Alternative:** TI **BQ29209** is a 2S overvoltage protector with integrated balancing control (external bleed FETs/resistors). It reduces part count, but you still need the HY2120/S-8252 for UV/OC.

**Thermal:** Bleeding produces about 0.9 W worst case. Keep the bleed resistors away from the NTC and the cells, or the charger will see false over-temperature.

---

## 6. Rule Order

| Layer | Per-cell voltage |
|---|---|
| Charger CV (IP2326) | 4.20 V |
| Balancer ON | ~4.20 V |
| Protector OV trip | 4.28–4.30 V |
| Optional secondary fuse/BQ2970-class 2nd-level OV | 4.35 V |
| Protector UV trip | 2.9 V |
| System low-battery shutdown (if load MCU) | 3.2 V |

---

## 7. Layout Notes

- Keep the boost hot loop tight: SW pin → inductor → VOUT cap → PGND. Use a solid PGND pour with stitched vias.
- Run Kelvin traces for the cell taps (B+, BM, B−) straight to the cell tabs. Don't share them with charge-current copper.
- The protection-FET path carries full pack current. Size it for at least 2 oz copper or wide pours.
- Put the NTC on the cell, not on the PCB.
- Keep CC lines short with the ESD diodes at the connector.
- Isolate the bleed resistors from the NTC and cells.

---

## 8. Key BOM

| Ref | Part | Qty |
|---|---|---|
| J1 | USB-C 16-pin receptacle | 1 |
| U1 | HUSB238 (opt.) or 2× 5.1 kΩ | 1 |
| U2 | IP2326 | 1 |
| L1 | 2.2 µH 6 A shielded | 1 |
| U3 | HY2120-CB / S-8252 | 1 |
| Q1 | FS8