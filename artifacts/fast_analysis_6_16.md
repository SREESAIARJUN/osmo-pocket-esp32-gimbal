# 2S Li-ion Charger/Protection Design: IP2326 + Balancing + USB-C Sink

I don't have context for "Cycle 6". If it refers to a previous design iteration, a requirements set, or a course assignment, share those constraints and I'll adapt this. The design below is self-contained.

---

## 1. Architecture Problem to Resolve First

**The IP2326 is a 5 V-input boost charger.** It takes about 5 V from USB and boosts it to the 8.4 V needed for a 2S pack. This has three consequences:

| Requirement | Does IP2326 handle it? | Implication |
|---|---|---|
| Charging 2S from 5 V | Yes (boost, roughly 10–15 W class) | Core function |
| USB-C PD negotiation (9/12/15/20 V) | **No.** It is a 5 V-only input, and >~6 V on VIN risks damage | A PD trigger requesting 9 V+ must **not** feed IP2326 directly |
| Cell balancing | **No** | Needs external balancer |
| Pack protection (OV/UV/OC/SC) | Charge termination only, not a protector | Needs a separate 2S protection IC + MOSFETs |

So there are two valid paths:

- **Option A (recommended if you must use IP2326):** a 5 V-only USB-C sink using Type-C current advertisement (up to 5 V/3 A = 15 W). No PD contract is needed, and this matches the IP2326's power class.
- **Option B (if PD at 9 V+ is a hard requirement):** replace IP2326 with a buck or buck-boost 2S charger (e.g., Injoinic IP2368, TI BQ25887/BQ25713 family, or a PD trigger such as CH224K/IP2721 feeding a buck 2S charger).

Option A is detailed below, with the Option B changes in Section 7.

---

## 2. Block Diagram (Option A)

```
USB-C Receptacle
  VBUS ──► TVS (SMBJ6.0A / ESD) ──► Fuse/PTC (3A) ──► Input filter ──► IP2326 VIN
  CC1 ──5.1kΩ──┐
  CC2 ──5.1kΩ──┴─ GND    (Rd pull-downs = sink; read CC voltage to detect 1.5A/3A source)
  D+/D- ─────────► IP2326 (if fast-charge/BC1.2 detection is used per datasheet)

IP2326 (boost) ──► L (≈2.2–4.7µH, Isat ≥ 5A) ──► BAT+ (8.4V CV)
                                                 │
            ┌────────────────────────────────────┤
            │                                    │
       Cell 2 (top)  ── HY2213 balancer #2 ── R_bal + LED/FET
            │ mid-tap (VC1)
       Cell 1 (bottom) ─ HY2213 balancer #1 ── R_bal
            │
          BAT− ──► 2S protector (HY2120 / S-8252) ──► Dual N-FET (8205A or better) ──► PACK−
```

---

## 3. Input Stage: USB-C Sink

| Item | Value | Notes |
|---|---|---|
| CC1, CC2 pull-down (Rd) | 5.1 kΩ ±1% each to GND | **Separate resistors per CC pin.** Never tie CC1 and CC2 together. This makes the device a compliant sink. |
| Source capability detect | Optional: ADC/comparator on CC | vRd ≈ 0.25–0.61 V means Default; 0.70–1.16 V means 1.5 A; >1.31 V means 3.0 A |
| TVS on VBUS | SMBJ6.0A or USB-rated ESD diode | Clamps hot-plug ringing |
| Input cap | 22 µF ceramic + 100 µF low-ESR | Place at the IP2326 VIN pin |
| Overcurrent | 3 A PTC or 4 A fuse | |
| Input current limit | Set per IP2326 datasheet | Match the detected source current (default 500 mA/900 mA, 1.5 A, or 3 A) so weak chargers aren't collapsed. Some IP232x variants auto-reduce current on VBUS droop; confirm for your part. |

Check whether your IP2326 variant integrates CC handling. If it does, omit the discrete Rd resistors to avoid double termination.

---

## 4. Charger Stage: IP2326

**Targets:** CV = 8.40 V (4.20 V/cell), CC around 1.0–1.5 A, termination around C/10.

**Power check at 15 W input, 88% efficiency:**
Pout ≈ 13.2 W, so Ichg ≈ 13.2 / 7.4 V ≈ **1.78 A max** at nominal pack voltage. Set to 1.5 A for thermal margin.

**Key components** (confirm exact values against the Injoinic datasheet/reference design):

- **Inductor:** 2.2–4.7 µH, Isat ≥ 1.3× peak input current (≥ 5 A), low DCR (<15 mΩ).
- **Output caps:** 2–3 × 22 µF/16 V X5R ceramic.
- **NTC:** 10 kΩ B=3435 thermistor on the pack. Stop charging below 0 °C and above 45 °C.
- **Status LEDs:** per the datasheet pins.

---

## 5. Cell Balancing

A passive shunt balancer per cell is simplest and works well with a CC/CV charger.

**Recommended:** HY2213-BB3A (or equivalent) per cell. It triggers at about 4.20 V and drives an external bleed path.

| Parameter | Value |
|---|---|
| Balance start | 4.20 V ±25 mV |
| Bleed resistor | 68 Ω, 0.5 W (two 1206 parts in parallel/series for heat) |
| Bleed current | 4.2 / 68 ≈ **62 mA** |
| Dissipation | ≈ 0.26 W per cell |

**Design rule:** bleed current should be about 3–5% of the termination current. With termination near 150 mA, 62 mA lets the high cell hold while the low cell catches up.

**Alternative:** TI BQ29209, a 2S overvoltage protector with integrated auto-balance. Use it if you want protection and balancing in fewer parts. Its balance current is small, so it is best for well-matched cells.

**Wiring:**
- Balancer 1: VDD = mid-tap (VC1), VSS = BAT−
- Balancer 2: VDD = BAT+, VSS = mid-tap
- Add 100 Ω + 100 nF RC filters on each sense pin.

---

## 6. Pack Protection: 2S Protector + MOSFETs

**IC:** HY2120-CB (or Ablic S-8252, or similar). Choose thresholds:

| Protection | Threshold | Release |
|---|---|---|
| Overcharge per cell | 4.28–4.30 V | 4.10 V |
| Overdischarge per cell | 2.70–2.90 V | 3.0 V |
| Discharge overcurrent | Set by FET Rds(on) | |
| Short circuit | ~1 V across FETs | Delay ~250 µs |

Thresholds must sit **above** the charger CV (4.20 V) and balancer (4.20 V). This gives three layers: charger, then balancer, then protector.

**MOSFETs:** a dual N-FET, common-drain, in the low side.
- 8205A (≈ 25 mΩ per FET) is OK up to about 3 A.
- For higher discharge current, use a part like AON6994/CSD16323 class, with Rds(on) of a few mΩ.

**Overcurrent setting:** I_trip = V_OCD / (2 × Rds(on)).
Example: 0.15 V / (2 × 12 mΩ) ≈ **6.25 A**. Size the FETs and traces for your load.

**Supporting parts:**
- VDD RC: 330 Ω + 100 nF.
- VM pin resistor: 1–2 kΩ.
- Cell-sense resistors: 1 kΩ, plus 100 nF to the respective reference.

---

## 7. Option B: True PD Sink (9 V or higher)

If PD negotiation is mandatory:

1. **PD trigger:** CH224K (resistor-selectable 9/12/15/20 V) or IP2721. Request **12 V** for a 2S pack, which provides headroom above 8.4 V.
2. **Charger:** a buck 2S charger with input ≥ 12 V, such as BQ25887 (which also has **integrated cell balancing**) or IP2368 (bidirectional PD with integrated PD sink/source).
3. **VBUS power path:** add a load switch/PMOS after the trigger reports PD-good, so no voltage reaches the charger until the contract is established.
4. Keep the HY2120 protection stage unchanged.

**Example:** at 12 V × 2 A = 24 W, charge current reaches about 2.5–3 A.

---

## 8. Layout Guidelines

- Keep the boost switching loop (VIN cap, IP2326 S