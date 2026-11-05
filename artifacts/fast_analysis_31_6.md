# 2S Li-ion Charger + Protection + Balancing (IP2326-based), Rev 31

## 0. Corrections to the requirements

The request as written combines parts that don't fit together, so the design corrects three points:

1. **The IP2326 is a 5 V-input boost charger.** It takes 5 V USB and steps up to charge a 2S pack (about 8.4 V), at up to roughly 10–15 W depending on the variant. Its input is not rated for 9/12/15/20 V. **Negotiating a higher PD voltage would damage it.**
   - Do not add PD negotiation. Use plain USB-C 5 V/3 A sinking with Rd resistors on CC.
   - If you truly need PD (e.g., 9 V for faster charging), drop the IP2326. Use a PD trigger IC (CH224K/IP2721) feeding a buck charger such as the IP2369, IP2368, or BQ25887, or a 9 V→8.4 V charger.
2. **The IP2326 is a charger, not a pack protector.** It does not provide over-discharge, over-current, or short-circuit protection against the load. A separate 2S protection IC and MOSFET pair is required.
3. **Balancing.** Some IP2326 datasheet revisions describe a balance function, but its current is small and the details vary by revision. Verify against your datasheet. This design adds per-cell balancing (HY2213) so it works either way.

## 1. Block diagram

```
USB-C (5V/3A) ─► Input TVS/fuse ─► IP2326 boost charger ─► PACK+ (B+)
                    │CC1/CC2 5.1kΩ Rd           │
                                                ├─ Cell2 ─┬─ HY2213 #2 (bal)
                                          BM (mid-tap)    │
                                                ├─ Cell1 ─┴─ HY2213 #1 (bal)
                                                B−
                                                │
                       2S protector (HY2120 / S-8252) ─► dual N-FET (common drain)
                                                │
                                              PACK−
```

## 2. USB-C input (sink, 5 V only)

| Item | Value / Part | Notes |
|---|---|---|
| CC1, CC2 | 5.1 kΩ 1% each to GND | Rd. Advertises sink, gets 5 V at up to 3 A from a capable source. |
| VBUS fuse | 3 A PTC or 4 A fast fuse | |
| VBUS TVS | SMF5.0A / SMBJ5.0A | Clamps hot-plug spikes. |
| VBUS bulk | 10 µF + 0.1 µF ceramic | Place close to the connector. |
| ESD on D+/D− | USBLC6-2 (optional) | Only needed if the data lines are routed. |

Do not tie CC1 and CC2 together through a single resistor. Use two separate 5.1 kΩ resistors, or non-compliant chargers may refuse to supply power.

## 3. IP2326 charger section

Take the exact pin names and values from the Injoinic reference schematic for your package.

- **Inductor:** about 2.2–4.7 µH, Isat ≥ 4 A, low DCR (≤ 20 mΩ). Example: a 7×7 mm shielded power inductor.
- **Input cap:** 2 × 10 µF X5R 10 V.
- **Output (BAT) caps:** 2 × 10 µF X5R **25 V**. These see 8.4 V; derate for DC bias.
- **Charge current set:** use the resistor on the ISET/configuration pin per the datasheet table. Start at 1–1.5 A for typical 2000–3000 mAh cells.
- **Termination:** fixed 8.4 V (4.2 V/cell). Confirm your variant; some have 8.6/8.7 V options.
- **NTC:** 10 kΩ B=3435 thermistor taped to the cells, wired to the NTC pin.
- **LED indicators:** per the datasheet, with 1 kΩ series resistors.
- **Thermal:** use a large GND pad under the IC with ≥ 6 thermal vias. The boost loop (VIN cap → inductor → SW → IC → GND) must be tight.

## 4. 2S protection (HY2120-xx or Ablic S-8252)

| Parameter | Typical threshold |
|---|---|
| Overcharge detect / release | 4.28–4.30 V / 4.08–4.10 V per cell |
| Overdischarge detect / release | 2.5–2.9 V / 3.0 V per cell |
| Discharge overcurrent | Set by FET Rds(on) × current (e.g., 150 mV) |
| Short circuit | ~0.6 V across the FETs, <500 µs |

Wiring:

- **VDD** connects to B+ through 330 Ω, with 0.1 µF to VSS.
- **VC (mid-tap)** connects to BM through 1 kΩ, with 0.1 µF.
- **VSS** connects to B−.
- **DO/CO** drive the gates of a dual common-drain N-MOSFET (e.g., 8205A for ≤ 3 A, or an AON7region/AO4800-class part for higher current). Use FETs with 20 V Vds and low Rds(on).
- **VM** connects to PACK− through 2 kΩ.

Choose a protector variant whose overcharge threshold (e.g., 4.28 V) sits above the charger's 4.2 V/cell termination. Otherwise the protector will trip during normal charging.

## 5. Cell balancing (per cell)

Use one **HY2213-BB3A** (or similar) across each cell:

- The IC turns on its bypass at about 4.20 V and drives an external shunt.
- **External shunt:** an SI2302-class N-FET plus a 47–68 Ω, 0.5 W resistor. This gives about 62–90 mA of bleed at 4.2 V.
- Bleed current should be meaningful relative to the taper. With charge termination near 100–150 mA, about 70 mA of bleeding equalizes cells over several cycles.
- Each cell gets a 100 Ω + 0.1 µF RC filter on its sense input.

## 6. Pack-level details

- **Mid-tap (BM) wire:** a thin wire is fine since it carries only the balance and sense currents. Fuse it (0.5 A) if the leads are long.
- **Main path:** traces sized for ≥ 5 A peak, using 2 oz copper or wide pours.
- **Discharge path:** the load connects between PACK+ and PACK−, after the protection FETs.
- **Charger connection:** connect the charger's BAT output to PACK+/PACK−, not B−. The protector can then block overcharge if the charger fails.

## 7. Validation checklist

1. Plug into a USB-C PD charger. Verify VBUS stays at 5 V and the source supplies ≥ 2 A. The Rd resistors are working if 5 V appears with C-to-C cables.
2. Charge from 6.0 V. Check CC/CV behavior, termination at 8.4 V ± 1%, and inductor and IC temperature (< 85 °C).
3. Start with a cell imbalance of ≥ 100 mV. Confirm the high cell's bleed engages near 4.2 V and the cells converge.
4. Test overcharge with an external supply on one cell, raising it to 4.3 V. CO should go low.
5. Test over-discharge with an electronic load. DO should cut off near 2.5–2.9 V/cell.
6. Short PACK+ to PACK− through a 10 mΩ shunt. Confirm cutoff in < 1 ms and that the FETs survive.
7. Check NTC behavior with a hot-air gun and with freeze spray.

---

If "Cycle 31" refers to a specific revision or test requirement in your project, such as a target charge current, cell capacity, or required PD voltage, send it. If PD above 5 V is mandatory, I'll redo the design around a buck-charger topology.