# 2S Li-ion Charger/Protection Design: IP2326 + Balancing + USB-C PD Sink

## 0. Architecture Constraints

These facts shape the design. **Verify each against the current Injoinic datasheet revision before layout.**

| Constraint | Implication |
|---|---|
| The IP2326 is a **5 V-input synchronous boost charger** for 2S (8.4 V), up to about 15 W. | It cannot use 9/12/15/20 V PD voltages. A PD sink only helps here by securing a clean 5 V / 3 A contract. |
| The IP2326 is a **charger**, not a full BMS. | You still need an independent 2S protector (OV/UV/OC/short) with back-to-back FETs. |
| Balancing support on IP2326 is limited or variant-dependent. | Add a dedicated balancing stage so the design doesn't depend on it. |

If you actually want **>15 W PD charging (9–20 V)**, swap the IP2326 for a buck/buck-boost 2S charger: IP2368, BQ25713, or BQ25887 (BQ25887 has integrated balancing). The rest of this design (protection, balancing, PD sink) carries over.

---

## 1. Block Diagram

```
USB-C ──► PD Sink Ctrl ──► VBUS 5V/3A ──► IP2326 Boost Charger ──► PACK+ (8.4 V)
 (CC1/CC2)  (CH224K/IP2721)    │                │
                               TVS + Bulk       └─ NTC (cell temp)
                                                    │
               ┌────────────────────────────────────┘
               ▼
   2S Cells ── Cell Balancer (BM, midpoint) ── 2S Protector IC ── Dual N-FET (low side) ── PACK–
```

---

## 2. USB-C PD Sink

**IC:** WCH **CH224K** (or Injoinic IP2721). Both are standalone, with no MCU needed.

- Configure the request for **5 V**. On CH224K, CFG pins select the voltage; the 5 V setting also advertises the Type-C current. Confirm the strap table in the datasheet.
- CC1/CC2 go straight to the connector. The IC presents Rd internally, so don't add external 5.1 kΩ resistors when using CH224K's PD mode.
- **Simpler alternative:** for 5 V only, two **5.1 kΩ Rd resistors** (CC1, CC2 to GND) plus Type-C current advertisement (3 A from a compliant source) is sufficient. PD is then optional, adding value only for PD-only chargers or explicit contract confirmation.
- **Input protection:**
  - TVS: SMBJ5.0A (or SMF5.0A) on VBUS.
  - Bulk capacitor: 10 µF + 100 nF at the connector. Keep the total below about 10 µF before the switch if strict USB inrush compliance matters.
  - Optional: PMOS reverse/overvoltage switch (e.g., AO3401 + 6.2 V Zener gate clamp), or an OVP IC (SGM2551 or similar). Protects against a misbehaving source delivering 9 V or more.

---

## 3. IP2326 Charger Stage (typical values; follow the datasheet reference design)

| Item | Value | Notes |
|---|---|---|
| Inductor | 2.2–3.3 µH, Isat ≥ 5 A, DCR < 15 mΩ | Shielded, e.g., 1265-size |
| VIN caps | 2×22 µF X5R 10 V + 100 nF | Close to VIN/GND |
| VOUT/BAT caps | 2×22 µF X5R **25 V** | DC-bias derating at 8.4 V |
| Charge current set | Per datasheet RSET/ISET table | Start at about 1.5 A charge (≈13 W from 5 V at ~90 % efficiency) |
| Termination voltage | 8.4 V (4.2 V/cell) | Some variants allow 8.7 V for 4.35 V cells; strap accordingly |
| NTC | 10 kΩ B=3435 at the cell, per datasheet divider | JEITA-style cutoff, typically 0–45 °C |
| LED pins | Status LEDs via 1 kΩ | Charging / full / fault |
| Input current limit | ≤ 3 A | Matches the 5 V/3 A Type-C contract; enable input-voltage DPM if available so a weak source doesn't collapse |

---

## 4. 2S Protection (independent of the charger)

**IC:** HYCON **HY2120** / Ablic **S-8252** / Fortune **FS8205A FETs + DW01-equivalent 2S part**.

| Parameter | Typical setting |
|---|---|
| Overcharge detect | 4.28–4.30 V/cell (must sit above the charger's 4.20 V) |
| Overdischarge detect | 2.8–3.0 V/cell |
| Discharge overcurrent | Set by FET Rds(on) × threshold, e.g., ~10 A |
| Short circuit | ~25–40 A, <500 µs |

- **FETs:** dual N-channel common-drain on the low side, e.g., **FS8205A** (≤5 A continuous) or 2× AON6240-class for higher current. Rds(on) sets the OC trip point, so pick the FETs and threshold together.
- **Filtering:** VDD RC of 330 Ω + 0.1 µF, and the cell-sense RC per the datasheet (typ. 1 kΩ + 0.1 µF).
- **Secondary fuse:** a fuse (e.g., 10 A slow-blow) or a PTC in series with PACK+. This is the last line of defense.

---

## 5. Cell Balancing

**Option A, passive bleed (recommended, simple):**
- **IC:** a 2S balance-capable protector (e.g., **HY2213** per cell, or a protector with a CB pin) bleeding each cell above about 4.18–4.20 V.
- **Bleed resistor per cell:** 47–68 Ω, giving about 60–90 mA at 4.2 V. Use 0805/1206 rated ≥ 0.5 W, or 2 in parallel.
- **Heat:** 4.2 V² / 56 Ω ≈ 0.31 W per cell. Provide copper pour and keep it away from the NTC.
- Balancing only acts near full charge (top balancing). This is fine for matched cells from the same batch.

**Option B, active/charger-integrated:** if using BQ25887 (see section 0), its internal balancing replaces this stage.

**Midpoint sense (BM) wiring:** run a dedicated Kelvin trace from the cell junction to the protector and balancer. Never share it with the power path.

---

## 6. Layout Rules

1. **Boost loop:** keep the IP2326 SW → inductor → output cap → GND loop minimal. Use a solid GND plane with thermal vias under the exposed pad.
2. **Current routing:** route high current (VBUS, BAT+, PACK–/FETs) wide, at ≥ 2 mm per amp on 1 oz copper, or use pours.
3. **Separation:** keep the protector's VSS/sense ground star-connected at the cell negative, separate from the switching ground.
4. **NTC placement:** thermally couple the NTC to the cells, not to the board or bleed resistors.
5. **Connector protection:** place the TVS and fuse at the connector edge.

---

## 7. Bring-Up and Validation Checklist

| Test | Pass criteria |
|---|---|
| PD contract (PD analyzer or USB meter) | 5 V negotiated; no renegotiation loops |
| Charge CV accuracy | 8.40 V ±0.5 % |
| CC current / input current | Within setting; VBUS stays above ~4.6 V at full load |
| Termination | Stops at the set taper current; no restart oscillation |
| Balancing | With a 50 mV imbalance injected, the delta drops below 20 mV after a full cycle |
| OV / UV / OC / short trip | Within IC tolerance (use an electronic load and cell simulator) |
| NTC | Charging stops below 0 °C and above 45 °C |
| Thermal | IP2326 and bleed resistors < 85 °C at 25 °C ambient, full power |
| Hot-plug | No VBUS overshoot above the TVS clamp; no IC resets |

---

"Cycle 13" doesn't change the electrical design. If it refers to a specific revision or requirement set (current target, cell chemistry, form factor), share it and I'll tailor the values, especially charge current, OC thresholds, and bleed current.