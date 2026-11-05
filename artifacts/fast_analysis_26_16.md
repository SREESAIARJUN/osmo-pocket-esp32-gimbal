# 2S Li-ion Charger + Protection + Balancing + USB-C Sink Design (IP2326-based)

> **About "Cycle 26":** I don't know what this refers to. It may be a project iteration, course assignment, or revision number. If it implies specific requirements, such as charge current, pack capacity, or form factor, share them and I'll adjust the values.

## 0. Check These Assumptions First

I'm working from general knowledge of the Injoinic IP2326. **Confirm each point against the current datasheet before layout:**

| Assumption | Why it matters |
|---|---|
| IP2326 is a **5 V-input synchronous boost charger** for 2S packs (~8.4 V CV, roughly 10–15 W class). | It cannot accept 9/12/15/20 V PD voltages. |
| It does **not** perform USB-PD negotiation. | PD needs a separate sink controller, or a different charger. |
| It does **not** provide independent per-cell OV/UV/OC disconnect. | A dedicated 2S protector IC plus MOSFETs is required regardless. |
| Its cell balancing is limited or absent (varies by variant). | I add an external balancer so the design doesn't depend on it. |

**Implication for PD:** with a 5 V-only charger, full PD negotiation gains nothing. A standard Type-C sink with 5.1 kΩ Rd resistors already draws **5 V / 3 A (15 W)** from any compliant source. If you truly need PD voltages (faster charging, >15 W), see **Option B** in Section 6.

---

## 1. Block Diagram (Option A: 5 V Type-C, recommended for IP2326)

```
USB-C ──► TVS + Polyfuse ──► IP2326 boost charger ──► 8.4 V CC/CV ──┐
(CC1/CC2: 5.1k Rd)            (NTC, LED status)                      │
                                                                     ▼
                         ┌────── Cell 2 (top) ──┬── Balancer B2 ── B+ / Pack+
                         │                      │
                         ├────── Cell 1 (bot) ──┼── Balancer B1
                         │                      │
                         └── 2S Protector IC (HY2120 / S-8252 class)
                                   │ DO / CO
                              Dual N-FET (back-to-back, low-side)
                                   │
                                 Pack− (P−)
```

---

## 2. USB-C Input Stage

| Ref | Part | Value / Notes |
|---|---|---|
| J1 | USB-C receptacle (16-pin is enough) | VBUS, GND, CC1, CC2 |
| R1, R2 | Rd pull-downs on CC1/CC2 | **5.1 kΩ ±1%**, one per CC line, never shared |
| F1 | Polyfuse | ~3 A hold, 6 V+ |
| D1 | TVS on VBUS | SMBJ5.0A (5 V standoff); use a higher-rated one if PD voltages could appear |
| C1 | Bulk input cap | 22 µF + 100 nF X7R, ≥10 V |
| Q0 (optional) | Reverse-polarity / OVP P-FET | Or a load switch with OVP set ~6 V |

**Optional:** monitor the CC voltage to detect the advertised current (Default / 1.5 A / 3 A). Use it to lower the IP2326 input current limit on weak sources.

---

## 3. IP2326 Charger Stage (pin names per datasheet)

| Function | Typical implementation |
|---|---|
| Boost inductor | 1.0–2.2 µH, Isat ≥ 5 A, low DCR (≤20 mΩ). Check the datasheet's recommendation. |
| Output caps (BAT) | 2 × 22 µF X7R, **≥16 V** (8.4 V nominal; derate for DC bias) |
| Input caps | 2 × 22 µF X7R, ≥10 V, placed tight to VIN/PGND |
| Charge current set | Resistor on the ISET/config pin. Typical 1.0–1.5 A at 8.4 V; must stay under 5 V × 3 A × η. |
| NTC | 10 kΩ B=3435 NTC thermally coupled to the cells |
| Status LEDs | Per datasheet (often 1–4 LED fuel gauge) |
| Charge voltage | 8.40 V (4.20 V/cell). Some variants support 8.7 V for HV cells. **Do not use** with standard cells. |

**Power check:** 8.4 V × 1.5 A = 12.6 W. At ~90% efficiency that draws ~2.8 A from 5 V, so a **3 A Type-C source is required**. On a 1.5 A source, limit charge current to ~0.8 A.

---

## 4. 2S Protection Circuit (independent of the charger)

**Protector IC:** HY2120-CB, Ablic S-8252, or equivalent 2S protector. Select thresholds to match your cells:

| Parameter | Typical setting |
|---|---|
| Overcharge (per cell) | 4.25–4.30 V, ~1 s delay |
| Overcharge release | 4.05–4.15 V |
| Over-discharge (per cell) | 2.5–2.8 V |
| Over-current (discharge) | Set via R_DS(on) sensing or a sense resistor |
| Short-circuit | ~µs-level response |

**Components:**

| Ref | Part | Value / Notes |
|---|---|---|
| U2 | 2S protector | VC1 to the cell midpoint, VDD to B+ via RC filter |
| R_VDD / C_VDD | Supply filter | 330 Ω / 0.1 µF (per datasheet) |
| R_VC1 / C_VC1 | Midpoint filter | 1 kΩ / 0.1 µF |
| Q1/Q2 | Dual N-MOSFET, common-drain | e.g. FS8205A-class only for low current; for >3 A use 30 V, <10 mΩ parts (e.g. AON7xxx/SiR-class pairs) |
| R_VM | Charger-detect resistor | ~2 kΩ from VM to P− |

**MOSFET rule of thumb:** V_DS ≥ 20 V (30 V preferred). Rds(on) is set by your OCP threshold:
I_OCP ≈ V_OCP / (2 × Rds(on)).
Example: 0.15 V / (2 × 10 mΩ) ≈ 7.5 A.

---

## 5. Cell Balancing

**Method:** passive bleed using one dedicated balancer IC per cell (e.g. HY2213-series). It shunts current through a resistor when that cell exceeds ~4.20 V (select the variant threshold).

| Ref | Part | Value |
|---|---|---|
| U3, U4 | 1-cell balancer, one per cell | Threshold ≈ 4.20 V |
| R_bal1, R_bal2 | Bleed resistor | **47–68 Ω, 0.5–1 W** → ~60–90 mA bleed at 4.2 V |
| Q_bal (if needed) | Small N-FET or PNP driven by the balancer output | Per IC application circuit |

**Design notes:**
- Bleed current should be ~5–10% of charge current to be effective during CV taper.
- Heat at 4.2 V: 4.2² / 56 Ω ≈ 0.31 W per resistor. Spread copper, and keep the resistors away from the NTC so they don't fool the thermal sensing.
- The balance threshold must sit **below** the protector OVP (4.20 V vs 4.25–4.30 V). Otherwise the protector trips before balancing occurs.
- Use matched cells from the same batch. Passive balancing corrects small drift, not mismatched cells.

---

## 6. Option B: True USB-PD Sink (if >15 W charging is required)

The IP2326 can't use PD voltages. If PD is a hard requirement, use one of these:

1. **PD trigger + compatible charger:** CH224K/CH224D (or IP2721, HUSB238) requesting 12 V or 9 V, feeding a **buck/buck-boost 2S charger** (e.g. TI BQ25713/BQ25703A, or a 2S buck charger at 12 V in).
   - Note that 9 V into 8.4 V CV is marginal for a pure buck. **Request 12 V**, falling back to 9 V only with buck-boost.
2. **Integrated PD charger SoC:** Injoinic **IP2368** (PD sink/source, 2–6S configurable buck-boost) replaces both the IP2326 and the PD trigger.
3. **If you must keep the IP2326 with a PD trigger:** configure the trigger to request **5 V only**. This is mainly useful with chargers that misbehave without a PD contract. Ensure the VBUS TVS and every component on VBUS tolerate 20 V, in case the trigger fails or a non-compliant source appears.

Protection (Section 4) and balancing (Section 5) stay identical in every option.

---

## 7. Layout & Safety Checklist

- [ ] Keep the boost hot loop (VIN cap → SW → inductor → output cap → PGND) as small as possible, on a solid ground plane.
- [ ] Use Kelvin connections for the protector's VDD/VC1 sense lines, routed directly to the cell tabs rather than through high-current copper.
- [ ] Size the P−/B− tra