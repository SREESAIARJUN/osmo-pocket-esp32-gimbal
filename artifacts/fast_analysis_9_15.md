# 2S Li-ion Pack: IP2326 Charger + Protection + Balancing + USB-C Input (Rev. "Cycle 9")

I don't have the earlier cycles, so this is a self-contained design. Please paste the Cycle 8 schematic or requirements if you want a delta review instead.

## 0. Design Constraints on the Original Request

Two parts of the request conflict with what the IP2326 does. Confirm both against the current Injoinic datasheet.

1. **The IP2326 has no USB PD sink capability.** It is a 5 V-input synchronous boost charger for 2S cells, rated at about 15 W. It uses the Type-C CC pins only for basic attach and 5V/3A current advertisement through Rd. A PD controller would negotiate 9 V or higher, which exceeds the IP2326's input rating.
   - **Option A (recommended):** Keep the IP2326 and use plain Type-C 5V/3A (Rd = 5.1 kΩ). This gives about 15 W without a PD controller.
   - **Option B:** If PD fast charging is a hard requirement, swap the charger for a PD-capable buck-boost part (e.g., Injoinic IP2368/IP2369, or an IP2721/CH224K PD trigger feeding a 9 V-input 2S buck charger). That is a different design, outlined in §6.
2. **The IP2326 does not provide pack protection or cell balancing.** These need separate ICs, covered in §3 and §4.

## 1. Block Diagram

```
USB-C ──► VBUS TVS/fuse ──► IP2326 (5V→8.4V boost CC/CV) ──► PACK+ 
  CC1/CC2 ─► IP2326 CC (or 5.1k Rd)            │
                                              ├── Cell top (8.4V)
                     HY2213 balancer ×2 ──────┤── Mid-tap (4.2V)
                                              └── Cell bottom (B−)
                     HY2120 2S protector ──► FS8205A dual NMOS ──► PACK−
```

## 2. Charger: IP2326

| Parameter | Value / Note |
|---|---|
| Input | 4.5–5.5 V from USB-C VBUS |
| Charge voltage | 8.4 V (4.2 V/cell) |
| Input power | ~15 W (5 V × 3 A max) |
| Battery current | ≈ 15 W × 0.9 / 8 V ≈ **1.6–1.7 A** at mid-charge |
| Inductor | 2.2–4.7 µH, Isat ≥ 5 A, low DCR (<20 mΩ), shielded |
| Input caps | 2× 22 µF X5R 10 V + 100 nF at VIN |
| Output caps | 2× 22 µF X5R **25 V** (derating at 8.4 V) |
| NTC | 10 kΩ B3950 on the cell, wired to the NTC pin |
| LEDs | Per the datasheet's status pins |

Implementation notes:
- **Type-C CC:** Use the IP2326's CC pins if your variant has them. Otherwise fit 5.1 kΩ Rd on each of CC1 and CC2 to GND. Never fit a single shared resistor.
- **Charge current:** Set the input-current limit (resistor or default per datasheet) to stay ≤ 3 A from VBUS.
- **Power stage layout:** Keep the switch node small. Use a solid GND plane. Place the output caps tight to the VOUT/GND pins.

## 3. Pack Protection: HY2120-CB (or equivalent 2S protector) + FS8205A

| Function | Threshold (typ.) |
|---|---|
| Over-charge | 4.28–4.30 V/cell, release ~4.10 V |
| Over-discharge | 2.8–3.0 V/cell, release ~3.0 V |
| Over-current (discharge) | V_DS sense ~150 mV → with 8205A (~2×20 mΩ) ≈ 3.5–4 A |
| Short circuit | ~1.2 V/µs-scale trip |

Component choices:
- **MOSFETs:** FS8205A dual NMOS, 20 V / 6 A, common-drain in the PACK− path. It is adequate for 2S at 8.4 V. Use 2× parallel or a 30 V part (e.g., AO8810 class) if continuous load is above 3 A.
- **RC filters:** VDD 330 Ω + 0.1 µF, VC (mid-tap) 1 kΩ + 0.1 µF, VM 2.2 kΩ, following the datasheet's reference values.
- **Fuse:** Add a series fuse or PTC (5 A) on PACK+ as a secondary protection layer.

The charger's 8.4 V CV must sit comfortably below the protector OV (4.30 V/cell). Keep IP2326 V_CV accuracy at ±0.5%.

## 4. Cell Balancing: HY2213 ×2 (bypass/bleed type)

There is one HY2213 per cell, each referenced across its own cell (Cell1: B−→Mid, Cell2: Mid→B+).

| Part | Value |
|---|---|
| Balance threshold | ~4.20 V (choose suffix variant, e.g., -BB3A, verify) |
| Bleed transistor | Small NMOS/NPN (e.g., AO3400) driven by the HY2213 OUT |
| Bleed resistor | 47 Ω 1 W → ≈ 90 mA at 4.2 V (~0.37 W) |
| Decoupling | 100 Ω + 1 µF per VDD |

How it works: whichever cell reaches 4.2 V first bleeds about 90 mA while the other catches up. This is top balancing only, so it works best with matched cells (capacity within 2%, IR within 10%).

Design rules:
- **Taper vs. bleed current:** The IP2326 CV taper current must fall to roughly the bleed current for balancing to be effective. Do not terminate charge too early. If termination is around 150–200 mA, raise the bleed to about 150 mA (27 Ω, 2 W) and add thermal relief.
- **Thermal:** Keep the bleed resistors away from the cells and the NTC.

## 5. Input Protection and Misc

- **VBUS TVS:** SMBJ5.0A (or 6.0A) TVS on VBUS. Add an optional 3 A PTC or eFuse.
- **ESD:** ESD diodes (e.g., ESD5Z/TPD2E) on CC1/CC2.
- **Ground split:** PACK− and charger GND join only at the B− side, after the protector's sense point. Keep the protector VM reference correct.
- **Mid-tap wiring:** Run a dedicated thin trace from the cell junction to both the protector VC and the balancer. Do not share it with any high-current path.

## 6. If PD Is Mandatory (Option B Outline)

```
USB-C ─► IP2721/CH224K (request 9 V, fallback 5 V) ─► 2S buck/buck-boost charger ─► pack
```

- **Charger options:** IP2368 integrates the PD sink plus a buck-boost 2S–6S charger with about 60–100 W capability. Alternatively, use a BQ25887, a 2S boost charger with **integrated cell balancing** (5 V input only, I²C).
- **Unchanged blocks:** Protection (§3) and balancing (§4) stay the same.

## 7. Validation Checklist (Cycle 9)

- [ ] V_CV = 8.40 V ±40 mV at 25 °C
- [ ] OV trip at 4.28–4.30 V per cell, using a programmable supply on each cell
- [ ] UV trip and recovery
- [ ] Short-circuit test at PACK+/− with no MOSFET damage (scope V_GS)
- [ ] Balancing: start at 100 mV imbalance and confirm it is below 20 mV after 3 charge cycles
- [ ] Thermal: max of inductor, IP2326, and bleed resistors below 85 °C at 15 W in the enclosure
- [ ] Type-C: 5V/3A source draws 3 A; legacy 5V/0.5 A source, confirm graceful current limit
- [ ] NTC: charge suspends at <0 °C and >45 °C

If you share the Cycle 8 issues (e.g., thermal, imbalance, PD failure), I can focus the next revision on those.