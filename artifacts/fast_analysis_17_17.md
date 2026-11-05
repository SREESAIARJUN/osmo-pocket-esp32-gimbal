# 2S Li-ion Charger + Protection + Balancing with IP2326 and USB-C Input

## 0. Important corrections before designing

These points change the architecture. Please verify each against the current Injoinic datasheet for your exact part revision.

1. **IP2326 is a 5 V-input boost charger, not a PD device.** It accepts about 4.5–5.5 V from USB and boosts it to charge a 2S pack (8.4 V), at up to about 15 W. It cannot accept 9, 12 or 15 V PD contracts, and it has no PD/CC logic.
   - **Consequence:** "PD sink negotiation" adds nothing here. The correct USB-C sink behaviour is **Type-C current advertisement at 5 V**, using Rd = 5.1 kΩ on CC1 and CC2. That draws up to 5 V/3 A from any compliant source with no protocol.
   - If you really need PD (9 V+ input for faster charging), switch to the **IP2368** (PD sink/source plus 2–6S charger), or use a **CH224K / IP2721 trigger** feeding a buck-boost charger such as the BQ25887 or BQ25713. Do not feed PD voltages into the IP2326.
2. **IP2326 is a charger, not a protector.** It does not provide independent per-cell over-voltage, under-voltage, over-current or short-circuit cutoff. A separate 2S protection IC is mandatory for safety.
3. **Balancing:** treat IP2326 balancing as absent or unverified unless your datasheet revision explicitly documents a midpoint pin. The design below adds **independent per-cell balancing**, so it works either way.
4. **"Cycle 17":** I don't recognise this as a standard or spec. If it refers to your internal design iteration or a requirement document, send me its constraints (charge current, pack capacity, enclosure, cost target) and I'll adapt the values.

---

## 1. Block diagram

```
USB-C ──► VBUS TVS/fuse ──► IP2326 (5V→8.4V boost CC/CV) ──► PACK+ ─┐
  CC1/CC2: 5.1k Rd to GND                                           │
                                                                    │
   Cell 2 (top) ── HY2213 balancer #2 ──┐                           │
   ─── MID (cell tap) ──────────────────┼── HY2120 2S protector ────┤
   Cell 1 (bottom) ─ HY2213 balancer #1 ┘          │                │
                                            CO / DO gates           │
   B– ── [FS8205A dual NMOS, low side] ── P– (system/charger GND)   │
                                                                    ▼
                                                              LOAD (P+/P–)
```

---

## 2. USB-C input (sink, 5 V only)

| Ref | Part | Notes |
|---|---|---|
| J1 | USB-C 16-pin receptacle (power + CC) | Tie both VBUS pin pairs and both GND pin pairs together |
| R1, R2 | 5.1 kΩ 1 % | CC1 → GND and CC2 → GND, as **separate** resistors (never share one) |
| F1 | 3 A PTC or 4 A fast fuse | Placed in series with VBUS |
| D1 | SMF5.0A / SMBJ5.0A TVS | Placed on VBUS to GND, at the connector |
| C1 | 10 µF + 0.1 µF X7R, 16 V | Keep total VBUS capacitance ≤10 µF (USB inrush limit) |
| D2, D3 (opt.) | ESD diodes on CC (e.g. TPD2E2U06) | Recommended for exposed connectors |

**Optional source-current detection:** sense the CC voltage with an ADC or comparator to detect the source's advertised current.
- About 0.25–0.61 V means Default USB power.
- About 0.70–1.16 V means 1.5 A.
- Above about 1.31 V means 3 A.

You can use this to tell the IP2326 to reduce charge current on weak sources. Most designs instead rely on the IP2326's input-voltage-regulation (VBUS droop) loop.

---

## 3. IP2326 charger stage

Follow the datasheet reference circuit exactly; the values below are typical starting points.

| Item | Typical value | Notes |
|---|---|---|
| Inductor | 2.2 µH (check datasheet range), Isat ≥ 6 A, DCR < 20 mΩ | Shielded, e.g. a 0630-size molded type |
| VIN caps | 2 × 10 µF X7R 16 V | Place close to the VIN/PGND pins |
| VOUT/BAT caps | 2 × 22 µF X7R 25 V | Rated for 8.4 V with derating |
| Charge current set | Resistor per datasheet | 15 W at about 90 % efficiency gives roughly 1.5 A at 8.4 V; derate to 1.0 A for small cells |
| Termination | 8.40 V CV (4.20 V/cell) | Fixed internally; confirm which variant you have |
| NTC | 10 kΩ B3435 on the pack | Wire to the IP2326 NTC pin to inhibit charging outside 0–45 °C |
| LED / status | Per datasheet | |

**Charge-current rule:** keep charge current ≤ 0.5–1 C of the cell rating. With 2 × 2000 mAh 18650s, 1.5 A (0.75 C) is fine.

---

## 4. 2S protection (mandatory)

**IC: HY2120-CB** (or equivalents S-8252, CW1032).

| Parameter | Typical HY2120 threshold |
|---|---|
| Over-charge detect / release | 4.28 V / 4.08 V per cell |
| Over-discharge detect / release | 2.80–2.90 V / 3.00 V per cell |
| Discharge over-current | Set by FET Rds(on), about 0.15–0.20 V at VM |
| Short-circuit | About 1.0–1.35 V at VM, fast cutoff in ~µs |

**Wiring:**
- VDD connects to B+ through 330 Ω, with 0.1 µF to VSS.
- VC (the midpoint sense) connects to MID through 1 kΩ, with 0.1 µF to VSS.
- VSS connects to B–.
- VM connects to P– through 2.2 kΩ.
- The CO and DO outputs drive the gates of the dual MOSFET.

**MOSFETs:** FS8205A or AO8810 dual N-channel, common-drain, placed on the low side between B– and P–.
- They are rated 20 V, which gives good margin over 8.4 V.
- Rds(on) is about 25 mΩ per FET at 4.5 V gate drive, so the over-current trip is roughly 0.15 V / 0.05 Ω ≈ 3 A.
- For higher load current, parallel FETs or use lower-Rds(on) parts. The trip current scales accordingly.

**Secondary protection:** add a PTC or fuse on P+, sized at about 1.5× the maximum load current.

**Charger connection:** the IP2326 battery output must go to **P+ / P–** (the protected side), not directly to the cells. That way the protector can block a runaway charge.

---

## 5. Cell balancing

The IP2326 itself charges the pack as a whole and does not see the midpoint. To keep the cells matched, add a passive bleed balancer on each cell.

**Option A (simplest): 2 × HY2213-BB3A single-cell balancer ICs**
- Each one turns on a bleed path when its cell exceeds about 4.20 V.
- Bleed resistor: 47–68 Ω, 0.5 W (1206 × 2 in parallel, or 2010 size). At 4.2 V that bleeds about 62–89 mA.
- Use an external small NMOS (e.g. AO3400) if your HY2213 variant has an open-drain output; some variants switch internally.
- Balancer #1 sits across B– to MID. Balancer #2 sits across MID to B+.

**Option B: MCU-controlled bleed**
- Measure both cells through divider plus ADC, with high-value dividers (≥1 MΩ) or gated dividers to avoid draining the pack.
- Turn on a bleed FET for any cell more than 20 mV above the other during CV phase.

**Sizing check:**
- In CV phase, current tapers toward about 100–150 mA.
- An 80 mA bleed can therefore meaningfully hold back the higher cell.
- Passive balancing corrects slow drift, not badly mismatched cells. Use matched cells (same lot, ΔV < 10 mV, ΔC < 2 %).

**Thermal:** each bleed resistor dissipates about 0.35 W. Spread copper and keep them away from the NTC and cells.

---

## 6. Layout guidelines

1. **Boost power loop** (IP2326 SW → inductor → output caps → PGND): keep it as small as possible, on a solid ground plane