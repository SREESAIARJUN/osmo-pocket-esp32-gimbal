# 2S Li-ion Charger/Protection Design: IP2326 + Cell Balancing + USB-C Sink

I've read "Cycle 27" as a revision or iteration label. If it refers to a specific requirement, such as a cycle-life target or a test cycle, tell me and I'll adjust.

## 0. Important Constraint: What the IP2326 Actually Does

Before committing to a schematic, note these limits of the Injoinic IP2326:

- It is a **5 V-input synchronous boost charger** for 2S Li-ion (8.4 V full), rated at roughly 10–15 W input (5 V / 2–3 A).
- It **does not negotiate USB PD** and **does not accept 9/12 V input**.
- It has **no per-cell protection and no cell balancing**.

That gives you two architectures:

| Option | Input | PD needed? | Charger | Notes |
|---|---|---|---|---|
| **A (recommended for IP2326)** | 5 V @ 3 A via Type-C current advertisement | No. Use 5.1 kΩ CC pull-downs. | IP2326 | Simple and cheap. About 13–15 W max. |
| **B** | 9–20 V via PD | Yes, with a PD sink IC | Not IP2326. Use a buck or buck-boost charger such as IP2369, BQ25887 or BQ25713. | Faster charging, higher BOM cost |

Forcing PD in front of an IP2326 would only ever request 5 V, which a 5.1 kΩ Rd pull-down already does. Requesting 9 V would **destroy the IP2326**. The design below is therefore Option A, with an optional PD-sink section that is safe for 5 V-only use.

---

## 1. Block Diagram

```
USB-C ──► [CC: 5.1k Rd ×2] ──► VBUS ─► [Input TVS + OVP load switch] ─► IP2326 (boost) ─► BAT+ (8.4 V)
                                                                          │
                                       ┌──────────────────────────────────┤
                                       │                                  │
                               Cell 2 (top) ── HY2213 balancer #2         │
                                  BAT_MID ───────────────────────── 2S protection IC (HY2120 / S-8252)
                               Cell 1 (bot) ── HY2213 balancer #1         │
                                       │                                  │
                                    BAT− ── [Dual N-FET 8205A / AON7810] ── PACK− (system/charger GND)
```

---

## 2. USB-C Input Stage

| Ref | Part | Value / Note |
|---|---|---|
| J1 | USB-C receptacle (16-pin power+USB2 is sufficient) | |
| R1, R2 | CC1, CC2 to GND | **5.1 kΩ 1%**, one per CC pin. Never share a single resistor between both pins. |
| D1 | VBUS TVS | SMBJ5.0A or SMF5.0A. Clamps hot-plug ringing. |
| U1 (opt) | OVP load switch | e.g. AW32901 or SGM2549, ~6.0–6.8 V OVP trip. Protects the IP2326 from a bad 9 V+ source. |
| C1 | VBUS bulk | 10 µF X5R 16 V close to the connector, plus 22 µF at the IP2326 VIN |

**Source current detection (optional):** Read the CC voltage with an MCU ADC to sense the source's advertisement:
- about 0.41 V means Default USB
- about 0.92 V means 1.5 A
- about 1.68 V means 3.0 A

Use this to lower the IP2326 charge current when the source can't supply 3 A.

**Optional PD sink (Option A-safe):** You can add a CH224K or IP2721 strapped for **5 V request only**. This is useful only if you need PD signalling for compliance or for e-marker detection. Otherwise Rd resistors are electrically equivalent for 5 V.

---

## 3. IP2326 Charger Stage

Verify the pin names and recommended values against the current Injoinic datasheet revision.

| Item | Recommendation |
|---|---|
| Boost inductor | **2.2–3.3 µH**, Isat ≥ 5 A, DCR ≤ 15 mΩ (e.g. 0630/0650 molded) |
| VIN caps | 22 µF + 0.1 µF X5R/X7R, ≥ 10 V |
| VOUT/BAT caps | 2 × 22 µF, **≥ 16 V rated** (DC-bias derating at 8.4 V matters) |
| Charge LED | Per datasheet LED pin. Supports 1–4 LED charge indication. |
| NTC | 10 kΩ B=3435 NTC on the cell pack, wired to the NTC pin. Stops charging outside roughly 0–45 °C. |
| Charge profile | Trickle charge below about 6 V, then CC, then CV at 8.4 V, terminating at about 1/10 CC |

Expected performance, assuming about 90% efficiency at 5 V / 3 A in: roughly 15 W in, 13.5 W to the battery, about 1.6 A at 8.4 V.

---

## 4. 2S Protection (Independent of the Charger)

**U3: HY2120-CB** (or Ablic S-8252, or TI BQ2970-class for 2S equivalents)

| Function | Typical threshold |
|---|---|
| Over-charge, per cell | 4.25–4.30 V (release ~4.10 V) |
| Over-discharge, per cell | 2.80–3.00 V |
| Over-current | Set by FET Rds(on) via the CS/VM pin |
| Short circuit | Fast trip, about 250 µs |

**Wiring:**
- **VDD:** BAT+ through 330 Ω, with 0.1 µF to VSS.
- **VC (mid sense):** BAT_MID through 1 kΩ, with 0.1 µF to VSS.
- **VSS:** BAT−.
- **DO/CO:** drive the gates of the dual FET Q1 (common-drain 8205A for ≤ 4 A, or AON7810 / 2× low-Rds(on) for higher current).
- **VM:** PACK− through 2 kΩ.

**Over-current setpoint:** I_OC = V_OCD / (2 × Rds(on)). For example, 0.15 V / (2 × 20 mΩ) ≈ 3.75 A. Choose FETs to give about 1.5× your maximum load.

**Fuse:** Add a resettable PTC or a one-time fuse in BAT+ as a secondary protection layer.

---

## 5. Cell Balancing

Use passive bleed balancing, one IC per cell.

**U4, U5: HY2213-BB3A** (balance turn-on at about 4.20 V). Each IC drives its own bleed path.

| Ref | Part | Value | Purpose |
|---|---|---|---|
| R_bal | Bleed resistor | **47 Ω, 1 W** | About 89 mA at 4.2 V, 0.37 W per cell |
| Q_bal | Bleed switch | Small N-FET (AO3400) | Only if bleed exceeds the IC's internal drive. Otherwise use the IC's direct drive. |
| C | VDD decoupling | 0.1 µF per IC | |

**Wiring:**
- **Cell 1 (bottom):** U4 VDD to BAT_MID, VSS to BAT−.
- **Cell 2 (top):** U5 VDD to BAT+, VSS to BAT_MID.

**Why this works:** The IP2326 terminates at about 1/10 CC, around 160 mA. A balancer bleeding about 90 mA can therefore divert most of the charge current from the high cell near the end of charge. With matched cells, this corrects 50–100 mV imbalances over a few cycles.

**Thermal:** Keep the bleed resistors away from the cells and the NTC, and give them copper pour. For larger packs (> 3 Ah), consider 33 Ω (about 127 mA, 0.53 W, use a 2 W resistor).

---

## 6. Layout Guidelines

1. **Boost power loop:** Keep the IP2326 SW node, inductor and output caps tight, with minimal SW copper area. Place a ground plane under it.
2. **Kelvin sensing:** Run the protection IC VC/VDD sense traces directly to the cell tabs, not through high-current paths.
3. **High-current path:** BAT− → FETs → PACK− should be wide (≥ 3 mm at 1 oz for 4 A), or use 2 oz copper.
4. **Separation:** Keep balancer resistors and NTC thermally separated.
5. **Input protection:** Place the TVS within 5 mm of the USB-C VBUS pins.

---

## 7. Bring-Up and Validation Checklist

- [ ] **USB-C:** 5.1 kΩ on both CC pins is visible to a Type-C source tester, and the source supplies 5 V.
- [ ] **OVP:** Apply 9 V to VBUS; the load switch must block it and the IP2326 must stay below its absolute maximum rating.
- [ ] **Charge profile:** Measure trickle, CC and CV transitions at 8.40 V ±1%, then termination current.
- [ ] **NTC:** Charging stops outside the configured temperature window.
- [ ] **Over-charge:** Using a cell simulator