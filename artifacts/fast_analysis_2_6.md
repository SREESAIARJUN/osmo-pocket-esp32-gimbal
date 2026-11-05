# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Problems with the request as stated

1. **The ICM-42688-P has no APEX motion engine.** APEX (pedometer, tilt, tap, raise-to-wake) is on parts like the ICM-42688-V, ICM-42670-P, and ICM-42605. The -P exposes Wake-on-Motion and SMD only.
2. **APEX cannot run alongside a 32 kHz ODR anyway.** On parts that have it, APEX runs from a DMP ODR of 25, 50, 100, or 500 Hz, with accel ODR in the ≤1 kHz range.
3. **"Cycle 2" is not a configuration name I can find** in TDK documentation. If it comes from an internal spec or another vendor's terminology, please share the definition.

The rest of this answer covers what is achievable: two ICM-42688-P sensors at 32 kHz ODR, read by SPI with DMA. Optional WoM settings are at the end. **Verify all timing numbers against the current datasheet revision (DS-000347).**

---

## 1. SPI Bus Parameters

| Parameter | Value |
|---|---|
| Mode | SPI Mode 0 or Mode 3 (CPOL=CPHA, sampled on rising edge) |
| Max SCLK | 24 MHz |
| Bit order | MSB first |
| Read flag | Address bit 7 = 1 |
| Auto-increment | Yes, for burst reads |
| CS setup/hold | Tens of ns (see datasheet SPI timing table) |
| Interface lock | Set `INTF_CONFIG4`/`DEVICE_CONFIG` and disable I²C (`INTF_CONFIG0[1:0] = 0b11`) |

At 24 MHz, signal integrity matters. Keep traces short (<5 cm ideal), add 22–33 Ω series termination on SCLK and MOSI at the MCU, and use solid ground under the bus.

## 2. Timing Budget at 32 kHz

The sample period is **31.25 µs**.

**Option A: per-sample register burst (not recommended)**
- Reads `TEMP_DATA1` (0x1D) through `GYRO_DATA_Z0` (0x2A): 1 address byte + 14 data bytes = 15 bytes.
- At 24 MHz this takes 15 × 8 / 24 MHz ≈ **5.0 µs**, plus CS and DMA setup of about 1–2 µs.
- Two sensors on a shared bus need about 14 µs, which is 45% bus utilization.
- It also generates 64,000 interrupts per second. Jitter and missed samples are likely.

**Option B: FIFO with watermark (recommended)**
- FIFO packet 3 (accel + gyro + temp + timestamp) is 16 bytes.
- Packet 4 (20-bit high-resolution) is 20 bytes.
- Data rate per sensor is 32,000 × 16 B = 512 kB/s. Both sensors together produce about 1.02 MB/s.
- With a watermark of 16 packets (256 B), each sensor interrupts at 2 kHz.
- One burst is 1 + 2 (FIFO_COUNT) + 256 bytes, about 11 µs at 24 MHz.
- The 2 KB FIFO holds about 4 ms of data, giving margin against latency.

**Bus topology:** use **separate SPI peripherals per IMU** if available. This allows fully parallel DMA, isolates faults, and keeps clock domains independent. A shared bus with two CS lines also works with Option B, since utilization is about 5%.

## 3. Register Configuration (Bank 0, per sensor)

| Step | Register | Addr | Value | Purpose |
|---|---|---|---|---|
| 1 | DEVICE_CONFIG | 0x11 | 0x01 | Soft reset, then wait ≥1 ms |
| 2 | WHO_AM_I | 0x75 | read → 0x47 | Verify device |
| 3 | INTF_CONFIG0 | 0x4C | 0x30 / 0x33 | Big-endian FIFO count and data; disable I²C |
| 4 | INT_CONFIG | 0x14 | 0x03 | INT1 push-pull, active-high, pulsed |
| 5 | INT_CONFIG1 | 0x64 | clear bit 4 | `INT_ASYNC_RESET = 0` (required per datasheet) |
| 6 | GYRO_CONFIG0 | 0x4F | 0x01 | ±2000 dps, 32 kHz |
| 7 | ACCEL_CONFIG0 | 0x50 | 0x01 | ±16 g, 32 kHz |
| 8 | GYRO_ACCEL_CONFIG0 | 0x52 | as needed | UI filter bandwidth |
| 9 | TMST_CONFIG | 0x54 | 0x23 | Timestamp enable, FSYNC off |
| 10 | FIFO_CONFIG1 | 0x5F | 0x07 (+0x10 for hi-res) | Accel, gyro, and temp to FIFO |
| 11 | FIFO_CONFIG2/3 | 0x60/0x61 | 0x00 / 0x01 | Watermark = 256 bytes |
| 12 | FIFO_CONFIG | 0x16 | 0x40 | Stream-to-FIFO mode |
| 13 | INT_SOURCE0 | 0x65 | 0x04 | FIFO threshold → INT1 |
| 14 | PWR_MGMT0 | 0x4E | 0x0F | Accel and gyro in Low-Noise mode |
| 15 | Wait | — | ≥45 ms | Gyro startup before using data |

Rules for writing these:
- Do not write registers within 200 µs of a `PWR_MGMT0` change.
- Never switch power modes faster than the datasheet allows.

## 4. DMA Configuration (generic; STM32H7-style example)

**Per IMU, use one SPI peripheral with two DMA streams (TX and RX).**

| Setting | RX stream | TX stream |
|---|---|---|
| Direction | Periph → Mem | Mem → Periph |
| Data width | 8-bit / 8-bit | 8-bit / 8-bit |
| Mem increment | Yes | Yes (or No, with a static 0x00 dummy buffer) |
| Mode | Normal (re-armed per transaction) | Normal |
| Priority | Very High (RX) | High |
| FIFO | Enabled, threshold full | Enabled |
| Buffer location | Non-cacheable RAM (e.g., D2 SRAM via MPU) or clean/invalidate D-cache | Same |

**Transaction flow (per watermark interrupt):**

1. The INT1 EXTI