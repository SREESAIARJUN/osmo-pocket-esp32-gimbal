# 2S Li-ion Charger, Protection and Balancing Design with IP2326 and USB-C Input

## 0. Corrections to the Brief

| Assumption in the brief | Reality | Design consequence |
|---|---|---|
| IP2326 is a protection IC | IP2326 (Injoinic) is a **5 V-input synchronous boost charger** for 2S Li-ion (about 10 W class). It does **not** provide the independent OV/UV/OC/short-circuit cut-off a protection IC gives. | Add a dedicated 2S protector (HY2120 class) with back-to-back MOSFETs. |
| PD negotiation is needed | IP2326 accepts **5 V only**. Requesting 9/12/15/20 V would exceed its input rating. | No PD controller is needed. Two 5.1 kΩ Rd resistors on CC1/CC2 give 5 V at up to 3 A from any compliant source. |
| IP2326 handles balancing | Check your datasheet revision and part suffix. Even where balancing is listed, it is low-current. | Add an external per-cell shunt balancer (HY2213) for a robust, verifiable design. |

**Before ordering parts**, confirm the following from the current IP2326 datasheet, since Injoinic revises these parts:
- pin numbers;
- maximum input voltage;
- charge-current programming method;
- whether your suffix includes balancing.

## 1. Block Diagram

```
USB-C ──► VBUS ─► Fuse/TVS ─► IP2326 (5V→8.4V boost CC/CV charger) ─► PACK+
          CC1/CC2 ─ 5.1k Rd to GND each                              │
                                                                     ├─ Cell 2 (top)  ── HY2213 shunt balancer
                                                                     ├─ Mid-tap (BM)
                                                                     ├─ Cell 1 (bottom) ── HY2213 shunt balancer
                                                                     │
PACK– ◄── Dual N-MOSFET (DO/CO, common-drain) ◄── HY2120 2S protector ◄── B–
```

## 2. USB-C Sink Front End (No PD)

- **CC termination:** CC1 and CC2 each go through a 5.1 kΩ (1%) resistor to GND.
  - This advertises a sink.
  - The source supplies vSafe5V.
- **Current advertisement (optional):** Read CC voltage with an ADC or comparator to detect the source's current capability.
  - Default USB is under 0.66 V.
  - 1.5 A is 0.66–1.23 V.
  - 3.0 A is above 1.23 V.
  - Scale the IP2326 input-current limit accordingly. A 500 mA legacy port cannot run a 10 W charge.
- **VBUS protection:**
  - SMAJ5.0A (or SMF5.0A) TVS.
  - 3 A PTC or a 4 A fast fuse.
  - 10 µF + 100 nF ceramic at the connector.
- **Optional PD:** Only consider PD if you replace IP2326 with a charger that accepts higher input voltage. In that case use a CH224K or IP2721 sink controller and an input-voltage limit to match the new charger. With IP2326, PD is unnecessary.

## 3. IP2326 Charger Section

| Parameter | Setting |
|---|---|
| Input | 5 V; input current limit ≤ the advertised Type-C current |
| Charge voltage | 8.4 V (4.20 V/cell). Use 8.2–8.3 V if you want longer cycle life. |
| Charge current | Program per datasheet, e.g. 1.0–1.5 A (≤ 0.5C for a 2–3 Ah pack) |
| Inductor | Per datasheet, typically 2.2–4.7 µH; Isat ≥ 4 A; low DCR (< 30 mΩ) |
| Output capacitors | ≥ 2 × 22 µF / 16 V X5R/X7R ceramic, close to the switch node and return |
| NTC | 10 kΩ B=3435, thermally bonded to the cells. Charge window 0–45 °C. |
| Status | LED or MCU GPIO for charge/full/fault |

**Layout:**
- Keep the boost hot loop (switch node, inductor, output capacitors, GND) tight.
- Use a solid ground plane.
- Keep the switch node away from the cell-sense traces.

## 4. 2S Protection (HY2120-class, e.g. HY2120-CB)

**Connections:**
- VC1 connects to the cell-1 positive (mid-tap) through 1 kΩ, with 100 nF to VSS.
- VDD connects to the top cell through 330 Ω, with 100 nF to VSS.
- VSS connects to B–.
- The VM/CS pin connects through 2 kΩ to PACK–.

**MOSFETs:** Use a dual N-channel common-drain device, e.g. 8205A / FS8205 (20 V, about 25 mΩ).
- DO drives the discharge FET gate.
- CO drives the charge FET gate.
- Use two in parallel if the continuous current is above 3 A.

**Typical thresholds (choose the variant to match):**

| Function | Threshold |
|---|---|
| Overcharge | 4.28–4.30 V/cell, release about 4.10 V |
| Over-discharge | 2.7–3.0 V/cell |
| Overcurrent | set by Rds(on) × 2 FETs, e.g. about 6 A |
| Short circuit | fast trip in roughly hundreds of µs |

**Coordination rule:** charger CV (4.20 V) < balancer start (about 4.20 V) < protector OVP (4.30 V). This ordering ensures the protector never trips during normal charging.

## 5. Cell Balancing (Passive, per Cell)

- **Device:** one HY2213-BB3A (or similar) across each cell.
  - The threshold is typically about 4.20 V; choose the suffix to match the charge voltage.
  - Each device drives a small N-MOSFET (e.g. AO3400) with a bleed resistor.
- **Bleed resistor:** 47 Ω gives about 89 mA at 4.2 V and dissipates about 0.38 W.
  - Use 2 × 100 Ω / 0.25 W 1206 resistors in parallel.
  - Space them away from the cells and NTC.
- **Current ratio:** Balancing current is only about 6–9% of the charge current. This is suitable for maintaining matched cells, not for recovering badly mismatched ones. Start with cells matched within 20 mV.
- **Taper:** Lower the IP2326 termination current, or use an MCU-timed top-off, so the balancers get time to act near CV.

## 6. Key BOM

| Ref | Part | Note |
|---|---|---|
| U1 | IP2326 | 2S boost charger |
| U2 | HY2120-CB (or equivalent) | 2S protector |
| U3, U4 | HY2213-BB3A | Per-cell balancers |
| Q1 | FS8205A dual NMOS | Charge/discharge switch |
| Q2, Q3 | AO3400 | Balance bleed switches |
| R_CC × 2 | 5.1 kΩ 1% | USB-C sink termination |
| D1 | SMAJ5.0A | VBUS TVS |
| F1 | 3 A PTC/fuse | Input protection |
| L1 | 2.2–4.7 µH, Isat ≥ 4 A | Boost inductor |
| NTC | 10 kΩ B3435 | Charge temperature window |
| F2 (optional) | Pack fuse, about 1.5 × max load | Secondary protection |

## 7. Validation Checklist

1. **Input:** Plug in sources rated 500 mA, 1.5 A and 3 A. Verify the input current never exceeds the advertisement.
2. **CV accuracy:** Pack voltage should be 8.40 V ±0.5%, and each cell 4.20 V ±25 mV at termination.
3. **Protection thresholds:** Use a bench supply on each cell to verify OVP, UVP, OCP and short-circuit trips and releases.
4. **Balancing:** Offset the cells by 50 mV and confirm convergence within a few charge cycles. Thermally image the bleed resistors (keep them below 80 °C).
5. **NTC:** Verify that charging is inhibited below 0 °C and above 45 °C.
6. **Ripple and EMI:** Check the switch node and VBUS ripple.

If "Cycle 30" refers to a specific revision or test-cycle requirement, such as 30 charge cycles for qualification, share the spec and I'll add the test procedure for it.