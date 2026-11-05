# 2S Li-ion Charger + Protection + Balancing + USB-C Sink: Design Rev. 10

## 0. Corrections to the Brief

These points change the architecture, so they come first.

1. **IP2326 is a 5 V-input boost charger.** As I understand its datasheet, Injoinic's IP2326 boosts a ~5 V input to charge a 2S pack at 8.4 V, up to about 15 W.
   - It does **not** accept 9/12/15/20 V PD voltages.
   - It does **not** provide pack protection (OV/UV/OC/short-circuit).
   - It does **not** provide cell balancing.
   - Verify these points against the current IP2326 datasheet before layout, because Injoinic datasheets vary by revision.
2. **"PD sink negotiation" with a 5 V-only charger** only makes sense in one form: requesting **5 V @ 3 A**. Plain Type-C Rd resistors (5.1 kΩ) already achieve that with most sources. A PD sink controller is therefore optional. It adds value only if you need to request a fixed 5 V contract explicitly, or to read source capability.
3. If you actually want **9–20 V PD input**, change the charger. See Section 7.

"Cycle 10" isn't a standard term I recognize. I've treated it as design revision 10. If it refers to a specific cycle-life or test requirement, tell me and I'll adjust.

---

## 1. System Architecture

```
USB-C ──► CC (Rd 5.1k / optional PD sink IC) 
   │
VBUS ──► TVS + input FET/fuse ──► IP2326 (5V→8.4V boost CC/CV)
                                        │
                                   PACK+ ───────────────┐
                                        │               │
                          ┌─────────────┴──────┐        │
                          │ Cell 2 (top)       │◄── Balancer B2 (HY2213)
                          │ Cell 1 (bottom)    │◄── Balancer B1 (HY2213)
                          └─────────────┬──────┘
                                        │
                     2S Protector (HY2120 / S-8252 / BQ29209-class)
                                        │
                       Back-to-back N-FETs (low-side) ──► PACK–
```

---

## 2. USB-C Input Stage

| Item | Part / Value | Notes |
|---|---|---|
| CC1, CC2 | 5.1 kΩ 1% to GND each | Sink advertisement; source then offers up to 3 A at 5 V |
| Optional PD sink | CH224K / HUSB238 / IP2721 set to **5 V** | Only needed for an explicit 5 V/3 A PD contract |
| TVS | SMBJ5.0A / SMF5.0A on VBUS | Hot-plug surge protection |
| Input fuse | 4 A PTC or 5 A fast fuse | |
| Reverse/OVP | Load switch with OVP ≈ 6 V (e.g. SY6280 / AW32901-class) | Protects IP2326 if a faulty source puts 9 V+ on VBUS |
| Bulk cap | 22 µF + 100 nF ceramic | Close to IP2326 VIN |

Do **not** use a PD controller that defaults to the highest PDO. It would apply 9–20 V to a 5 V-max boost charger.

---

## 3. IP2326 Charger Stage

Follow the datasheet reference design. Typical values for a 15 W class design:

- **Inductor:** 2.2–4.7 µH, Isat ≥ 6 A, low DCR (≤ 15 mΩ).
- **Boost output caps:** 2–3 × 10 µF/25 V X7R.
- **Charge voltage:** 8.4 V (4.20 V/cell) is the default. Use 8.3 V if there is a cell-life priority, if the IP2326 variant allows selection.
- **Charge current:** set via the ISET resistor per the datasheet table. Size for ≤ 0.5 C of the cell capacity. Example: 2000 mAh cells give a ~1 A charge, which is about 9 W at the pack.
- **NTC:** 10 kΩ B3435 NTC on the cell body. Configure charge inhibit below 0 °C and above 45 °C.
- **Status LEDs:** per datasheet pins.

---

## 4. Cell Balancing (Passive, Per Cell)

Use one **HY2213-BB3A** (or equivalent) across each cell. Its approximate characteristics:

- Balance start ≈ 4.20 V
- Balance stop ≈ 4.15 V
- Drives an external bleed FET/resistor

| Component | Value | Rationale |
|---|---|---|
| Bleed resistor | 47 Ω, 1 W (2512 or 2 × 1206 parallel) | ≈ 89 mA at 4.2 V, ≈ 0.37 W dissipation |
| Bleed FET | AO3400 / SI2302 (if external) | Low Vgs(th) |
| Supply filter | 100 Ω + 0.1 µF per IC | |

**Interaction with CC/CV charging:** the IP2326 taper current should end above the bleed current, or balancing cannot keep up. Set the charge termination current to ≥ 100 mA, or accept slow top-balancing over several cycles.

Balancing of this kind only fixes small mismatches (< 50 mV). Start with matched cells: same lot, ΔV < 10 mV, ΔIR < 5 mΩ.

---

## 5. 2S Protection Stage

Use a **HY2120-CB** (or Seiko S-8252, or another 2S protector). Typical thresholds:

| Function | Threshold | Delay |
|---|---|---|
| Overcharge | 4.28–4.30 V/cell | ~1 s |
| Overcharge release | 4.08–4.10 V | |
| Over-discharge | 2.80–3.00 V/cell | ~100 ms |
| Overcurrent (discharge) | Set by Vdet / (2 × Rds(on)) | ~10 ms |
| Short circuit | ~1.0–1.35 V across FETs | ~250 µs |

**Thresholds must nest correctly:** CV setpoint 4.20 V < balancing 4.20 V < protector OV 4.28 V. The protector is the last line of defense, not the regulator.

**MOSFETs:** dual N-ch common-drain, e.g. FS8205A (light loads) or 2 × AON6512 / CSD17575 (high current).

Overcurrent example: Rds(on) = 5 mΩ per FET, 10 mΩ total. A 0.15 V OCP threshold then trips at ≈ 15 A. Size the FETs for your load.

**Sense filtering:**
- VCC/VC1 RC filters: 330 Ω / 0.1 µF.
- 2 kΩ series resistor on the CS (V–) pin.

---

## 6. Layout & Safety

- **Power paths:** keep the boost loop (SW, inductor, output caps, GND) tight. Use a solid ground plane and a Kelvin connection from the protector sense to the FETs.
- **Thermal management:**
  - Separate the balancing resistors from the cells and the NTC, otherwise they skew the temperature reading.
  - Add copper pours for heat.
- **Clearances:**
  - Pack-tap traces need ≥ 0.3 mm clearance.
  - Fuse every balance tap with 500 mA or a 0 Ω/thin-trace fuse.
- **Pack fuse:** add a secondary fuse (thermal or chemical) on PACK+ for UL/IEC 62133 compliance.

---

## 7. Charger Alternatives If You Need Real PD (9–20 V)

| IC | Input | Balancing | Notes |
|---|---|---|---|
| **TI BQ25887** | 5 V boost | **Integrated** cell balancing | Cleaner 2S solution than IP2326 + HY2213 |
| **Injoinic IP2368 / IP2366** | PD 5–20 V buck-boost | No | Integrated PD sink; add HY2213 + HY2120 |
| **TI BQ25713 / BQ25731** + TPS25750 | PD up to 20 V | No | High power, I²C-controlled |

---

## 8. Validation Checklist (Rev. 10)

1. **Input:**
   - Plug in a 5 V/3 A source and confirm VBUS stays within 4.75–5.25 V under full charge load.
   - Verify the input OVP trips on a forced 9 V input.
2. **Charge profile:** confirm the CC current, the 8.40 V ±0.5% CV voltage, and termination.
3. **Balancing:** inject a 100 mV imbalance and confirm convergence to < 20 mV within N cycles.
4. **Protection:** using a programmable supply in place of the cells, verify each threshold:
   - Overcharge (OV)
   - Over-discharge (UV)
   - Overcurrent (OCP)
   - Short-circuit response time
5. **NTC cutoffs:** verify charge inhibit at 0 °C and 45 °C.
6. **Thermal:** run a 1 h soak at max charge. The IP2326 and inductor should stay under 85 °C.

If you share the cell capacity, max load current, and whether PD >5 V is required, I can provide exact resistor values and a BOM.