# 2S Li-ion Charger + Protection + Balancing + USB-C Input (Rev "Cycle 21")

## 0. Key constraint before the design

**The IP2326 is a 5 V-input synchronous boost charger** for 2S packs (8.4 V full charge, roughly 10–15 W class). A boost converter cannot have its input above its output. If a USB-PD source negotiates 9 V, 12 V or 15 V, that input exceeds the 8.4 V pack voltage and falls outside the IP2326's operating range, and it can be damaged.

The design therefore has to choose one of two input strategies:

| Option | Input | PD needed? | Charger | Max practical power |
|---|---|---|---|---|
| **A (recommended for IP2326)** | 5 V @ up to 3 A | No. Type-C Rd sink only; PD optionally used to *lock* 5 V | IP2326 | ~15 W |
| **B (if you truly need >15 W)** | 9–20 V via PD | Yes | Replace IP2326 with a buck-boost/PD charger (e.g. IP2368, or a PD trigger + BQ25713-class buck-boost) | 30–65 W |

The rest of this document details **Option A**, with a hardened "PD sink" front end that guarantees only 5 V ever reaches the IP2326. Section 7 covers Option B.

> ⚠️ Verify every pin number, threshold and resistor equation against the current Injoinic IP2326 datasheet (Chinese/English revision). Injoinic parts vary between revisions, and the values below are design targets, not datasheet transcriptions.

---

## 1. System architecture

```
USB-C ──► CC/VBUS front end ──► VBUS protection ──► IP2326 boost charger ──► PACK+ (8.4 V)
          (Rd 5.1k, or PD     (TVS, eFuse/OVP     (CC/CV, NTC, LED)            │
           trigger fixed 5V)   clamp at ~6 V)                                  │
                                                                 ┌─────────────┴──────────┐
                                                                 │  Cell 2 (top)          │
                                                     Balancer ◄──┤  midpoint (VC)         │
                                                     + 2S prot.  │  Cell 1 (bottom)       │
                                                                 └─────────────┬──────────┘
                                                                     Back-to-back N-FETs
                                                                               │
                                                                            PACK−
```

Responsibilities are deliberately split:

- **IP2326** handles charge regulation: CC/CV, termination, NTC, and charge status.
- **Dedicated 2S protector/balancer** provides an independent safety layer: per-cell OV/UV, overcurrent, short circuit and balancing.
- **Input front end** handles the Type-C sink contract and over-voltage lockout.

Never rely on the charger alone for protection. The protector must work even if the IP2326 fails short.

---

## 2. USB-C sink front end

### 2a. Minimum (no PD silicon)
- CC1 → 5.1 kΩ ±1 % → GND
- CC2 → 5.1 kΩ ±1 % → GND
- A compliant source then supplies only vSafe5V. PD sources never raise VBUS without an explicit PD contract, and you never send one.
- Optional: an ADC/comparator on CC reads the advertised current (Default / 1.5 A / 3.0 A), and the IP2326 charge current is throttled to match. Thresholds are roughly 0.66 V for 1.5 A and 1.23 V for 3 A, across Rd.

### 2b. With a PD trigger ("PD sink negotiation" requirement)
If the spec demands explicit PD negotiation, for example to make power availability deterministic or to log the contract:
- Use a sink controller such as **CH224K**, **HUSB238** or **IP2721**, configured to request **5 V only**:
  - CH224K: strap CFG for 5 V.
  - HUSB238: set the PDO select resistor to the 5 V option.
- Request the 5 V/3 A PDO. If the source offers only 5 V/1.5 A, fall back to a lower charge current using the controller's status pin, or report it to an MCU over I²C on the HUSB238.

### 2c. Mandatory over-voltage backstop
Assume a misbehaving charger or cable will eventually deliver 9–20 V. Add:
- **TVS:** SMBJ5.0A or similar on VBUS. It clamps transients only.
- **OVP load switch:** an OVP eFuse/switch with a ~6.0–6.2 V trip point, rated ≥28 V absolute and ≥3 A (e.g. a TPS2595/TPS25947-class, SY6280-class, or AW32901-class part). This switch is what actually protects the IP2326.
- **Input caps:** 10 µF + 100 nF ceramic after the switch. Keep only ≤10 µF on the connector side, per the Type-C inrush limit.

---

## 3. IP2326 charger stage (5 V → 8.4 V boost)

| Function | Implementation / target |
|---|---|
| Input cap | 2× 10 µF X5R 16 V + 100 nF at VIN pin |
| Boost inductor | 2.2–4.7 µH (per datasheet), Isat ≥ 6 A, DCR < 15 mΩ (e.g. 7×7 mm shielded) |
| Output cap | 3× 10 µF 25 V X5R at BAT (DC-bias derating matters at 8.4 V) |
| Charge voltage | 8.4 V (4.2 V/cell). Lower to 8.2 V for cycle life if selectable |
| Charge current | Set via the ISET resistor/strap. Target ~1.5 A pack current at 5 V/3 A input (≈12.6 W out ÷ ~90 % efficiency ≈ 14 W in) |
| Input current limit | Set ≤ 2.8 A for margin on a 3 A source. If only Default/1.5 A is advertised, reduce to ≤ 1.4 A |
| NTC | 10 kΩ B=3435 thermistor bonded to the cells, wired to the NTC pin per the datasheet divider. Charge window 0–45 °C |
| Status | LED pins → charge/full indicators, or MCU GPIO |
| Precharge / termination | Internal (verify thresholds: typical trickle below ~6 V pack, terminate at ~C/10) |

> Check whether your IP2326 revision has internal balancing. Regardless, the design below provides **independent** balancing so the protection layer stays self-contained.

---

## 4. 2S protection + cell balancing

### Recommended IC: TI **BQ29209** (2S OVP + automatic cell balancing), paired with a 2S protector for UV/OC/SC
Alternatives for the protector are an all-in-one **HY2120 / HY2213 (Hycon)** or a **S-8252 (ABLIC)**-class 2S protector, plus a separate balancer.

### 4a. Primary protector (UV / OV / OC / SC)
| Parameter | Target |
|---|---|
| Overcharge detect | 4.28–4.30 V/cell, release 4.08–4.10 V |
| Over-discharge detect | 2.7–2.8 V/cell, release 3.0 V |
| Discharge overcurrent | Set by FET Rds(on) and protector threshold, ≈ 2× max load |
| Short circuit | ≤ 300 µs delay |
| FETs | Dual N-ch common-drain, back-to-back in PACK− (e.g. 20–30 V, ≤ 10 mΩ total, such as an 8205-class part for low current or AON7xxx-class for >5 A) |
| Supply filter | 330 Ω + 100 nF RC to VDD. 1 kΩ + 100 nF on the VC (midpoint) sense |

### 4b. Balancing (BQ29209 or discrete)
- **Scheme:** passive bleed, active above about 4.0–4.1 V when the cell mismatch exceeds roughly 30–50 mV.
- **Bleed resistors:** 2 × 47–68 Ω, 0.5 W (1206 ×2 parallel or 2010), one per cell, giving ~60–90 mA bleed at 4.2 V.
- **Sanity check:** a 1.5 A charge with 70 mA bleed corrects small mismatches over many cycles. Passive balancing is maintenance, not repair. Badly mismatched cells should be rejected, not balanced.
- **Thermal:** 4.2² / 56 ≈ 0.31 W per resistor. Ke