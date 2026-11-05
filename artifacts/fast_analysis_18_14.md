# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Caveats

1. **"Cycle 18"**: I don't recognize this. It isn't an InvenSense/TDK designation I know of. It may be an internal project milestone, a vendor app note, or a firmware release. If it carries specific requirements such as a target MCU, latency budget, or bus topology, please share them. The configuration below is generic.
2. **APEX and 32 kHz are not compatible on the same device.** The APEX motion engine (pedometer, tilt, tap, raise-to-wake, SMD, WoM) runs off the DMP at 25, 50, 100, or 500 Hz (`DMP_ODR`), and most features expect accel ODR ≤ 1 kHz. Realistic options:
   - **A.** IMU1 at 32 kHz for raw data (control/vibration), IMU2 at ≤1 kHz with APEX enabled.
   - **B.** Both at 32 kHz, with the motion features implemented in MCU firmware.
   - **C.** Both at ≤1 kHz with APEX.

   The details below assume both IMUs run at 32 kHz raw. The APEX notes at the end apply only to whichever device runs at a compatible ODR.

---

## 1. SPI Bus Parameters

| Parameter | Value |
|---|---|
| Mode | SPI Mode 0 or 3 (CPOL=CPHA=0 or 1) |
| Max SCLK | 24 MHz (datasheet maximum) |
| Recommended SCLK | 20–24 MHz with short, matched traces. Drop to 10 MHz if signal integrity is marginal. |
| Bit order | MSB first, 8-bit frames |
| Read/write | Bit 7 of the address byte: 1 = read, 0 = write |
| Auto-increment | Yes, for burst reads |
| CS timing | Check the datasheet's SPI timing table for CS setup/hold and minimum CS-high time. In practice, insert ≥100 ns CS-high between transactions. |

**Topology:** Use **two separate SPI peripherals** if possible, with each IMU on its own bus and DMA streams. This gives true parallelism and no arbitration. A shared bus with two CS lines also works, but the reads must be serialized (see the budget below).

**Disable I²C** so it can't glitch the SPI interface: set `INTF_CONFIG0.UI_SIFS_CFG = 0b11`.

---

## 2. Timing Budget at 32 kHz

The sample period is **31.25 µs**.

### Option 1: Direct register read per data-ready interrupt
Burst-read 14 bytes from `TEMP_DATA1` (0x1D) through `GYRO_DATA_Z0` (0x2A): 1 address byte + 14 data bytes = 120 bits.

| SCLK | Transfer | +CS/DMA overhead (~1–2 µs) | Single bus, 2 IMUs |
|---|---|---|---|
| 24 MHz | 5.0 µs | ~6.5 µs | ~13 µs (42% of period) |
| 12 MHz | 10.0 µs | ~11.5 µs | ~23 µs (74%, tight) |

This approach works, but it generates 64k interrupts/s total. ISR jitter becomes critical, and you risk missed samples.

### Option 2: FIFO + watermark (recommended)
- Use FIFO packet type 3 (accel + gyro + temp + timestamp), 16 bytes per sample.
- Data rate: 32,000 × 16 = **512 KB/s per IMU**, so each SPI link carries about 4.1 Mbit/s, roughly 17% of 24 MHz.
- FIFO size is 2 KB, about 128 packets, which is 4 ms of buffer at 32 kHz.
- Set the watermark to **16 packets (256 B)**. The interrupt then fires every 500 µs (2 kHz).
- Each burst is 1 address byte + 256 bytes, about **86 µs at 24 MHz**. On a shared bus, both IMUs take about 175 µs per 500 µs window (35%).
- Read `FIFO_COUNTH/L` (0x2E/0x2F) first. A combined burst from 0x2D–0x2F followed by a separate FIFO burst is typical. Alternatively, enable `FIFO_COUNT_REC` and read fixed-size blocks.

---

## 3. Register Initialization (Bank 0)

```
DEVICE_CONFIG   (0x11) = 0x01     // soft reset, wait 1 ms
INTF_CONFIG0    (0x4C) : UI_SIFS_CFG = 11b (disable I2C), big-endian data
INTF_CONFIG1    (0x4D) : select clock (PLL auto)
GYRO_CONFIG0    (0x4F) = FS_SEL | ODR=0001b   // 32 kHz
ACCEL_CONFIG0   (0x50) = FS_SEL | ODR=0001b   // 32 kHz
GYRO_ACCEL_CONFIG0 (0x52)          // UI filter BW as needed (low-latency)
FIFO_CONFIG     (0x16) = 0x40      // Stream-to-FIFO
FIFO_CONFIG1    (0x5F) = accel+gyro+temp+TMST enabled (pkt 3), WM_GT_TH=1
FIFO_CONFIG2/3  (0x60/0x61) = watermark (e.g. 256 bytes)
INT_CONFIG      (0x14) = push-pull, active-high, pulsed
INT_CONFIG1     (0x64) : clear INT_ASYNC_RESET bit (required per datasheet)
INT_SOURCE0     (0x65) = FIFO_THS_INT1_EN   (or UI_DRDY for Option 1)
PWR_MGMT0       (0x4E) = 0x0F      // gyro+accel low-noise mode; wait ≥200 µs, no writes during
```

Verify the bitfields against the current datasheet revision before use.

**Synchronizing the two IMUs:** Drive both FSYNC pins from a common MCU timer, or feed a common external clock to `CLKIN` (pin 9 via `INTF_CONFIG5`) so the ODRs don't drift apart. Without this, the two 32 kHz streams will slip against each other by ppm-level amounts.

---

## 4. DMA Configuration (generic, STM32-style)

Configure one set per IMU.

| Item | Setting |
|---|---|
| TX stream | Memory→peripheral, 8-bit, memory increment ON. Source is a static buffer `{0x30 \| 0x80, 0x00…}`. |
| RX stream | Peripheral→memory, 8-bit, memory increment ON, **higher priority than TX** |
| Mode | Normal (re-armed per transaction) or circular with double-buffering |
| Buffers | Ping-pong, 2 × (watermark + margin), e.g. 2 × 320 B, 32-byte aligned |
| Cache (M7) | Place in non-cacheable memory (DTCM/MPU region), or invalidate before parsing |
| FIFO/burst | DMA FIFO enabled, threshold full, single bursts on the peripheral side |
| CS | GPIO-controlled: assert before enabling DMA, deassert in the RX-complete ISR |

**Sequence per watermark interrupt (INT1 EXTI):**
1. Assert CS and read `FIFO_COUNT` (3-byte transaction, can be polled since it's short).
2. Round down to a whole number of packets, then start the TX+RX DMA for `1 + N×16` bytes.
3. In the RX-complete ISR, deassert CS, swap the ping-pong buffer, and signal the processing task.
4. If the bus is shared, chain the second IMU's transfer from the first one's completion ISR.

**Priorities:** EXTI (INT1) > DMA RX complete > processing task.

---

## 5. Processing Notes
- Parse the FIFO header byte for each packet. Discard packets with header `0x80` (empty) or invalid-data markers (−32768).
- Use the 16-bit FIFO timestamp to align the two IMUs. At 32 kHz, the timestamp res