# 2S Li-ion Charger, Protection and Balancing Design Using IP2326 (Cycle 4)

## 1. Corrections to the Requirements

Three assumptions in the request don't match what the IP2326 actually does. They change the architecture.

| Requirement | IP2326 reality | Resolution |
|---|---|---|
| USB-C **PD** sink negotiation | IP2326 is a **5 V-input** boost charger. It supports Type-C sink detection (Rd on CC) but not PD messaging. | 5 V Type-C sink is enough. PD buys nothing because the IP2326 can't accept 9/12 V. See Option B if PD is required. |
| Cell balancing | Not integrated | Add an external balancer per cell (HY2213 ×2) |
| Battery protection | IP2326 does charge termination and NTC only. It provides no OV/UV/OC/short-circuit cutoff on the pack. | Add a dedicated 2S protector IC plus back-to-back N-MOSFETs |

Pin numbers and thresholds below must be verified against your specific datasheet revision. Injoinic documents vary, and several suffix variants exist.

## 2. Architecture (Option A: Recommended)

```
USB-C (5V, Rd sink) ─► Fuse/TVS ─► IP2326 boost charger ─► 8.4V CV / CC ─┐
                                                                         │
                         ┌──── HY2213 balancer (Cell 2) ────┐            │
   PACK+ ◄──────────────┤ Cell 2 (top)                      ├────────────┘
                         ├──── HY2213 balancer (Cell 1) ────┤
                         │ Cell 1 (bottom)                  │
                         └─── 2S protector (OV/UV/OC/SC) ───┘
                                   │
                         Q1/Q2 back-to-back NMOS (DSG/CHG)
                                   │
   PACK– ◄──────────────────────────┘
```

## 3. Block Details

### 3.1 USB-C Input (5 V Sink)

**Rd resistors**
- Use 5.1 kΩ ±1% from CC1 to GND and from CC2 to GND (separate resistors). This advertises a sink, so the source supplies vBus.
- If your IP2326 variant has CC pins, use its internal handling and omit the discrete Rd.

**Protection**
- **Fuse:** 2 A PTC or fast fuse on VBUS.
- **TVS:** SMBJ5.0A (or SMF5.0A) on VBUS.
- **ESD diodes:** low-capacitance ESD on CC1 and CC2.

**Input capacitance**
- Place 10 µF + 100 nF ceramic at the connector.
- Place 22 µF near the IP2326 VIN pin.

**Input power**
- Max ≈ 5 V × 3 A = 15 W if the source advertises 3 A.
- With Rd-only sinking you are guaranteed only Default USB power (500 mA–900 mA, or 1.5/3 A if the source advertises it).
- Program the charger's input current limit accordingly, or have the MCU read CC voltage to detect the advertised current.

### 3.2 IP2326 Boost Charger

**Battery regulation**
- **CV target:** 8.4 V (4.2 V/cell). Some variants are selectable to 8.6/8.7 V for HV cells. Use 8.4 V for standard cells.

**Charge current**
- Set by the ISET or RSET resistor (check datasheet). Typical maximum is ≈1.5 A battery-side.
- Choose about 0.5C of the cell capacity, e.g. 1.0 A for 2000 mAh cells.

**Inductor**
- 2.2–4.7 µH, Isat ≥ 4 A, low DCR (≤20 mΩ).
- Shielded parts, e.g. a 6×6 mm or 7×7 mm power inductor.

**Output capacitance**
- 2 × 22 µF / 16 V X5R on BAT.

**NTC**
- 10 kΩ B=3435 NTC thermally coupled to the cells.
- Charging is inhibited outside about 0–45 °C.

**LED outputs**
- Charge/full indication per datasheet, with 1 kΩ series resistors.

**Ground connection**
- Connect the charger output to PACK+ and to the protector-side PACK– (i.e., after the protection FETs).
- This lets the protector block charging on cell OV.

### 3.3 Cell Balancing: HY2213 ×2

One HY2213 is placed across each cell.

**Operation**
- When a cell exceeds ≈4.20 V (the variant suffix sets the threshold), the HY2213 drives an external bypass.
- The bypass is a PMOS or NMOS plus a bleed resistor.

**Bleed resistor**
- 4.2 V / 39 Ω ≈ 108 mA bleed current, dissipating ≈0.45 W.
- Use 2 × 2512 parts in series or parallel, rated ≥1 W total.

**Why this works**
- The cell that tops off first gets its charge current partially diverted.
- With the IP2326 in CV at 8.4 V, the lagging cell continues to rise until the pair converges.

**Supply decoupling**
- Use 100 Ω + 100 nF on each HY2213 VDD.

**Limitation**
- Balancing only happens near full charge (top balancing).
- Bleed current ≈100 mA versus charge current ≈1 A is adequate for cells that are matched within ~5%.
- It is not a fix for a degraded cell.

### 3.4 2S Protection IC

**Candidate ICs (verify availability)**
- HY2120-series 2S variants.
- Seiko/ABLIC S-8252.
- TI BQ2970-class parts stacked, or a 2S-specific part.

**Typical thresholds**

| Parameter | Value |
|---|---|
| Overcharge detect | 4.28–4.30 V/cell (above the balancer threshold) |
| Overcharge release | 4.08 V |
| Over-discharge detect | 2.5–2.8 V/cell |
| Discharge overcurrent | Sensed via the MOSFET Rds(on) or a shunt; set to ~2× max load |
| Short circuit | Fast trip (<500 µs) |

**MOSFETs**
- Use a dual N-MOSFET, common-drain, e.g. a 20–30 V, <10 mΩ part.
- Place it in the PACK– path as DSG and CHG FETs.

**Filtering**
- Add an RC filter on each cell sense input: 330 Ω + 100 nF.

**Threshold ordering (important)**
- Balance threshold (4.20) < charger CV per cell (4.20) < protector OV (4.28+).
- This prevents the protector from tripping during normal balancing.

## 4. Option B: If USB-C PD Is Mandatory

If you need faster charging from 9 V or 12 V PD adapters, replace the charger chain. Either:

- **Option B1:** Add a PD sink trigger (IP2721, CH224K or HUSB238), set to request 12 V. Follow it with a 2S buck charger, e.g. IP2369, BQ24133 or a similar 2S-capable part.
- **Option B2:** Use a single-chip PD bidirectional 2S solution such as the IP2366 or IP5328P class.
- **Option B3:** Use TI's BQ25887 (5 V input, 2S boost charger) for integrated cell balancing in the charger. Pair it with a 5 V Type-C sink.

## 5. Layout Guidelines

- Keep the IP2326 boost loop tight: SW node → inductor → output cap → GND. Minimize the SW copper area.
- Use Kelvin-connect cell sense lines directly at the cell tabs. Don't share them with high-current traces.
- Put balancing resistors away from the NTC so they don't falsely trigger the temperature limit. Thermally relieve them with copper.
- Route a wide PACK– path through the protection FETs. Add thermal vias under the FETs.
- Use a single-point ground star between the charger power ground and protector sense ground.

## 6. Validation Checklist (Cycle 4)

1. **Input:** Plug in a C-to-C adapter and confirm the source enables VBUS. Test a C-to-A cable as well.
2. **Charge profile:** Verify the CC/CV transition at 8.4 V ±1%.
3. **Balancing:** Pre-imbalance the cells by 150 mV and confirm convergence to <20 mV at termination.
4. **Overcharge:** Force OV by disabling the balancer and confirm the protector cuts the CHG FET at the spec voltage.
5. **Over-discharge:** Confirm UV cutoff and recovery when a charger is connected.
6. **Short circuit:** Short PACK+ to PACK– and confirm the trip time and that the FETs survive.
7. **Thermal:** Confirm the NTC cutoff at 0 °C and 45 °C, and check balancer resistor temperature at full bleed.
8. **Standby current:** Measure total quiescent drain on the pack. Target is <20 µA.

If you share the target cell capacity, max load current, and whether PD is a hard requirement