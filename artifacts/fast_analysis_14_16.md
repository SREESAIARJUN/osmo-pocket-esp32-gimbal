# 2S Li-ion Charger/Protection Design: IP2326 + Balancing + USB-C Sink

I don't know what "Cycle 14" refers to (a course iteration, revision number, or charge-cycle requirement). I've treated it as a design revision label. Tell me if it implies a specific constraint.

---

## 1. Constraints That Shape the Design

Three points about the IP2326 drive most of the architecture. Confirm each against the current Injoinic datasheet, since revisions differ.

| Assumption | Consequence |
|---|---|
| The IP2326 is a **5 V-input synchronous boost charger** for 2S (8.4 V) packs, typically ~1–1.5 A charge (~10 W class). | Its input is designed for 5 V. Negotiating 9/12/15/20 V via PD and feeding it to the IP2326 would exceed its input rating. |
| It is a **charger only**. | It does **not** provide cell protection (OV/UV/OC/short per cell). A separate 2S protector plus MOSFETs is required. |
| It does **not** balance cells. | External balancing is required. |

**USB-C implication:** A sink only needs **Rd = 5.1 kΩ on CC1 and CC2** to receive 5 V at up to 3 A (when the source advertises it). That covers the IP2326's full input power. A PD controller adds cost without benefit unless you use a charger that accepts higher input voltages.

**If you truly need PD (e.g. 9 V/2 A → faster charge), choose one of these instead:**
- **IP2368 / IP2366**: Injoinic, PD-input buck-boost, 2–6S.
- **TI BQ25887**: 5 V-input 2S boost charger with **integrated cell balancing**. Pair it with HUSB238 for 5 V/3 A detection.
- **BQ25713 + TPS25750 (or HUSB238/CH224K)**: full PD sink at 9–20 V with a buck-boost charger.

Below are **Option A** (IP2326, 5 V Type-C, recommended for your stated part) and an outline of **Option B** (PD upgrade).

---

## 2. Block Diagram (Option A)

```
USB-C ──► VBUS ─► TVS + PTC/eFuse ─► IP2326 (boost, CC/CV 8.4V) ─► PACK+
   │                                                               │
   CC1/CC2: 5.1kΩ to GND (Rd)                                      │
                                                       ┌───────────┤
                                     Cell 2 (top) ─────┤  Balancer │ HY2213 ×2
                                     Mid tap  ─────────┤  (shunt)  │ (one per cell)
                                     Cell 1 (bottom) ──┤           │
                                                       └───────────┤
                                     2S protector (HY2120 / S-8252)│
                                     ─► dual N-MOSFET (low side) ─► PACK−
```

---

## 3. Subsystems

### 3.1 USB-C Input (Sink)
- **CC1, CC2**: 5.1 kΩ ±1% each to GND. This is mandatory, or C-to-C cables will supply no VBUS.
- **Optional current detection**: read the CC voltage with an MCU/ADC, or use a HUSB238 in 5 V mode. This tells you whether the source offers Default USB, 1.5 A, or 3 A, so you can derate the charge current.
- **VBUS protection**:
  - SMBJ5.0A (or SMF5.0A) TVS to GND.
  - Either a 2 A PTC or, better, an eFuse/OVP switch (e.g. TPS2595x, or an AW32901-style OVP at ~6 V). This guards against a faulty source delivering higher voltage.
- **Input capacitance**: 10 µF ceramic plus 22 µF near the IP2326 input. Total VBUS capacitance must be ≤10 µF at the connector (USB spec), so place the bulk capacitance after the load switch.
- **ESD**: use a low-capacitance ESD array on the CC lines (and D+/D− if used).

### 3.2 IP2326 Charger
Use the datasheet reference schematic for exact pins. Key design values:

- **Inductor**: per datasheet, typically 2.2–4.7 µH. Isat ≥ 4 A, low DCR (<30 mΩ), shielded.
  - Input current at 1 A charge to 8.4 V, 88% efficiency: 8.4 × 1 / (5 × 0.88) ≈ **1.9 A**.
  - Peak inductor current ≈ 2.5 A, so Isat ≥ 4 A gives margin.
- **Output caps**: 2 × 22 µF / 16 V X5R ceramic.
- **Charge current setting**: via the set resistor or pin per datasheet.
  - Choose ≤0.5C for the cells. For 2 × 2500 mAh, ≤1.25 A; 1 A is a sensible default.
  - Stay ≤ about 1.9 A input if the source only guarantees 2 A, or rely on the IP2326's input-voltage-loop (VBUS droop) regulation if available.
- **NTC**: 10 kΩ B3435 thermistor bonded to the cells, wired to the IP2326 NTC pin. Charging should be inhibited below 0 °C and above 45 °C.
- **Status LEDs**: per datasheet LED pins (charging / full).
- **Termination voltage**: 8.4 V (4.20 V/cell). Some variants offer 8.3 V; lower voltage gives better cycle life.

### 3.3 2S Protection (Primary Safety)
**IC**: HY2120-CB (or Ablic S-8252, or a 2S protector with similar thresholds).

Typical thresholds for a CB-type variant (verify the suffix):

| Parameter | Value |
|---|---|
| Overcharge (OV) | 4.28 V / cell |
| Overdischarge (UV) | ~2.90 V / cell |
| Overcurrent | set by MOSFET Rds(on) sensing |

**MOSFETs**: dual common-drain N-channel, low side, e.g. FS8205A (20 V, ~25 mΩ), or a lower-Rds part for higher current.
- Overcurrent trip: I_oc ≈ V_oc / (2 × Rds(on)).
- Example: 0.15 V / (2 × 25 mΩ) ≈ **3 A**. Choose the MOSFET to set your discharge limit.

**Supporting parts**:
- Cell sense inputs: 330 Ω–1 kΩ series resistors plus 0.1 µF caps per sense input (RC filter).
- VM pin: ~2 kΩ resistor to PACK−.

**Secondary protection (recommended)**: a series fuse in PACK+ rated ~1.5× maximum discharge current.

### 3.4 Cell Balancing
**Passive shunt balancing, one HY2213-BB3A (4.20 V threshold) per cell.**

- Each HY2213 drives a small NMOS (e.g. AO3400) that switches a bleed resistor across its cell when that cell exceeds ~4.20 V.
- **Bleed resistor**: 4.2 V / 68 Ω ≈ 62 mA, dissipating ~0.26 W. Use 1206 or 2512 parts, or 2 × 136 Ω in parallel.
- **Limitation**: balance current (~60 mA) is far below the charge current (~1 A). So the balancer only trims cells near full charge and cannot correct large mismatches. Start with matched cells (ΔV < 20 mV, matched capacity and IR).
- **Charge holdoff (optional but valuable)**: OR the two balancer outputs into the IP2326 enable pin (if present) or into a gate on the charge path. This pauses or tapers charging while balancing is active, so the high cell doesn't hit the protector's 4.28 V OV threshold.

**Alternatives**:
- BQ29209 (2S OVP with balancing).
- Switching to BQ25887, which integrates balancing (Option B).

### 3.5 Threshold Coordination (Critical)

```
Charger CV (per cell, ideal):  4.20 V
Balancer turn-on:              4.20 V  (HY2213-BB3A)
Protector OV trip:             4.28 V  (HY2120-CB)
Absolute cell max:             4.25–4.30 V (check cell datasheet)
```

Ensure the charger's 8.4 V divided across two imbalanced cells cannot push one cell above 4.28 V before the balancer acts. With a 1 A charge and 62 mA balance current, that requires starting with reasonably matched cells. If you can't guarantee that, lower the CV to 8.3 V.

---

## 4. Bill of Materials (Key Items)

| Ref | Part | Note |
|---|---|---|
| J1 | USB-C 16-pin receptacle | power + CC |
| R1, R2 | 5.1 kΩ 1% | CC1/CC2 Rd |
| D1 | SMBJ5.0A | VBUS TVS |
| U1 | OVP/eFuse (~6 V OVP) | input guard |
| U2 | IP2326 | boost charger |
| L1 | 2.2–4.7 µH, Isat ≥ 4 A | per datasheet |
| U3 | HY2120-CB | 2S protector |
| Q1 | FS8205A (or lower Rds) | protection FETs |
| U4, U5 | HY2213-BB3A | balancers |
| Q2, Q3 | AO3400 | bleed switches |
| R_