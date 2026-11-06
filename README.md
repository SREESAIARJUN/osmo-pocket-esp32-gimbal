# 3-Axis Ultra-Smooth Gimbal (DJI Osmo Pocket Form Factor)

**Autonomous Engineering Release — Revision 7.0 / 150**  
*Powered by ESP32-S3 Dual-Core LX7 @ 240MHz, SimpleFOC Field-Oriented Control, and Real Physics Simulation.*

---

## System Overview
- **Form factor**: handheld stabilizer, 32 mm × 35 mm × 128 mm handle.
- **MCU**: ESP32-S3-WROOM-1-**N8R8** (dual Xtensa LX7 @ 240 MHz, 8 MB quad flash, 8 MB octal PSRAM, native USB).
- **Motor control**: 3× TMC6300-LA low-voltage 3-phase BLDC drivers, 12N14P gimbal motors (14 pole pairs).
- **Encoders**: 3× AS5600 **12-bit** (4096 counts/rev) magnetic encoders behind a **TCA9548A** I2C switch.
- **IMUs**: 2× ICM-42688-P on SPI. One sits on the camera cradle for attitude; the other sits on the handle for base-motion feed-forward.
- **Power**: 2S Li-ion (7.4 V nominal), USB-C PD charging, TPS7A4700 low-noise LDO.

## Repository Contents
| Path | Contents |
|---|---|
| `cad/`, `stl/` | Parametric OpenSCAD sources and printable STL meshes |
| `kicad/` | KiCad 8 schematic and 4-layer PCB, `kicad/tools/gen_rev7_schematic.py` (schematic generator), and `gerbers/` |
| `firmware/` | PlatformIO project (`platformio.ini`), SimpleFOC firmware in `src/`, host-side unit tests in `test/host/` |
| `simulation/` | Roll-axis physics simulator, encoder-bus timing budget, and Wokwi project |

### Build & verify
```bash
cd firmware && pio run -e esp32-s3-devkitc-1     # cross-compile (pioarduino / Arduino-ESP32 3.x)
./firmware/test/host/run_host_tests.sh           # host unit tests + compile-time pin-map negative tests
cd simulation && python3 gimbal_physics_sim.py && python3 encoder_bus_budget.py
kicad-cli sch erc --severity-error kicad/osmo-pocket-esp32-gimbal.kicad_sch
```

---

## Revision 7.0 Engineering Changelog
**Task:** fix the AS5600 I2C address conflict with a TCA9548A multiplexer, and put every signal on a valid ESP32-S3 GPIO.

### Defects found in Rev 6.0
| # | Defect | Effect |
|---|---|---|
| D1 | All three `MagneticSensorI2C(AS5600_I2C)` instances shared one bus. The AS5600's 7-bit address is fixed at **0x36** (only the AS5600**L** can be re-addressed). | All three encoders ACKed every frame and drove SDA together. Each axis read the **wired-AND** of three angles, so commutation used garbage angles. The host test `legacy_shared_bus_reproduces_address_conflict` reproduces this. |
| D2 | `Wire.begin(21, 22, …)`: **GPIO22 does not exist on the ESP32-S3** (no pads at GPIO22–25). | The encoder bus could never come up on real hardware. |
| D3 | The anti-cogging term was added to `voltage.q` *before* `move()`, which overwrites `voltage.q`. | The feed-forward was never applied. |
| D4 | PID gains were hard-coded in `main.cpp`. The `ROLL_KP=22.30`/`ROLL_KD=0.186` in `gimbal_config.h` were unused. | The Rev 6.0 tuning claims didn't match the firmware (it ran 22.0/0.18). |
| D5 | `simulation_report.json` hard-coded `"status": "PASS"` and `"SUB-0.05_DEGREE_HORIZON_LOCK"`. | It reported PASS while its own measured error was **0.39° RMS**, about 8× over the spec. |
| D6 | The docs said the AS5600 was 14-bit. | It is 12-bit (0.088°/count). |
| D7 | The schematic and PCB were title-block stubs with no components or nets. | Nothing could be checked. |

### Changes implemented
**Firmware (`firmware/src/`)**
- **`gimbal_pins.h` (new): one place for every GPIO, checked by the compiler.** `static_assert`s reject any pin that is:
  - non-existent (GPIO22–25)
  - used by in-package flash (GPIO26–32) or octal PSRAM (GPIO33–37)
  - a strapping pin (0/3/45/46)
  - native USB (19/20), UART0 (43/44) or JTAG (39–42)
  - assigned twice

  `VBAT_SENSE` must also be on ADC1, because ADC2 is unavailable while Wi-Fi is active.

  | Function | GPIO |
  |---|---|
  | Pan PWM A/B/C, EN | 4, 5, 6, 7 |
  | Roll PWM A/B/C, EN | 8, 9, 10, 11 |
  | Pitch PWM A/B/C, EN | 12, 13, 14, 15 |
  | Encoder I2C0 SDA / SCL | **17 / 18** (Rev 6: 21 / 22 ✗) |
  | TCA9548A RESET_N | 2 |
  | IMU SPI SCK / MOSI / MISO | 21 / 38 / 47 |
  | IMU CS camera / handle | 48 / 16 |
  | VBAT_SENSE (ADC1_CH0) | 1 |
- **`tca9548a.{h,cpp}` (new): driver for the I2C switch at 0x70.**
  - Hardware reset over RESET_N, plus a presence check that reads back the control register.
  - Exactly one channel is enabled at a time. The selected channel is cached, so re-selecting the same channel costs nothing on the bus.
  - `i2c_bus_clear()` frees a stuck bus: up to 9 SCL clocks, then a STOP (UM10204 §3.1.16).
- **`as5600_mux.{h,cpp}` (new):**
  - **`EncoderBus` probes each encoder at startup**, one mux channel per encoder (PAN=ch0, ROLL=ch1, PITCH=ch2). It reads `STATUS` (MD/ML/MH) and `AGC`. An axis with a missing device or a bad magnet is never powered.
  - **Fast read:** the AS5600 `RAW ANGLE` address pointer does not advance past the low byte. Once the pointer is parked on 0x0C, each sample is one 2-byte read frame instead of write + read. This saves 29 % of bus time.
    - A startup self-test does 4 back-to-back reads. It catches a device whose pointer does advance and falls back to full reads for it.
    - Bits 15:12 must read 0 on every sample, as an integrity check.
  - **Sweep task on core 0:** a FreeRTOS task polls the three encoders, and the FOC loop runs on core 1, so I2C never blocks `loopFOC()`. Samples are handed across with a lock-free seqlock built on `std::atomic`, and each carries a sequence number and a capture time.
  - **Fault handling:**
    - After 8 consecutive failed reads on an axis, the driver frees the bus (9-clock bus clear), pulses the mux reset, re-initialises I2C and re-parks the pointers.
    - If the axis then fails another 8 reads, it latches `BUS_FAULT` and the other axes keep running.
    - The bus clock is capped at 400 kHz, the TCA9548A's Fast-mode limit, and each Wire transfer times out after 2 ms.
  - **`MuxedAS5600` (a SimpleFOC `Sensor`)** only advances when a *new* sample arrives, and uses the sample's **capture timestamp**. `getVelocity()` therefore differentiates over the real sampling interval, with no zero-delta spikes from re-reading cached data.
- **`main.cpp`:**
  - Builds each axis from the pin map and config.
  - An axis is only started if its encoder is healthy and `driver.init()` and `initFOC()` both succeed.
  - **Encoder watchdog:** an axis is disabled and stays off if its sample is older than `ENC_STALE_LIMIT_US` (10 ms) or its encoder is faulted.
  - Loop order is now `loopFOC → move → add anti-cogging`, clamped to `voltage_limit` (fixes D3).
- **`gimbal_config.h`:** holds all axis gains and limits (fixes D4) and the encoder-bus parameters (`ENC_I2C_HZ`, `ENC_MUX_ADDR`, channel map, task core/priority, stale limit).
- **`platformio.ini` (new):** env `esp32-s3-devkitc-1`, which matches the path in `wokwi.toml`.
  - Uses `qio_opi` memory for N8R8, SimpleFOC 2.3.4, and loop() on core 1.
  - Uses the **pioarduino** platform, because SimpleFOC 2.3.4 needs Arduino-ESP32 3.x / ESP-IDF 5. The official `espressif32@6.x` platform fails with `#error ESP-IDF version 4 or lower detected`.

**Electronics (`kicad/`)**
- **The schematic is now a real circuit.** It is generated by `kicad/tools/gen_rev7_schematic.py`, which gives deterministic UUIDs and has a netlist self-check. Its contents:
  - U1 ESP32-S3-WROOM-1-N8R8, with an EN RC network and 3V3 decoupling.
  - U2 TCA9548APWR: A2..A0 tied to GND (address 0x70), RESET on GPIO2 with a 10 k pull-up, 2.2 k upstream pull-ups, and 100 nF decoupling.
  - Three channels, each with 4.7 k pull-ups on the encoder side, a 4-pin flex connector (J1–J3), and an AS5600 (U3–U5) running at 3.3 V (VDD5V tied to VDD3V3), with DIR and PGO to GND.
  - Unused mux channels 3–7 and unused module pins have no-connect flags.
  - Motor, IMU and VBAT signals are global labels on the validated GPIOs.
- **PCB:** title block set to Rev 7.0, plus `REV7 TCA9548A@0x70` on the silkscreen.

**Simulation (`simulation/`)**
- **`encoder_bus_budget.py` (new):** bit-level timing at 400 kHz.
  - Fast read: **147 bits = 367.5 µs per sweep, about 2.7 kHz per axis**. That is 0.30° electrical commutation error at 1 rad/s and 0.79° phase lag at the 6 Hz angle-loop crossover.
  - Full write + read: 517.5 µs and 1.9 kHz.
  - Results go into `simulation_report.json → encoder_bus`.
- **`gimbal_physics_sim.py`:** the verdict now comes from the measured RMS against the 0.05° limit (fixes D5), and the noise uses a fixed seed so runs are reproducible.
- **`diagram.json`:** the Wokwi I2C wiring moved from 21/22 to 17/18.

### Architecture improvements
```
            core 0 (enc_i2c task, prio 5)                  core 1 (loop)
 I2C0 GPIO17/18 @400k ─► TCA9548A 0x70 ─┬ ch0 ─► AS5600 0x36 (PAN)    ┐
          RESET_N GPIO2 ───────────────►├ ch1 ─► AS5600 0x36 (ROLL)   ├─► seqlock[3] ─► MuxedAS5600 ─► loopFOC/move ×3
                                        └ ch2 ─► AS5600 0x36 (PITCH)  ┘   {raw, t_us, seq}            + stale watchdog
 IMUs moved to SPI (GPIO21/38/47, CS 48/16) so that IMU traffic never shares the encoder bus.
```
- **Address plan:** TCA9548A at 0x70 upstream. Downstream each AS5600 is alone at 0x36 on its own channel, so 0x36 is never visible twice. Channels 3–7 are left free for expansion (for example a handle-side sensor), with no address re-plan needed.
- **Fault isolation:** a faulty encoder, missing magnet, or stuck channel takes out only its own axis, and only after automatic bus recovery has been tried.

### Validation status
| Check | Result |
|---|---|
| Cross-compile, `pio run -e esp32-s3-devkitc-1` (Arduino-ESP32 3.x, SimpleFOC 2.3.4) | ✅ SUCCESS: RAM 8.9 % (29,072 B), flash 11.8 % (395,332 B). No warnings from project sources (the only warnings come from SimpleFOC's own headers). |
| Host unit tests (`run_host_tests.sh`, g++ with `-Wall -Wextra -Werror` + ASan/UBSan, real `tca9548a.cpp`/`as5600_mux.cpp` against an I2C bus model) | ✅ 15 tests / 99 checks pass. Covered: the address-conflict repro, per-axis isolation through the mux, the 400 kHz clamp, the sweep budget, fast-read fallback, magnet diagnostics, missing mux/encoder, transient recovery, BUS_FAULT latching, select caching, the seqlock, and turn tracking in the SimpleFOC adapter. |
| Compile-time pin-map negative tests | ✅ GPIO22, PSRAM GPIO35, strapping GPIO46, a duplicate pin, and VBAT off ADC1 are each rejected by `static_assert`. |
| KiCad ERC (`kicad-cli sch erc --severity-error`) | ✅ 0 errors. The 109 warnings are 91× "library 'gimbal' not in config" (symbols are embedded in the schematic) and 18× dangling global labels for motor/IMU/VBAT circuits not drawn yet. |
| Netlist self-check | ✅ All 30 expected nets match. For example, ENC_SCL = U1.11 (IO18) + U2.22; ENC_MUX_RST_N = U1.38 (IO2) + U2.3. |
| PCB DRC | ✅ 0 violations (outline-only board) |
| Encoder bus budget | ✅ PASS: 2.7 kHz/axis (requirement ≥ 2 kHz) from bit timing only; see open item 2 |
| Roll horizon-lock simulation | ❌ **FAIL: 0.392° RMS vs 0.05° spec** (−24.3 dB attenuation). Rev 6.0 marked this as passing; the simulator now reports it honestly. Re-tuning is tracked for a later revision. |

### Open items carried forward
1. **TMC6300 input topology:** the TMC6300 has six gate inputs (UH/UL/VH/VL/WH/WL), but the firmware drives it as 3PWM + EN. A 6PWM pin map needs 18 PWM GPIOs. The validated pool is only 21 general-purpose GPIOs, and all 21 are allocated in Rev 7.0. Moving to 6PWM therefore means either releasing the JTAG pins (39–42), adding an I/O expander for the slow signals, or choosing a different driver. This has to be settled before PCB layout.
2. **Hardware sweep time:** Arduino-ESP32 adds per-transaction driver overhead, estimated at 20–50 µs × 6 transactions per sweep. Measure `EncoderBus::lastSweepUs()` on hardware. The fallback, if per-axis rate drops below 2 kHz, is to run the AS5600s at 1 MHz behind a faster switch such as the PCA9846, or to move to SPI encoders.
3. **Schematic pinouts were written from memory**, because no datasheet PDFs or KiCad libraries are installed on the build host. Check the ESP32-S3-WROOM-1, TCA9548APWR and AS5600-ASOM pin numbers against their datasheets before layout. Native USB (GPIO19/20) and the BOOT strap (GPIO0) are no-connect in this sheet. The production board needs the USB-C data lines and a BOOT button on them.
4. **Roll-loop tuning** to meet the 0.05° spec (see validation).

---

### Revision 6.0 Engineering Changelog
- **Mechanical CAD**: handle wall thickness tuned to 2.12 mm and corner fillet to 3.30 mm. STL meshes recompiled.
- **Electronics**: KiCad 8 project updated to Rev 6.0; 4-layer RS-274X Gerbers and drill files regenerated.
- **Firmware**: roll-axis PID set to $K_p=22.30$, $K_d=0.1860$ (only took effect in Rev 7.0, see D4). 512-point anti-cogging LUT regenerated.
- **Simulation**: claimed $<0.05^\circ$ RMS horizon lock. Rev 7.0 found this was never met (see D5).
