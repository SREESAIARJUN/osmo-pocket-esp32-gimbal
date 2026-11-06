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
| `firmware/` | PlatformIO project (`platformio.ini`), SimpleFOC firmware in `src/` (including the Rev 57 IMU stack: `icm42688`, `madgwick_ahrs`, `imu_bias`, `imu_pipeline`), host-side unit tests in `test/host/` |
| `simulation/` | Roll-axis physics simulator, encoder-bus timing budget, and Wokwi project |

### Build & verify
```bash
cd firmware && pio run -e esp32-s3-devkitc-1     # cross-compile (pioarduino / Arduino-ESP32 3.x)
./firmware/test/host/run_host_tests.sh           # host unit tests + compile-time pin-map negative tests
cd simulation && python3 gimbal_physics_sim.py && python3 encoder_bus_budget.py
kicad-cli sch erc --severity-error kicad/osmo-pocket-esp32-gimbal.kicad_sch
```

---

## Revision 57.0 Engineering Changelog
**Task:** firmware for the ICM-42688-P: a 1 kHz SPI FIFO reader, a Madgwick AHRS, and compensation for temperature-driven gyro bias drift.

### Starting point
The pin map reserved SPI2 for two ICM-42688-P (SCK 21 / MOSI 38 / MISO 47, CS 48 camera / 16 handle) and the System Overview advertised dual IMUs. However, **`firmware/src/` contained no IMU code at all**: no driver, no attitude estimate, and nothing for the stabiliser to close a horizon loop on.

### Changes implemented
**`icm42688.{h,cpp}` (new): register-level SPI FIFO driver**
- **Bring-up.** Soft reset, then a WHO_AM_I check (`0x47`). Every configuration register is written and then **read back**, so a stuck MISO, a wrong CS or an ICM-42605 fitted by mistake is reported as `NOT_PRESENT` or `CONFIG_FAULT` instead of streaming garbage.
- **Configuration.**
  - Gyro and accel in Low-Noise mode at **ODR 1 kHz**, UI filter BW ODR/4 (250 Hz).
  - I2C slave disabled (`INTF_CONFIG0 = 0x73`).
  - Big-endian data, `FIFO_COUNT` reported in **records**.
  - Absolute 1 µs timestamp (`TMST_CONFIG = 0x01`).
  - FIFO in stream mode.
- **Hi-res FIFO packet 4** (20 bytes/record, `FIFO_CONFIG1 = 0x1F`). Each record holds a 20-bit accel/gyro value (8-bit high/mid bytes plus a nibble in the extension byte), a **16-bit temperature** and a 16-bit timestamp.
  - Full scale is fixed at ±2000 dps / ±16 g with 2¹⁹ LSB = FS, i.e. 262.144 LSB/dps and 32768 LSB/g. A hard handle flick cannot clip the gyro, and resolution is still 0.0038 dps.
  - The FIFO temperature is 7.5 mK/LSB (raw/132.48 + 25 °C), instead of the 0.48 °C step of the 8-bit packet 3. That resolution is what makes a temperature bias model usable.
- **Two SPI transactions per service.**
  1. A 3-byte burst at `INT_STATUS..FIFO_COUNTL`. This clears `FIFO_FULL` and latches the count in one go.
  2. One burst of N×20 bytes.

  At 8 MHz that is about 22 µs of bus time per record, under 5 % of SPI2 for both IMUs at 1 kHz.
- **Fault handling.**
  - **Overflow:** sets the resync flag, so dt is not trusted across the gap.
  - **Impossible count:** the FIFO is flushed.
  - **Header mismatch:** the FIFO is flushed and resync is flagged. A record header with the "empty" bit set *inside* the latched count also counts as lost alignment (see review fix R2).
  - **Invalid-sample markers** (`0x8000` in bits [19:4]): the record is dropped.
- **`ArduinoSpiDevice`** (SPI mode 3) only uses `transferBytes()` on 4-byte-aligned buffers of at least 8 bytes. The HAL stores through `uint32_t*`, and an unaligned store traps on the LX7. Short register reads go byte-by-byte.

**`madgwick_ahrs.{h,cpp}` (new): 6-DoF Madgwick gradient-descent filter**
- Single-precision only. q maps body to earth (+Z up), and Euler angles are aerospace Z-Y-X.
- **Initial alignment from the accelerometer.** `beta_init = 1.0` rad/s is used for 0.5 s, then the steady-state `beta = 0.02` rad/s. That allows about 1.3 dps of residual gyro error, and the correction slew is capped at 2β = 2.3 °/s.
- **Accelerometer magnitude gating.** The weight is 1 below |‖a‖−1 g| = 0.05 g and ramps linearly to 0 at 0.20 g, so handle swings do not tilt the horizon.
- Variable dt per sample. Bad dt values (≤ 0, NaN, > 100 ms) are rejected, and a NaN blow-up resets the filter and re-aligns it.
- Optional Madgwick ζ gyro-bias integrator (eq. 47–48), clamped at 2 dps. It defaults to **off**, because bias is handled by `imu_bias` and is unobservable about the gravity axis anyway.

**`imu_bias.{h,cpp}` (new): gyro zero-rate output vs. temperature**
- **Model:** per sensor axis, `b(T) = b0 + b1·(T − 25 °C)`.
- **Estimator:** a 2-state Kalman filter per axis, with `H = [1, T−25]` and `R = var/N + floor`.
  - Priors: b0 ±1 dps, b1 ±0.01 dps/°C.
  - Process noise on the offset is 1e-11 (rad/s)²/s, matched to the bias instability.
  - A single temperature only anchors b0. Once the board has warmed through a span, the slope becomes observable and is learned.
  - The die temperature is low-passed with τ = 0.5 s before the model is evaluated.
- **`StillnessDetector`:** 500-sample (0.5 s) windows. A window counts as still only if all of these hold:
  - gyro σ < 0.2 dps
  - accel σ < 6 mg
  - |‖a‖ − 1 g| < 0.05
  - |mean rate| < 3 dps
  - gravity rotated by less than 0.034° between the two halves of the window. This last test rejects slow constant rotation about any non-vertical axis.

  Sums are taken about the first sample of the window, so float is exact enough and no soft-double is needed.
- **Innovation gate at 5σ,** with re-anchoring. After 6 consecutive rejected windows that agree with each other, the offset covariance is inflated to the innovation and the window is fused (see review fix R1).
- **Persistence:** a `GyroTempModel` snapshot with CRC-32, plus a PSD and finiteness check on import. On import the offset σ is floored at the 0.1 dps turn-on repeatability.

**`imu_pipeline.{h,cpp}` (new): `ImuChannel`, one per IMU**
- The per-record pipeline is:
  1. **dt** from device-timestamp deltas × the measured **device-clock ratio**. The ratio is taken against `micros()` over 2 s windows, only when the FIFO is fully drained. The ICM's internal oscillator tolerance would otherwise become a gyro-integration scale error: 1.5 % equals 1.35° on a 90° pan.
  2. Temperature low-pass, then gyro − b(T) in the **sensor** frame.
  3. Stillness detector, feeding the bias model.
  4. Signed-permutation remap to the body frame. `ImuMount::valid()` is `constexpr`: it rejects duplicate axes and mirrored (det = −1) maps at compile time.
  5. Madgwick update.
- `AttitudeState` is published through `SeqLock` and holds: q, roll/pitch/yaw, bias-corrected rates, accel, the current bias, temperature, accel weight, clock scale, timestamps, counters, status, and these flags: `ALIGNED`, `BIAS_LEARNED`, `SLOPE_LEARNED`, `STILL`, `ACCEL_GATED`, `GAP`, `CLOCK_LOCKED`.
- **Self-healing.** If no record arrives for 50 ms, `begin()` runs again (brown-out or CS glitch). Each service drains at most 64 records, so one call never monopolises the task.
- **`setLearningInhibit()`.** A slow motorised pan passes every stillness test and would otherwise be learned as bias, so the stabiliser must set this flag while it commands motion.
- **`start_imu_task()`** creates the `imu_spi` FreeRTOS task:
  - pinned to **core 0** at **priority 6**, above the encoder sweep (5), so the 1 ms tick preempts the I2C sweep
  - `xTaskDelayUntil(1 tick)`, with `static_assert(configTICK_RATE_HZ == 1000)`
  - a 6 KB stack
  - the only owner of the shared SPI2 bus

**`main.cpp` / `gimbal_config.h`**
- `SPIClass(FSPI)` is used with both chip selects driven high before the bus clocks.
- Two `ImuChannel`s are built from the new `IMU_*` / `AHRS_*` / `GYRO_BIAS_*` configuration block. The mount maps are checked with `static_assert`.
- The learned bias(T) model is **restored from NVS** at boot. It is saved on the `B` serial command, **only while every motor is disabled**, because a flash write stalls both cores' caches mid-commutation.
- Optional attitude telemetry via `-DIMU_TELEMETRY_PERIOD_MS=…`.

**Pre-existing defects fixed along the way (anti-cogging, Rev 8.0 code)**
- `test_anti_cogging.cpp` **had not been compiling.** `bad.add(LUT_SIZE/2, …)` overflowed the `uint8_t` parameter under `-Werror`, so the whole anti-cogging suite never ran.
- `HarmonicSet::add(uint8_t order)` **silently truncated** orders above 255. For example 268 became 12, so a wrong harmonic was accepted. It now takes `int` and rejects anything outside 1 … LUT_SIZE/2−1.
- `Lut::build()` accepted an **empty** set and zeroed the live cogging table. It now returns false and keeps the previous table.
- Four feed-forward assertions had demanded 0.1 % accuracy at a point between LUT samples, below the 0.47 % interpolation bound that the same file proves. They had been hidden by the compile error. The tolerance is now set to that bound.

### Architecture
```
 core 0  imu_spi task (prio 6, 1 kHz)              enc_i2c task (prio 5)          core 1  loop()
 SPI2 8 MHz mode 3 ─┬ CS48 ICM-42688-P (camera) ─► ImuChannel ─► SeqLock<AttitudeState> ─┐
                    └ CS16 ICM-42688-P (handle) ─► ImuChannel ─► SeqLock<AttitudeState> ─┼─► stabiliser / telemetry
  per record: FIFO(20 B, tmst) → dt·clk → T lpf → ω−b(T) → stillness→Kalman b0,b1         │   FOC ×3 (unchanged)
              → mount remap → Madgwick(gated) ─────────────────────────────────────────────┘
 NVS "imu_bias": GyroTempModel (CRC-32) restored at boot, saved on 'B' with motors idle
```

### Adversarial review (multi-agent workflow) and fixes
Four independent reviewers covered datasheet correctness, RTOS/concurrency, estimator maths and test validity. Each finding then went to a skeptic told to refute it. **3 findings were confirmed and 8 refuted.** All confirmed findings are fixed and each has a regression test:

| # | Finding | Fix |
|---|---|---|
| R1 | **The bias Kalman could lock out.** Once converged, the 5σ gate is about 0.02 dps wide, and rejections never widened P. A 0.04 dps bias step went untracked for **1122 s**. After an NVS import (which added only q·age = 3.6e-8), a 0.08 dps turn-on shift was rejected for **about 50 min**, i.e. 4.8°/min of pan creep. | Re-anchor after 6 consistent rejections, so a 0.04 dps step is tracked in **3.0 s**. A single outlier is still gated out. The import covariance is floored at 0.1 dps. |
| R2 | **A 1-byte FIFO slip could look like "FIFO empty".** The slip only went undetected when the stray header byte had bit 7 set, i.e. a negative accel-X high byte. Then 50 ms of silence was followed by a full re-init that stalled *both* IMUs. | A header with the empty bit inside the latched count is treated as an alignment error and the FIFO is flushed. Streaming resumes within 2 ms with no re-init. |
| R3 | **The clock-ratio window opened or closed with a FIFO backlog pending**, so after a stall `clock_scale` could be up to about 2 % off. | The window only opens or closes when the FIFO is fully drained. Across 80 ms and 130 ms stalls it stays within 1e-3. |
| + | Hardening from refuted-but-cheap items: `begin()` now timestamps *after* its 52 ms of start-up delays, and a test covers a rotated (non-identity) mount with the bias removed in the sensor frame. | |

### Validation status
| Check | Result |
|---|---|
| Cross-compile, `pio run -e esp32-s3-devkitc-1` (Arduino-ESP32 3.1.3) | ✅ SUCCESS: RAM 9.7 % (31,920 B), flash 12.9 % (431,264 B). Project sources produce no warnings. |
| Soft-float audit of the Xtensa objects (`objdump -dr`, 2,376 instructions in the 4 new modules) | ✅ **0** calls to `__adddf3/__muldf3/__divdf3/__subdf3/__extendsfdf2` |
| Host build of the new modules with `-Wall -Wextra -Werror -Wconversion -Wdouble-promotion -Wshadow` plus ASan/UBSan | ✅ clean |
| `test_imu.cpp`: 22 tests / **133 checks**. These run against a register-level ICM-42688-P model (soft reset, clear-on-read INT_STATUS, record-count FIFO, stream overflow, flush, 20-bit packet-4 encoding, device-clock error, true bias(T), noise). | ✅ all pass |
| ↳ Warm-up yaw drift: 150 s still at 25→32 °C, then 300 s handheld (tremor, never still) at 32→45 °C, with true bias 0.25 dps + 0.006 dps/°C on Z | ✅ **−0.29°** with the temperature model, vs **12.45°** with constant-offset calibration and about 99° raw |
| ↳ Device clock 1.5 % fast, 90° pan | ✅ clock_scale = 1/1.015 ± 5e-4, pan 90° ± 0.25° (the uncorrected result would be 91.35°) |
| ↳ Service jitter 1–7 ms (starved task) | ✅ 90° ± 0.1°, 0 overflows. The timestamps keep dt exact. |
| ↳ Static alignment / AHRS hold / 90°/s tracking / gating at 0.8 g | ✅ ±0.05° / ±0.05° over 20 s / ±0.2° / 0° tilt (ungated: > 1°) |
| `test_anti_cogging.cpp` (compiles again) | ✅ 5,931 checks pass |
| `test_encoder_mux.cpp` | ✅ 111 checks pass |
| Negative pin-map compile tests | ⚠️ "strapping GPIO46" now reports *accepted*. This is **not caused by Rev 57**. A concurrent revision (the Rev 59 joystick work, uncommitted in the shared tree) moved `ENC_MUX_RST_N` from GPIO2 to GPIO40, so the runner's `sed 's/ENC_MUX_RST_N = 2;/…/'` no longer matches anything and nothing is mutated. That revision's owner needs to update the sed target. |

### Known limitations / open items
1. **Not yet validated on hardware.** On hardware, check: WHO_AM_I and the readback on both CS lines; the FIFO record rate (expect 1000 ± 1 %/s); `clock_scale`; and the IMU task's CPU time and its jitter on the encoder sweep.
2. **Yaw is unobservable** without a magnetometer. Yaw drift is bounded only by how good the bias model is. Learning must be inhibited during commanded pans; the stabiliser does not call `setLearningInhibit()` yet, because there is no attitude-to-motor-target loop yet.
3. A **magnitude-only accel gate** cannot reject a small lateral acceleration (0.3 g gives ‖a‖ = 1.044 g). That error is bounded only by the 2β slew of 2.3°/s. A centripetal/linear-acceleration feed-forward from the handle IMU is the next step.
4. The bias model is linear in T. If hardware shows curvature or hysteresis beyond the re-anchor behaviour, add a quadratic term (a 3-state filter).
5. The **IMU mount maps** are identity placeholders until the PCB placement of U-IMU1/U-IMU2 is final.

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
