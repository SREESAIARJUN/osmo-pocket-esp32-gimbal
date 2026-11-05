# 2S Li-ion Pack: IP2326 Charger + Protection + Balancing + USB-C Sink

## 0. Two design constraints to resolve first

**1. The IP2326 is a 5 V-input boost charger.** It boosts USB 5 V up to the 8.4 V needed for 2S, at roughly 10–15 W. It does not accept 9, 12 or 15 V PD contracts. Two consequences:
- **Option A (recommended): stay at 5 V and use USB-C current advertisement.** A plain Type-C sink with 5.1 kΩ Rd resistors can draw 5 V/3 A when the source advertises the 3.0 A Rp level. That covers the IP2326's full power range with no PD protocol needed.
- **Option B: true PD negotiation (9/12 V).** Requesting a higher voltage would exceed the IP2326's input rating. Use a buck-boost 2S charger that accepts PD input instead, such as the Injoinic IP2368 / IP2369-class parts, or a PD trigger plus a different charger. Section 5 covers this.

I design Option A in full below and include an Option B hook.

**2. The IP2326 is only a charger.** It does not provide cell-level overvoltage/undervoltage cutoff, discharge overcurrent or short-circuit protection, or balancing. You need:
- a 2S protection IC with back-to-back MOSFETs, and
- per-cell balancing.

"Cycle 18" isn't a reference I can tie to a specific spec. If it implies particular requirements (current, cell chemistry, form factor), share them and I'll adjust the values.

---

## 1. Block diagram

```
USB-C ──► CC1/CC2 (5.1k Rd each) 
  VBUS ─► TVS + input cap ─► IP2326 (boost charger, 8.4 V CV / CC)
                                   │ BAT+
                                   ▼
            ┌──────────── PACK+ ◄──┴──► LOAD
            │
   Cell 2 (top)  ── HY2213 balancer #2 (bleed R)
            │ mid-tap ─────────────► protection IC VC2/sense
   Cell 1 (bottom)── HY2213 balancer #1 (bleed R)
            │
         B− ── [Dual N-MOSFET, back-to-back] ── PACK− 
                    ▲ DO / CO gates
             2S protection IC (e.g., Seiko S-8252 / HY2212-class)
```

---

## 2. Charger stage (IP2326)

| Item | Value / Note |
|---|---|
| Input | VBUS 5 V. SMBJ5.0A (or 6–7 V-standoff) TVS to GND. 10 µF + 22 µF ceramic at VIN. |
| Boost inductor | 2.2–4.7 µH (per datasheet), Isat ≥ 5 A, low DCR (<20 mΩ), shielded |
| Output caps | 2–3 × 22 µF / 25 V X5R/X7R at BAT (de-rate for DC bias at 8.4 V) |
| Termination voltage | 8.4 V (4.20 V/cell). Some variants allow 8.6/8.7 V; only use those if the cells are rated for it. |
| Charge current | Set via the ISET resistor per the datasheet table. Start at ~1.0–1.5 A for typical 18650s (≤0.5 C). |
| NTC | 10 kΩ B3950 thermistor taped to the cells, to the IP2326 NTC pin |
| LEDs | Status LED pins per datasheet (charge / full / fault) |

**Input power budget:**
- Charge power is P = 8.4 V × 1.5 A ≈ 12.6 W.
- At about 90% efficiency, input power is ≈ 14 W, so input current at 5 V is ≈ 2.8 A.
- This requires a 3 A-capable source (Rp = 10 kΩ to 3.3 V, or 22 kΩ to 5 V, as seen on CC).
- If the source advertises only Default or 1.5 A, reduce ISET or rely on the IP2326's input-voltage-regulation (VBUS droop) loop, if your variant has one.

⚠ Verify pin names and ISET/NTC values against the current Injoinic datasheet. The variants (IP2326 / IP2326_15W, etc.) differ slightly.

---

## 3. USB-C sink interface (Option A)

- **CC1 → 5.1 kΩ (1%) → GND; CC2 → 5.1 kΩ (1%) → GND.** Use separate resistors; never tie CC1 and CC2 together.
- **Optional source-capability detection:** route CC to a small MCU ADC through ~100 kΩ to read the advertisement:
  - vRd ≈ 0.25–0.61 V: Default USB
  - 0.70–1.16 V: 1.5 A
  - more than 1.31 V: 3.0 A
- Use the reading to drive the IP2326 current-select or an enable pin.
- **ESD protection:** a TPD2E/ESD5Z-class diode on CC1/CC2, and a TVS on VBUS.
- **Connector:** a 16-pin USB 2.0 Type-C receptacle is sufficient. Connect D+/D− only if you need data or BC1.2.

---

## 4. Protection + balancing

### 4a. Protection IC

Use a 2S protector such as the Seiko S-8252 series or an HY2212-class part, with a dual N-MOSFET (for example two AO4406, or a single dual part like the AO8810 / FS8205A for lower currents).

| Parameter | Typical setting |
|---|---|
| Overcharge detect (per cell) | 4.275–4.30 V |
| Overcharge release | 4.075–4.10 V |
| Over-discharge detect | 2.5–2.8 V |
| Discharge overcurrent | Set by MOSFET R_DS(on) × threshold. Example: 2 × 10 mΩ, 0.1 V threshold → ≈ 5 A |
| Short circuit | ~0.5 V / R_DS(on) range, µs response |
| Cell sense filters | 330–1 kΩ + 0.1 µF on each VC input |

**MOSFET selection:**
- V_DS ≥ 20 V
- R_DS(on) at Vgs = 4.5 V low enough that I² × R stays under ~0.3 W at maximum load

### 4b. Balancing

Fit one HY2213-class balancer (around 4.20 V threshold) across each cell, with a bleed resistor.

- Bleed resistor example: 4.2 V / 62 Ω ≈ 68 mA per cell (0.28 W, so use a 0805/1206 pair or a 2512 part).
- Balancing current should be about 3–10% of the charge current to be effective during CV taper.
- The IP2326 terminates on total pack voltage. Without balancing, an imbalanced pack can push one cell past 4.2 V; the protector catches this as a last resort, but the balancer should prevent it.
- Place the bleed resistors away from the cells and the NTC. Otherwise their heat skews the NTC reading.

---

## 5. Option B: true PD sink negotiation

If you want 9 V or 12 V input for faster charging:
- **PD trigger:** CH224K or IP2721 (resistor/pin-configured request for 9/12 V), or an STUSB4500 (I²C/NVM, with a fallback PDO list).
- **Charger:** replace the IP2326 with a buck-boost 2S charger rated for 20 V input, such as the IP2368, BQ25713/BQ25798 class, or SC8886.
- **Power-path protection:** put a load switch or P-FET after the PD trigger so VBUS is only connected once the negotiated voltage is valid (use the PG pin).
- **Never** feed a negotiated 9 V or higher into the IP2326.

---

## 6. Layout guidelines

1. Keep the boost loop (VIN cap → inductor → SW → output cap → GND) as small as possible, with a solid ground plane.
2. Use Kelvin connections for the protection IC's VC1/VC2/VSS sense lines, directly at the cell tabs.
3. Size the B− → MOSFET → PACK− path with wide copper (≥ 2 oz, or a pour) for the discharge current.
4. Place the CC resistors close to the connector. Keep VBUS traces ≥ 1.5 mm for 3 A.
5. Mount the NTC on the cell surface, thermally isolated from the inductor and the bleed resistors.

---

## 7. Validation checklist

- [ ] Rd measures 5.1 kΩ on each CC pin. The source supplies VBUS only when attached.
- [ ] Charge current and termination voltage (8.40 V ±0.5%) verified with an electronic load and a 4-wire DMM.
- [ ] Each cell is clamped at ≤ 4.20 V under a deliberate 100 mV imbalance. Balancing bleed confirmed.
- [ ] Overcharge and over-discharge trip and release points verified with a