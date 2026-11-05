# Dual ICM-42688-P at 32 kHz: SPI Timing, DMA, and APEX Configuration

**About "Cycle 13":** This isn't a term from TDK/InvenSense documentation, so it's probably internal to your project or a framework. If it implies specific constraints (an MCU, RTOS tick, or frame budget), tell me and I'll adapt this. Below I assume an STM32H7-class MCU. Confirm all register values against the current ICM-42688-P datasheet revision before production.

---

## 1. APEX at 32 kHz is not a supported combination

The APEX features (pedometer, tilt, tap, raise-to-wake, wake-on-motion) run on the on-chip DMP. They are specified only for:

- accelerometer ODR between roughly 50 Hz and 1 kHz (tap needs 200 Hz, 500 Hz, or 1 kHz)
- DMP_ODR of 25, 50, 100, or 500 Hz

You cannot run APEX with the accelerometer at 32 kHz. The gyro and accel ODRs are independent, so the practical options are:

| Option | Gyro ODR | Accel ODR | APEX | Notes |
|---|---|---|---|---|
| **A (recommended)** | 32 kHz | 1 kHz | Yes (tap, tilt, pedometer, wake-on-motion) | Most control loops need fast gyro, not fast accel |
| B | 32 kHz | 32 kHz | No | Full-rate 6-axis; do motion detection on the MCU |
| C | 32 kHz on IMU0 | 1 kHz on IMU1 | APEX on IMU1 only | Asymmetric; loses redundancy |

The rest of this answer uses **Option A**, but the bus and DMA sizing assume worst case (full 16-byte packets at 32 kHz) so it also covers Option B.

---

## 2. Bus topology

Use **one SPI peripheral per IMU** (for example SPI1 → IMU0 and SPI4 → IMU1). Two separate buses give you:

- no arbitration or CS sequencing between the IMUs
- independent DMA streams
- true simultaneous sampling, since both INT1 lines can fire within nanoseconds of each other

A shared bus is possible (see the budget in section 3), but it serializes the reads and adds 10–15 µs of skew between the two sensors.

---

## 3. SPI electrical and timing parameters

| Parameter | Value |
|---|---|
| Mode | Mode 0 or Mode 3 (CPOL=CPHA=0 or 1) |
| SCLK max | 24 MHz (datasheet limit). Use **≤ 20 MHz** unless traces are short and termination is verified |
| Bit order | MSB first, 8-bit frames |
| Read/write bit | Address bit 7: 1 = read, 0 = write |
| CS setup/hold | Keep ≥ 40 ns. Use 1–2 SPI clocks of software or hardware delay (STM32H7: `MSSI`/`MIDI` fields) |
| Interface lock | Set `INTF_CONFIG0`/`DEVICE_CONFIG` to disable I²C and lock SPI (`INTF_CONFIG0` UI_SIFS_CFG = SPI-only) to avoid glitches |
| Init clock | Do register configuration at ≤ 1 MHz, then switch to the fast clock for FIFO reads |
| Signal integrity | 22–33 Ω series resistors on SCLK and MOSI at the MCU. Keep traces under 5–10 cm at 20+ MHz |

### Per-transfer budget (one IMU, FIFO packet 3 = 16 bytes)

Packet 3 contains: header (1) + accel (6) + gyro (6) + temp (1) + timestamp (2) = **16 bytes**.

| Item | @ 20 MHz | @ 24 MHz |
|---|---|---|
| 1 sample (1 address byte + 16 data = 136 bits) | 6.8 µs | 5.7 µs |
| Sample period at 32 kHz | 31.25 µs | 31.25 µs |
| Bus utilization (per-sample reads, one IMU per bus) | ~22% | ~18% |
| Shared bus, two IMUs | ~45% + CS gaps | ~37% + CS gaps |

Per-sample interrupts at 32 kHz × 2 IMUs = 64,000 IRQs/s, which is wasteful. **Use FIFO watermark batching.**

### Recommended batching

| Watermark | Batch period | Bytes per burst | Burst time @ 20 MHz | IRQ rate per IMU |
|---|---|---|---|---|
| 4 packets | 125 µs | 64 + 1 | 26 µs | 8 kHz |
| **8 packets** | **250 µs** | **128 + 1** | **52 µs** | **4 kHz** |
| 16 packets | 500 µs | 256 + 1 | 104 µs | 2 kHz |

Pick the batch period to match your control loop. An 8-packet watermark aligned to a 4 kHz loop is a common choice. The FIFO is 2 KB, so there is plenty of headroom if a read is delayed.

---

## 4. Register configuration (Bank 0 unless noted)

```c
// --- Reset & interface ---
WR(0x76, 0x00);          // REG_BANK_SEL = 0
WR(0x11, 0x01);          // DEVICE_CONFIG: soft reset
delay_ms(2);
RD(0x75);                // WHO_AM_I, expect 0x47

// --- Clocking / interface ---
WR(0x4D, 0x30);          // INTF_CONFIG1: RTC off, CLKSEL = PLL auto (verify bits vs datasheet)
WR(0x4C, 0x30 | 0x40);   // INTF_CONFIG0: big-endian FIFO/sensor data, FIFO_COUNT in RECORDS (bit6)

// --- ODR / full-