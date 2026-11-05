# 2S Li-ion Charger, Protection and Balancing with IP2326 and a USB-C PD Sink (Design Cycle 3)

> **Revision note:** I don't have the content of Cycles 1–2, so this is a self-contained revision. If earlier cycles fixed specific values or part choices, send them and I'll reconcile.

---

## 1. Architecture Problem to Resolve First

The **IP2326 is a 5 V-input synchronous boost charger** for 2S packs (8.4 V CV). This has three consequences:

1. **It has no PD controller.** USB-C/PD negotiation needs a separate sink IC.
2. **It cannot use 9/12/15/20 V PD contracts.** Its input range is around 5 V. Confirm the absolute-max VIN in your datasheet revision. Negotiating 9 V into it will damage it.
3. **It has no protection or balancing.** You need a separate 2S protector and balancer.

So "PD sink negotiation" with IP2326 means **negotiating a 5 V / 3 A contract**. That matters because some PD-only adapters advertise default USB current on Rp but offer 3 A only through a PD contract.

There are two valid paths:

| Option | Input contract | Charger | Max charge power | Recommendation |
|---|---|---|---|---|
| **A** | PD 5 V / 3 A (fallback to Type-C Rp current) | IP2326 (boost) | ~13 W into the pack | Use this if you must keep IP2326 |
| **B** | PD 9 V or 12 V | Buck 2S charger (e.g., TP5100, BQ24133) instead of IP2326 | 18–24 W+ | Better if you need higher power |

The rest of this document details **Option A**, with notes for Option B in section 8.

---

## 2. Block Diagram (Option A)

```
USB-C ──► PD sink (HUSB238, VSET=5V) ──► VBUS protection ──► IP2326 boost charger ──► PACK+ (8.4V CV)
  CC1/CC2     │ PG/flag                    (TVS, PTC/eFuse)        │ NTC, LEDs              │
              └─► IP2326 EN (optional)                             │                         │
                                                                   ▼                         │
                     Cell1 (bottom)  ── HY2213 balancer #1 ──┐                               │
                     Cell2 (top)     ── HY2213 balancer #2 ──┤                               │
                                                             │                               │
                     HY2120 2S protector ── FS8205A dual NMOS (in PACK− path) ── PACK− ──────┘
```

---

## 3. USB-C / PD Sink Stage

**IC:** HUSB238 (Hynetek) is a standalone PD sink with resistor-selected voltage.

- **VSET → 5 V request** (set by resistor, per datasheet table).
- **ISET → 3 A.**
- CC1/CC2 connect directly to the receptacle. Rd is integrated, so dead-battery operation works.
- Its PG/GATE output can drive an optional P-FET load switch so IP2326 only sees VBUS after the contract is established.
- **Fallback behavior:**
  - With a non-PD source, the IC still presents Rd, and you get Rp-advertised current (0.5/1.5/3 A).
  - IP2326's input-voltage regulation (VBUS droop limiting) prevents adapter collapse at lower current ratings.

**Alternative:** if you don't care about PD-only adapters, use just **2 × 5.1 kΩ Rd** on CC1/CC2. This is simpler and cheaper.

### VBUS front end
| Part | Value | Purpose |
|---|---|---|
| TVS | SMBJ5.0A (or SMF5.0A) | ESD/surge clamp; Option A only |
| Fuse | 3 A PTC or eFuse (e.g., TPS25940) | Overcurrent |
| Bulk cap | 22 µF + 100 nF X7R, ≥10 V | Placed at IP2326 VIN |

If the source might ever supply 9 V (e.g., a misconfigured PD trigger), add an **OVP load switch** such as SGM40552 or AP22653, set to about 6 V. This is cheap insurance for IP2326.

---

## 4. Charger Stage: IP2326

Pin numbering varies by package and revision, so the items below are listed by function. Verify the pinout against your datasheet.

- **Output:** 8.4 V CV (4.2 V/cell). Some variants support 8.7 V for 4.35 V cells. Choose the variant to match your cells.
- **Charge current:** up to ~1.5 A at the battery.
- **Power budget:** 15 W in × ~90 % efficiency ≈ 13.5 W, which is ~1.6 A at 8.4 V. IP2326's limit is the binding constraint, so it fits within 5 V / 3 A.
- **Inductor:** value per datasheet (typically 2.2–4.7 µH).
  - Isat ≥ 4.5 A, since input peak current is above 3 A.
  - Low DCR, ≤ 20 mΩ.
  - Shielded (e.g., 6×6 or 7×7 mm).
- **Output cap:** ≥ 2 × 22 µF, 16 V X7R.
- **NTC:** 10 kΩ, B = 3435, thermally coupled to the cells. This gives charge inhibit outside roughly 0–45 °C.
- **LEDs:** status pins as datasheet. IP2326 has an integrated LED driver.

---

## 5. Protection Stage: HY2120 + FS8205A

**HY2120** (HYCON) is a dedicated 2S protector. Choose the suffix to set thresholds; typical values:

| Parameter | Typical | Notes |
|---|---|---|
| Overcharge (OCV) | 4.28–4.30 V/cell | Must be above IP2326 CV (4.2 V) plus tolerance |
| Overcharge release | ~4.08–4.10 V | |
| Overdischarge | 2.5–2.9 V/cell | Pick based on cell spec |
| Discharge OC detect | ~150 mV across FETs | |
| Short circuit | ~1.35 V, ~µs delay | |

**MOSFET:** FS8205A (dual N, 20 V, ~25 mΩ each at Vgs 4.5 V, two in series ≈ 50 mΩ). With an 8.4 V pack, gate drive is ample.

- **Overcurrent trip:** 0.15 V / 0.05 Ω ≈ **3 A**.
- If you need more discharge current, use lower-RDS FETs (e.g., 2 × AO4406 back-to-back,