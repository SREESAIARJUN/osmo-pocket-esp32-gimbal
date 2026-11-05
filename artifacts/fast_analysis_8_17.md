# 2S Li-ion Charger + Protection + Balancing with IP2326 and USB-C (Design Rev "Cycle 8")

## 0. Architecture issue to resolve first

**The IP2326 is a 5 V-input boost charger.** It boosts 5 V up to the 8.4 V needed for 2S. It does not negotiate USB PD and cannot use 9/12/15 V PD contracts. Two consequences follow:

1. **PD negotiation adds nothing for IP2326.** A USB-C sink only needs 5.1 kΩ Rd pull-downs on CC1/CC2 to receive 5 V at up to 3 A (15 W) from a Type-C source. That matches the IP2326's maximum charge power class (≈10–15 W).
2. **If you really need PD (e.g., 9 V/12 V for faster charging), the IP2326 is the wrong chip.** Options:
   - **Option B1:** A 2S buck-boost charger with integrated PD sink, such as Injoinic **IP2366/IP2368** (also offer PD source, i.e., power-bank use).
   - **Option B2:** A standalone PD sink trigger (**CH224K**, **HUSB238**, **IP2721**) requesting 9 V or 12 V, feeding a 2S **buck** charger (e.g., TI BQ25887, which also does balancing, or BQ24133).

Below, **Option A (IP2326, Type-C 5 V/3 A)** is designed in full. Option B is sketched in §6.

Also, the IP2326 is a **charger, not a protector**. Over-discharge, over-current, and short-circuit protection on the battery side require a dedicated 2S protection IC and MOSFETs.

**Verify against the current Injoinic datasheet:** the exact pinout, balancing capability and thresholds, the charge-current setting method, and the NTC pin behavior. Pin names below are functional, not guaranteed pin numbers.

---

## 1. Block diagram (Option A)

```
USB-C ──► VBUS TVS/fuse ──► IP2326 boost charger ──► BAT+ (8.4 V)
 CC1/CC2: 5.1k Rd           │ L, Cin, Cout           │
                            │ NTC, LED/status        ├── Cell balancing (IP2326 internal* or HY2213 ×2)
                            └── BAT mid-tap ◄────────┤
                                                     │
                     2S pack: B+ ─ Cell2 ─ BM ─ Cell1 ─ B−
                                                     │
                     HY2120 2S protector ── 2× N-MOSFET (back-to-back, low side) ── P−
```

\*If your IP2326 variant/datasheet confirms charge balancing, the mid-tap goes to it. Otherwise use the external HY2213 balancers in §4.

---

## 2. USB-C input stage

| Ref | Part | Purpose |
|---|---|---|
| J1 | USB-C 16-pin receptacle (power + CC) | Input |
| R1, R2 | 5.1 kΩ 1 % (CC1→GND, CC2→GND) | Advertise sink (Rd), get 5 V up to 3 A |
| F1 | 4 A PTC or 5 A fast fuse | Input over-current |
| D1 | SMBJ5.0A / SMF5.0A TVS on VBUS | Hot-plug and ESD surge clamp |
| D2, D3 | Low-cap ESD diodes on CC1/CC2 (e.g., ESD5Z5.0) | CC ESD |
| C1 | 10 µF + 22 µF X5R/X7R, 16 V | Input bulk at IP2326 VIN |

**Input notes:**
- Do **not** place more than about 10 µF before any load switch if you need strict Type-C inrush compliance. In practice the IP2326's soft-start handles this.
- If you want to detect the source's current capability (Default / 1.5 A / 3 A), read the CC voltage with an MCU ADC:
  - ~0.41 V means default
  - ~0.92 V means 1.5 A
  - ~1.68 V means 3 A
- You can then lower the charge current for weak sources. The IP2326 also performs input-voltage-based current backoff (VIN drop detection).

---

## 3. IP2326 charger stage

| Item | Typical value | Notes |
|---|---|---|
| Boost inductor | 2.2–4.7 µH, Isat ≥ 6 A, DCR < 20 mΩ | Follow the datasheet recommendation; shielded |
| VOUT/BAT capacitance | 2 × 22 µF, 25 V X7R | Rated well above 8.4 V |
| Charge current set | Per datasheet resistor/config pin | Target ≤ 1C of the cells. Example: 2 × 2500 mAh at 1.2–1.5 A charge (≈10–12 W) |
| Termination voltage | 8.4 V (4.2 V/cell) | Some variants offer 8.6/8.7 V; check the cell chemistry |
| NTC | 10 kΩ B3950 at the pack | Charge inhibit outside ~0–45 °C |
| Status | LEDs per datasheet | Charging, full, fault |

**Layout for the boost loop:** keep the switch-node area minimal, and put the output capacitors directly from VOUT to the PGND pad. Use a solid ground plane and multiple vias under the exposed pad for thermal relief. At 15 W and ~90 % efficiency, about 1.5 W is dissipated.

---

## 4. Cell balancing

### 4a. If using IP2326 internal balancing
- Route the pack mid-tap (BM) to the IP2326 mid-cell sense pin through a 100–470 Ω series resistor, with 0.1 µF to GND, for ESD and filtering.
- Internal balancing current is small, typically tens of mA. That is fine for matched cells.

### 4b. External balancing (recommended for robustness or if internal balancing is absent)
Use one **HY2213-BB3A** (or equivalent) per cell:
- Bleed starts at ~4.20 V and stops at ~4.15 V (check the suffix).
- Each IC drives a small P- or N-MOSFET in series with a bleed resistor across its own cell.
- **Bleed resistor:** 4.2 V / 68 Ω ≈ 62 mA, giving 0.26 W. Use a 1206 or 2512 resistor rated ≥ 0.5 W.
- **Cell 1:** IC VDD = BM, VSS = B−.
- **Cell 2:** IC VDD = B+, VSS = BM.

This is top-of-charge balancing only. It is adequate for consumer packs with grade-matched cells.

---

## 5. 2S battery protection (mandatory)

**IC:** HY2120 (2S protector; choose the suffix for your thresholds). A common example set:

| Parameter | Typical setting |
|---|---|
| Over-charge detect / release | 4.28 V / 4.08 V per cell |
| Over-discharge detect / release | 2.8–2.9 V / 3.0 V per cell |
| Discharge over-current | Set by Rds(on) of the FETs, sensed at the VM pin |
| Short-circuit | ~1.2 V at VM, about 250 µs |

**MOSFETs:** dual N-channel, back-to-back common-drain, on the low side between B− and P−.
- For ≤ 4 A: 8205A or FS8205 class (20 V, ~25 mΩ).
- For higher current: e.g., AON7804 / SiA914 class, Vds ≥ 20 V, Rds(on) ≤ 10 mΩ.
- **Over-current check:** with 2 × 10 mΩ = 20 mΩ and a 0.1 V OCP threshold, trip is about 5 A. Pick the FETs to set the trip point.

**Supporting parts:**
- VDD RC: 330 Ω + 0.1 µF from B+.
- VC (mid) RC: 1 kΩ + 0.1 µF from BM.
- VM resistor: 2 kΩ to P−.
- Optional **secondary protection:** a fuse on B+ (e.g., a 2nd-level protector such as S-8244 driving a chemical fuse for high-energy packs), or at minimum a cell-rated fuse.

**Connection rules:**
- The charger's BAT output goes to **P+/P−**, i.e., through the protector, not directly to the cells.
- The protector's over-charge threshold (4.28 V) must sit above the charger termination (4.2 V) so normal charging never trips it.

---

## 6. Option B: if PD sink negotiation is truly required

**Topology:**
```
USB-C ─► CH224K (CFG resistor selects 9 V or 12 V request; falls back to 5 V)
       ─► PG pin enables the load switch once the contract is reached
       ─► 2S buck charger (BQ25887: integrated cell balancing, 2 A, I²C)
       ─► HY2120 + FETs ─► pack
```

**Alternatively,** a single **IP2366** gives PD/QC sink up to ~20 V and 2S–6S buck-boost charging (plus optional source/OTG). It has fewer parts, but you still need the HY2120 protector and balancing.

**PD-specific design points:**
- **Voltage ratings:** VBUS