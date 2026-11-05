# 2S Li-ion Charger/Protection Design: IP2326 + Balancing + USB-C Sink (Revision 2)

## 0. Constraints That Shape the Design

**1. IP2326 is a 5 V-input boost charger.**
- It is a synchronous boost charger that takes ~5 V from USB and boosts it to charge a 2S pack (8.4 V full).
- It does **not** contain a PD controller. It is not designed to accept 9/12/15/20 V input. Check the absolute-max VIN in your datasheet revision, but it is far below PD voltages.
- Therefore "PD sink negotiation" here must mean one of two things:
  - **Type-C current advertisement (recommended):** CC pull-down resistors so the source supplies 5 V at up to 3 A. No PD messaging is needed.
  - **A PD sink controller locked to a 5 V request:** this adds cost and gives no benefit over the first option.
- If you really need 9–20 V PD input, change the charger (see §7).

**2. IP2326 is a charger, not a pack protector, and does not balance cells.**
- Charge termination (CV at 8.4 V) applies to the **total** pack voltage only.
- Per-cell over-voltage, under-voltage, over-current and short-circuit protection must come from a separate 2S protection IC with back-to-back MOSFETs.
- Balancing must be added separately.

**Fix from a typical Rev 1:** do not feed a PD trigger (IP2721, CH224K, etc.) set to 9 V into IP2326 VIN. This is a common failure mode.

---

## 1. Block Diagram

```
USB-C ──► CC1/CC2 5.1k Rd ──► VBUS ─► TVS + Polyfuse/eFuse ─► IP2326 VIN
                                                               │ (boost, L + SW)
                                                               ▼
                                                            BAT+ (8.4 V CV)
                                                               │
                                ┌──────────── 2S PACK ─────────┤
                                │  Cell 2 (top) ── HY2213 bal  │
                                │  Cell 1 (bot) ── HY2213 bal  │
                                └──── B- ──► 2S Protection IC ──► Dual NMOS ──► P- (system GND)
```

---

## 2. USB-C Sink Front End

| Item | Value | Notes |
|---|---|---|
| CC1, CC2 pull-downs | 5.1 kΩ ±1 % each to GND | Each CC pin needs its **own** resistor. Never share one resistor between CC1 and CC2; some chargers will refuse to supply power. |
| VBUS TVS | SMBJ5.0A or SMF5.0A | Clamps hot-plug ringing. |
| Input protection | 3 A polyfuse, or an eFuse (e.g. TPS2595 class) with OVP ~6 V | The eFuse also blocks accidental 9 V+ input. |
| Bulk capacitance | 10 µF ceramic at the connector side | Keeps total VBUS capacitance within the Type-C 10 µF inrush limit. Place more capacitance after the eFuse if one is used. |
| Optional | Read CC voltage via ADC/comparator | Detects 0.9 / 1.5 / 3 A source capability so you can limit IP2326 input current (see the datasheet's input-current/ISET options). |

**Power budget:**
- 5 V × 2 A input ≈ 10 W.
- At ~90 % efficiency, that gives roughly 1.1–1.2 A into the pack at 7.4 V nominal.
- Set the charge current with the IP2326 programming resistor, following the datasheet formula.
- On a 500 mA legacy port, IP2326's input-voltage regulation (VIN droop loop) must be enabled so the host is not collapsed.

---

## 3. IP2326 Charger Stage

Follow the datasheet reference design for exact pin names and values. Key items:

**Inductor**
- Use the value from the datasheet (typically 1.5–2.2 µH).
- Isat ≥ 4 A and low DCR (< 20 mΩ).

**Capacitors**
- VIN: ≥ 2 × 10 µF X5R placed at the pin.
- BAT: ≥ 2 × 10 µF, rated 16 V or higher. A 25 V rating is preferred because of DC-bias derating at 8.4 V.

**NTC**
- 10 kΩ B3950 thermistor, physically bonded to the cells.
- Charging is inhibited roughly below 0 °C and above 45 °C.

**LED / status pins**
- Route them to an indicator or MCU GPIO.

**Thermal**
- Provide an exposed pad with ≥ 6 thermal vias to an inner ground plane.

---

## 4. 2S Protection (Primary Safety)

**IC**
- Use a dedicated 2-cell protector, e.g. Seiko S-8252 series or the HY2120 family (choose the 2S variant).
- Pick a threshold suffix matching your cells:

| Parameter | Typical setting |
|---|---|
| Over-charge detect | 4.25–4.30 V per cell |
| Over-charge release | 4.10–4.15 V per cell |
| Over-discharge detect | 2.5–2.8 V per cell |
| Over-discharge release | ~3.0 V per cell |
| Over-current | 3–5 A, set by FET RDS(on) × detect voltage, or a sense resistor |
| Short circuit | ~10–20 A, < 500 µs response |

**MOSFETs**
- Use a dual common-drain NMOS rated 20 V or higher, e.g. 8205A class (≈ 25 mΩ).
- For more than 3 A, use a lower-RDS(on) part in a PDFN package.

**Supporting parts**
- RC filters on the VC pins: 330 Ω / 0.1 µF.
- 1–2 kΩ series resistor on the V- sense pin.

**Secondary protection (recommended)**
- Add a thermal fuse or a PTC on B+.
- Alternatively, use a secondary OV IC (e.g. BQ29209) that blows a chemical fuse.

---

## 5. Cell Balancing

Use one **HY2213-class shunt balancer per cell** (each sits across one cell):

- **Threshold:** 4.20 V (use the 4.20 V or 4.15 V threshold variant).
- **Bleed path:** an external resistor with a small NMOS or PNP, depending on the variant.
  - 62 Ω gives ≈ 68 mA, dissipating ≈ 0.28 W per cell. Use a 1206 or 2010 resistor.

**How it works with IP2326:**
- Near the end of CV charging, the charger tapers to its termination current.
- Balancers bleed only the high cell, so the pack converges over a few cycles.

**Design rules:**
1. Make the charger's termination current ≥ about 2× the bleed current. Otherwise the high cell may still stop before the low cell catches up.
2. Keep the bleed threshold **below** the protector's OV threshold. With 4.20 V bleed and 4.28 V OV there is 80 mV of margin.

**Alternative:** BQ29209 combines 2S over-voltage protection with internal balancing, at a lower current of about 10 mA. Suitable for small packs.

---

## 6. Layout and Validation Checklist

**Layout**
- Keep the boost loop (VIN cap → IC → inductor → SW → BAT cap) as small as possible.
- Use Kelvin connections for the protector's VC and V- sense lines.
- Route the balance resistors away from the NTC so they do not falsely heat it.

**Tests**
1. Type-C sources at 0.9 / 1.5 / 3 A, plus a non-compliant 9 V source. The eFuse must block the 9 V source.
2. Charge profile: CC, then CV at 8.4 V ± 1 %, then termination.
3. Imbalance test: start the cells at 3.9 V and 4.0 V. Confirm the difference converges to < 20 mV within 3 cycles.
4. Trip points with a programmable supply in place of each cell: OV, UV, OC and