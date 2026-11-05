# 2S Li-ion Charger and Protection Design: IP2326 + Balancing + USB-C Sink (Rev. 7)

## 0. Two constraints that shape this design

**1. IP2326 is a 5 V-input boost charger. It does not negotiate USB Power Delivery and is not designed for 9 V, 12 V, or 15 V input.**
- Its input is a nominal 5 V (≈4.5–5.5 V operating) from USB, boosted to charge a 2S pack to 8.4 V.
- PD negotiation would gain nothing unless you change chargers. The correct USB-C sink for IP2326 is a **Type-C 5 V / 3 A sink using Rd = 5.1 kΩ on CC1 and CC2**.
- Section 6 shows the alternative architecture if you need PD (15–20 W+).

**2. IP2326 is a charger, not a pack protector.**
- It controls charge current and voltage and provides charge-side cell balancing.
- It does **not** provide independent overcharge, overdischarge, overcurrent, or short-circuit cutoff on the discharge path.
- A separate 2S protection IC and MOSFET pair is mandatory.

> ⚠️ Verify all pin numbers, thresholds, and resistor-programming tables against the current Injoinic IP2326 datasheet revision. Parameters vary between revisions and suffix variants. Values below marked "typ." are design targets, not guaranteed datasheet figures.

---

## 1. System block diagram

```
USB-C receptacle
 VBUS ──┬── PTC/eFuse ── TVS(5V) ── C_in ──► IP2326 VIN
        │
 CC1 ── 5.1k ── GND        (Rd sink: requests default/1.5A/3A
 CC2 ── 5.1k ── GND         from source; no PD stack needed)

IP2326 (sync boost) ── L ── SW ── BAT+ (8.4 V CV)
        │  VM/BM pin ── cell midpoint (balance sense)
        │  NTC ─ 10k NTC on pack
        │  LED/status
        ▼
 ┌──────────── 2S PACK ────────────┐
 │ B+ ── Cell2 ── BM ── Cell1 ── B- │
 └──────────────────────────────────┘
        │ (B+, BM, B-)
        ▼
 2S Protection IC (HY2120-CB class)
   DO ──► Q1 (discharge FET) ┐ FS8205A (dual N,
   CO ──► Q2 (charge FET)    ┘ common-drain) in B- path
   VM ── P- sense
        ▼
 P+ / P-  → load and IP2326 GND reference

 Optional: HY2213 ×2 (per-cell top-balance bleed)
```

**Ground topology:**
- Place IP2326 on the **P−** side of the protection FETs, so the protector can cut the charger as well as the load.
- If you place it on the B− side, the protector can only cut the load. That loses secondary overcharge protection, so avoid it.

---

## 2. USB-C sink front end

| Item | Value | Notes |
|---|---|---|
| CC1, CC2 pulldown | 5.1 kΩ ±1% each to GND | Separate resistors. Never share one resistor between CC pins. |
| Input fuse | 3 A PTC or eFuse (e.g., 5 V/3.5 A trip) | |
| TVS | SMBJ5.0A / 5 V-class unidirectional | Clamps hot-plug overshoot |
| C_in | 2× 22 µF X5R 10 V + 0.1 µF | Close to VIN |
| ESD on CC/D± | Low-cap TVS array | |

**Input current limit:**
- Configure IP2326 for 2 A input, or use its adaptive input-current/voltage-tracking feature.
- This keeps VBUS from collapsing on weak 5 V sources.
- 5 V × 2 A ≈ 10 W in gives roughly 1 A into an 8.4 V pack at ~85–90% efficiency.

---

## 3. IP2326 charger section

| Parameter | Target | Implementation |
|---|---|---|
| CV voltage | 8.40 V (4.20 V/cell) | Fixed internally or by option pin, per datasheet |
| Charge current | ~1.0 A (scale to 0.5 C of your cells) | Set via ISET resistor per datasheet table |
| Pre-charge | ~10% I_chg below ~3 V/cell | Internal |
| Termination | ~10% I_chg | Internal |
| Inductor | 2.2–4.7 µH, I_sat ≥ 4 A, low DCR (<20 mΩ) | Shielded |
| C_out (BAT) | 2× 10 µF 16 V X5R + 0.1 µF | 16 V rating gives margin on 8.4 V |
| NTC | 10 kΩ B=3380/3950, thermally bonded to cells | 0–45 °C charge window |
| Balance sense | Cell midpoint to IP2326 balance pin via ~100 Ω + 0.1 µF | Short, dedicated trace |

**Balancing:**
- IP2326's internal balance works during charge. Its bleed current is small, on the order of tens of mA.
- This is adequate for matched new cells, but it cannot correct large mismatches quickly.

---

## 4. Pack protection (mandatory)

**Protection IC: HY2120-CB (or equivalent: S-8252, BQ2970-class 2S variant)**

| Function | Typ. threshold | Delay |
|---|---|---|
| Overcharge (per cell) | 4.28–4.30 V | ~1 s |
| Overcharge release | ~4.08 V | |
| Overdischarge | 2.8–3.0 V | ~100 ms |
| Overcurrent (discharge) | Set by V_OC / R_DS(on) | ~10 ms |
| Short circuit | ~1.0–1.35 V on VM | ~250 µs |

**MOSFET: FS8205A** (dual N, 20 V, ~25 mΩ at 4.5 V gate) for ≤4–5 A. For higher current, use two separate low-R_DS(on) N-FETs such as AON7410-class, or parallel pairs.

**Overcurrent trip** = V_OCP ÷ (2 × R_DS(on)):
- Example: 0.15 V ÷ (2 × 25 mΩ) ≈ **3 A**.
- R_DS(on) is temperature dependent, so expect roughly ±30% on the trip point.

**Support components:**
- VDD RC filter: 330 Ω + 0.1 µF.
- Cell-sense RC filters: 1 kΩ + 0.1 µF.
- VM resistor: ~1–2 kΩ, which protects the IC against charger reverse connection.

**Charge vs. protection thresholds:** The charger CV (4.20 V) must sit below protector OVP (~4.28 V). Keep at least 50 mV margin after accounting for the tolerance of both parts.

---

## 5. Enhanced cell balancing (recommended for used or mismatched cells)

Add one **HY2213** (single-cell balance IC) per cell:
- Balance threshold ≈ 4.20 V, set by the part suffix.
- Its output drives a bleed path: a small N-FET plus a resistor, or the internal path.

**Bleed resistor sizing:**
- 4.2 V ÷ 56 Ω ≈ 75 mA bleed per cell, about 0.32 W. Use a 1206 or 2010 resistor and spread its heat.
- Size the bleed to be at least 5–10% of the termination current. Otherwise the charger terminates before balancing completes.

---

## 6. If PD is actually required

To charge faster than about 10 W, IP2326 is the wrong part. Use one of these instead:

- **IP2368 / IP2366 (Injoinic):** integrated PD/QC sink plus buck-boost 2–6S charger. This is the closest drop-in replacement.
- **CH224K / AP33772 PD sink + BQ25713 / BQ25703A buck-boost charger:** request 9–15 V via PD, then buck to 8.4 V.
- **Do not** feed a PD-negotiated 9 V+ rail into IP2326's VIN.

The protection and balancing sections (4, 5) carry over unchanged.

---

## 7. Layout rules

1. **Boost power loop:** keep the IP2326 SW → inductor → output cap → GND loop minimal. Use a solid ground pour, and place thermal vias under the exposed pad.
2. **Kelvin connections:** run cell sense and balance traces from the cell tabs, not from the high-current copper.
3. **Protection FETs:** use wide B− and P− copper and place the FETs close together. Give the protection IC's VM and VSS pins their own sense traces.
4. **TVS placement:** place the TVS at the connector, ahead of any long traces.
5. **NTC:** bond it physically to the cells, not to the PCB hot spot.

---

## 8. Validation chec