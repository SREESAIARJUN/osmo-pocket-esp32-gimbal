# 2S Li-ion Charger + Protection + Balancing with IP2326 and USB-C Input

## 1. Constraints That Shape the Design

**IP2326 is a charger, not a protector.**
The IP2326 (Injoinic) is a 5 V-input synchronous boost charger for 2S Li-ion (8.4 V termination, up to about 15 W class). It handles:
- CC/CV charging
- Trickle charge
- NTC monitoring
- LED status

It does not handle over-discharge, over-current, or short-circuit protection on the discharge path. A dedicated 2S protector IC is still needed.

**USB-C PD negotiation to 9/12 V is not appropriate here.**
The IP2326 is designed for a nominal 5 V input. Negotiating 9 V+ would exceed its input rating and could destroy it. The correct USB-C sink behavior for this chip is:
- Present Rd = 5.1 kΩ on CC1 and CC2.
- This gets 5 V at up to 3 A from any Type-C source, with no PD controller required.

If you specifically need PD (e.g., 9 V/2 A for faster charging), change the charger. Options are in Section 6.

**Balancing.**
I'm not confident the IP2326 integrates cell balancing, so check the current datasheet. The design below adds external per-cell balancing, which works either way.

**"Cycle 20".**
I don't have context for this label. If it refers to a previous iteration or a specific requirement set, share what changed and I'll adapt.

## 2. Block Diagram

```
USB-C ──► VBUS (5V) ──► [Input TVS + bulk cap] ──► IP2326 boost charger ──► BAT+ (8.4V)
  CC1/CC2: 5.1k Rd each to GND                                                │
                                                             ┌────────────────┤
                                               Cell 2 ═══╪═ HY2213 bal. (cell 2)
                                                   mid ──┤      (BM tap)
                                               Cell 1 ═══╪═ HY2213 bal. (cell 1)
                                                         │
                                       2S protector (HY2120 / S-8252 class)
                                                         │
                                        Dual N-MOSFET (DO/CO) in BAT− path
                                                         │
                                                       PACK−
```

## 3. Section Details

### A. USB-C Input (Sink, 5 V Only)

| Item | Value | Notes |
|---|---|---|
| CC1, CC2 | 5.1 kΩ ±1% to GND each | Separate resistors. Do not tie CC1 and CC2 together. |
| VBUS TVS | SMBJ5.0A or similar | Hot-plug and ESD clamp |
| Input cap | 10 µF ceramic + 47–100 µF bulk | Place near the IP2326 VIN pin |
| Optional | PTC or eFuse (e.g., 3 A) | Protects against faulty cables/sources |

Optional current-capability sensing: read the CC voltage with an MCU ADC:
- ~0.41 V → Default USB
- ~0.92 V → 1.5 A
- ~1.68 V → 3.0 A

You can then reduce the IP2326 charge current on weak sources.

### B. IP2326 Charger

Follow the datasheet reference schematic exactly. Typical elements:
- **Boost inductor:** about 2.2–4.7 µH, Isat ≥ 4–5 A, low DCR (e.g., a 6×6 mm shielded part).
- **Output caps on BAT:** 2–3 × 10 µF/25 V X7R ceramic.
- **Charge-current set resistor:** per the datasheet table. Keep input current ≤ 3 A at 5 V. That gives roughly 1.5 A into the pack at 8.4 V with ~90% efficiency.
- **NTC:** 10 kΩ B3950, thermally bonded to the cells, wired to the NTC pin.
- **LED pins:** status indicators.
- **Layout:** keep the VIN → inductor → LX → BAT loop as small as possible, with a solid ground plane.

### C. Cell Balancing (Passive, per Cell)

Use one HY2213-BB3A (or equivalent single-cell balance IC) per cell, threshold about 4.2 V.

Each IC drives a small N-FET (e.g., AO3400) that switches a bleed resistor across its cell:
- **Bleed resistor:** 47 Ω (~90 mA at 4.2 V, ~0.4 W). Use a 1 W part or 2 × 100 Ω in parallel, rated 0.5 W each.
- **Wiring:**
  - Cell 1 balancer: between B− and BM (mid-tap).
  - Cell 2 balancer: between BM and B+.
- **Thermal:** keep bleed resistors away from the cells and the NTC.

Note that bleeding only happens near full charge. If the cells are badly mismatched, balancing is slow. Start with matched cells.

### D. 2S Protection

Use a 2S protector IC, for example:
- HY2120-CB
- Ablic S-8252 series
- TI BQ2970-class for 2S variants

| Function | Typical threshold |
|---|---|
| Over-charge (per cell) | 4.25–4.30 V. Must sit above the balancer threshold and the charger's 4.2 V/cell. |
| Over-discharge | 2.5–2.8 V/cell |
| Discharge over-current | Set by FET Rds(on) × trip voltage. Target about 1.5–2 × max load. |
| Short circuit | Fast trip, ~µs range |

**Dual MOSFETs:**
- Common-drain dual N-FETs (e.g., FS8205A or an AON-series dual) with VDS ≥ 20 V. That is fine for an 8.4 V pack.
- Low Rds(on) sets your current-sense accuracy.

**Supply filtering:**
- VDD and VC (mid-cell sense) RC filters: 100–470 Ω + 0.1 µF, per the protector datasheet.

**Placement:** the protector sits between the cells and PACK−. The charger output connects to PACK+/PACK−, so the charger is also protected by the CO FET.

## 4. Threshold Coordination

Thresholds must be ordered from lowest to highest:

```
Charger CV          8.40 V   (4.20 V/cell)
Balancer start      ~4.20 V/cell
Protector OVP       4.25–4.30 V/cell
Protector OVP rel.  ~4.10–4.15 V/cell
Protector UVP       2.5–2.8 V/cell
```

The protector OVP must never be below the charger CV divided by 2 plus its tolerance. Otherwise the pack will chatter near full charge.

## 5. Test Checklist

1. **USB-C sources:** verify 5 V on the following, and that no source supplies more than 5 V:
   - A PD charger (it should stay at 5 V because no PD request is made)
   - A legacy A-to-C cable
   - A laptop port
2. **Charge profile:** check the CC current, the CV at 8.40 V ±0.5%, and the termination current.
3. **Balancing:** use an intentionally mismatched pair (e.g., 4.0 V / 4.2 V).
   - Confirm the high cell bleeds.
   - Confirm no cell exceeds the OVP limit.
4. **Protector trips:** use a programmable supply in place of the cells to test:
   - OVP
   - UVP
   - Over-current
   - Short circuit to PACK−
5. **Thermal:**
   - Charge at full current in a 40 °C ambient.
   - Check inductor, IC, and bleed resistor temperatures.
   - Confirm the NTC cutoff works.

## 6. If You Actually Need PD Negotiation

Options for higher-voltage input:
- **Simplest — PD-capable charger:** use a buck-boost charger with integrated PD. For example, Injoinic's IP2368 handles PD sink/source and 2S–6S buck-boost. Verify the part's current specs before committing.
- **Discrete approach:** pair a PD sink trigger (CH224K, HUSB238, or AP33772) requesting 9 V or 12 V with a charger whose input accepts that voltage. For example, a 2S buck charger from TI's BQ257xx family, or similar. Some 2S chargers with integrated balancing exist (e.g., TI BQ25887, which is 5 V-input boost).
- **Always:** add an input OVP load switch so a misnegotiated 20 V cannot reach the charger.

Tell me your target charge current, pack capacity, and whether "Cycle 20" implies particular specs. I can then give exact resistor values against the specific datasheets you're using.