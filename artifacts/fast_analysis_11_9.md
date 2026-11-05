# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Corrections Before the Design

**1. APEX is not available on the ICM-42688-P.**
The APEX motion engine (pedometer, tilt, tap, raise-to-wake) is a feature of other TDK InvenSense parts, such as the ICM-42670-P and ICM-42605. The ICM-42688-P offers only Wake-on-Motion (WOM) and Significant Motion Detection (SMD). On parts that do have APEX, it runs at a DMP rate of 25–500 Hz, so it cannot coexist with a 32 kHz pipeline in any case. If you need APEX-style gestures, compute them on the host from the decimated 32 kHz stream, or add a separate low-power IMU.

**2. "Cycle 11" doesn't match anything I recognize.**
It isn't an ICM-42688-P datasheet term. If it refers to your project's internal spec or revision, please share the requirements and I can align the design to them.

Below is the design for the part as it actually exists.

---

## 1. Bus Topology

**Recommended: one SPI peripheral per IMU** (e.g., SPI1 and SPI2), each with its own DMA streams.
- Avoids bus arbitration jitter.
- Lets both IMUs sample simultaneously.

If you must share a bus, use separate chip-selects and sequence the transfers. The budget below shows this still fits, but with less margin.

## 2. SPI Electrical and Timing Parameters

| Parameter | Value |
|---|---|
| Mode | 0 or 3 (both supported). Mode 3 is common. |
| Bit order | MSB first |
| Max SCLK | 24 MHz in the datasheet. 20–21 MHz is a practical target for signal-integrity margin. |
| Read/write bit | Bit 7 of the address byte: 1 = read |
| Address auto-increment | Yes (burst reads) |
| CS setup, hold, and inter-frame gap | Take exact values from the datasheet's SPI timing table. Hardware NSS or a GPIO with ≥100 ns guard is safe. |
| Interface lock | Set `INTF_CONFIG0`/`DEVICE_CONFIG` early so the I²C interface is disabled (`I2C_SLEW`/`SPI_SLEW` in `DRIVE_CONFIG` as appropriate for trace length) |

**Layout tips:**
- Keep traces under 5 cm.
- Add 22–33 Ω series termination on SCLK and MOSI.
- Give each IMU a solid ground return.

## 3. Timing Budget at 32 kHz

- **Sample period:** 31.25 µs.

### Option A: Per-sample data-register read (lowest latency)

- Burst read from `TEMP_DATA1` (0x1D) through `GYRO_DATA_Z0` (0x2A) = 14 data bytes + 1 address byte = 15 bytes = 120 bits.

| SCLK | Transfer time | Plus CS/DMA overhead (~1–2 µs) | Bus utilization per IMU |
|---|---|---|---|
| 24 MHz | 5.0 µs | ~6.5 µs | ~21% |
| 20 MHz | 6.0 µs | ~7.5 µs | ~24% |

- **Shared bus, two IMUs:** about 15 µs of the 31.25 µs period. This works, but leaves little slack for ISR jitter.

### Option B: FIFO batching (recommended for robustness)

- **FIFO packet:** Packet 3 (accel + gyro + temp + timestamp) = 16 bytes. Use Packet 4 (20 bytes) if you enable 20-bit high-resolution mode.
- **Watermark:** 8 packets = 128 bytes, which triggers an interrupt every 250 µs (4 kHz IRQ rate per IMU).
- **Transfer per read:** 1 address byte + 2-byte `FIFO_COUNT` + 128 bytes.
  - At 20 MHz this takes about 52 µs per 250 µs, roughly 21% utilization.
- **Buffer headroom:** the FIFO is 2 KB, about 128 packets or 4 ms of buffering. This gives a large cushion against host latency.

## 4. IMU Register Configuration

Apply this to both IMUs, in Bank 0 unless noted.

```
DEVICE_CONFIG  (0x11) = 0x01        // soft reset, wait ≥1 ms
INTF_CONFIG0   (0x4C) : FIFO_COUNT_REC = records (optional), big-endian default
INTF_CONFIG1   (0x4D) : default clock select (PLL)
PWR_MGMT0      (0x4E) = 0x0F        // gyro + accel Low-Noise mode (required for 32 kHz)
                                     // wait ≥200 µs before further writes, ~45 ms for gyro settle
GYRO_CONFIG0   (0x4F) = (FS_SEL<<5) | 0x01   // ODR = 32 kHz
ACCEL_CONFIG0  (0x50) = (FS_SEL<<5) | 0x01   // ODR = 32 kHz
GYRO_ACCEL_CONFIG0 (0x52)          // UI filter BW, e.g., ODR/4 or low-latency option
INT_CONFIG     (0x14) = 0x03        // INT1 push-pull, active high, pulsed
INT_CONFIG1    (0x64) : INT_TPULSE_DURATION=1 (bit6, 8 µs pulse),
                        INT_TDEASSERT_DISABLE=1 (bit5)
                        // the datasheet requires these for ODR ≥ 4 kHz
```

**Option A (data-ready):**
```
INT_SOURCE0    (0x65) = 0x08        // UI_DRDY → INT1
```

**Option B (FIFO):**
```
FIFO_CONFIG    (0x16) = 0x40        // stream-to-FIFO
FIFO_CONFIG1   (0x5F) = 0x07        // accel + gyro + temp (+ TMST bit3 if desired)
FIFO_CONFIG2/3 (0x60/0x61) = 128    // watermark (bytes or records per INTF_CONFIG0)
INT_SOURCE0    (0x65) = 0x04        // FIFO_THS → INT1
```

**Anti-alias filters (Bank 1 and Bank 2):**
- Configure the gyro and accel AAF (`GYRO_CONFIG_STATIC3–5`, `ACCEL_CONFIG_STATIC2–4`) for your desired bandwidth.
- The defaults are tuned for lower ODRs.

**Clock sync for two IMUs:** to time-align them, feed a common external clock to CLKIN on pin 9 of both parts. Set `INTF_CONFIG1.RTC_MODE=1` and select the pin 9 function in Bank 1 `INTF_CONFIG5`. Without this, each part's internal oscillator drifts independently (about ±1–2%).

## 5. DMA Configuration (STM32-style, MCU-agnostic concept)

Per IMU:

| Item | Setting |
|---|---|
| Trigger | EXTI on INT1 rising edge |
| DMA streams | One TX stream and one RX stream, both byte width, memory-increment on |
| TX buffer | `[0x80 \| reg, 0x00 × N]`. Use a dummy fill, or TX-from-constant with memory-increment off for the tail. |
| RX buffer | Ping-pong (double buffer, DMA `DBM` mode) so the CPU parses buffer A while DMA fills buffer B |
| Priority | RX = Very High, TX = High. Arm RX before TX to avoid overrun. |
| Completion | RX transfer-complete IRQ, then deassert CS and swap buffers |