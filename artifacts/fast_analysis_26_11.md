# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Design

## Issues to resolve first

**1. "Cycle 26" doesn't match anything I know.** It isn't a TDK/InvenSense document revision, silicon revision, or APEX feature name I recognize. If it's an internal project milestone, a vendor app note, or a specific firmware/SDK release, please share it. I won't invent parameters to match it.

**2. APEX and a 32 kHz accel ODR are incompatible.** The APEX engine (pedometer, tilt, tap, raise-to-wake, WoM) runs on the on-chip DMP at low rates (DMP_ODR of 25, 50, 100, or 500 Hz). Its algorithms require specific low accel ODRs: tap needs roughly 200 Hz to 1 kHz, and pedometer and tilt need about 50 Hz. You realistically have three options:
- **A.** Run the gyro at 32 kHz with the accel at an APEX-compatible ODR, if your APEX features tolerate it. Verify this against the APEX section of the datasheet.
- **B.** Run both sensors at 32 kHz and compute motion features on the host.
- **C.** Run one IMU at 32 kHz for high-rate data and the other at a low ODR with APEX.

The rest of this design assumes **B or C**. The SPI and DMA design is the same in all cases.

**3. The host MCU isn't specified.** The examples below use STM32H7/G4-style terminology. The structure ports directly to other MCUs.

---

## 1. SPI electrical and protocol settings

| Parameter | Value |
|---|---|
| Mode | 0 (CPOL=0, CPHA=0) or 3. Mode 3 is common. |
| Max SCLK | 24 MHz. Use about 20 MHz for margin on long traces. |
| Bit order | MSB first |
| R/W bit | Address bit 7 = 1 for read, 0 for write |
| Word size | 8-bit |
| Burst reads | Auto-increment. FIFO_DATA (0x30) does not increment. |
| CS | One per IMU, separate GPIOs, shared SCK/MOSI/MISO |

**Datasheet-critical timings.** Take these values from the SPI timing table in your datasheet revision rather than from memory:
- CS setup and hold relative to SCLK
- CS high time between transactions
- MOSI setup/hold and MISO output delay. MISO delay limits the speed at which sampling still works at 24 MHz.

For safety, insert at least 100 ns of CS-high time between the two devices.

**Signal integrity:**
- Add 22–33 Ω series termination on SCK and MOSI at the MCU, and on MISO at each IMU.
- Keep the bus short. If one IMU is at the end of a long trace, consider giving each IMU its own SPI peripheral. This also allows truly parallel DMA.
- Disable I²C on each part by setting INTF_CONFIG0/I3C bits appropriately, so stray glitches can't flip it into I²C mode.

---

## 2. Timing budget at 32 kHz

The sample period is **31.25 µs**.

### Option 1: Direct register burst (not recommended at 32 kHz)

The burst reads from 0x1D (TEMP_DATA1) through 0x2A (GYRO_DATA_Z0): 1 address byte + 14 data bytes = 15 bytes = 120 bits.

| SCLK | Per IMU | Both IMUs plus CS overhead | Bus utilization |
|---|---|---|---|
| 24 MHz | 5.0 µs | about 11 µs | about 35% |
| 20 MHz | 6.0 µs | about 13 µs | about 42% |

This works, but it means 64,000 DRDY interrupts per second. Any interrupt latency over about 30 µs loses samples, and timing jitter goes directly into the data.

### Option 2: FIFO with watermark batching (recommended)

Use FIFO packet 3: accel + gyro + temp + timestamp = 16 bytes per sample. Packet 4 (high-resolution, 20-bit) is 20 bytes. The tables below assume 16 bytes.

The data rate is 32,000 samples/s × 16 B = **512 kB/s per IMU**, or **1.024 MB/s for both**. At 20 MHz SCLK (2.5 MB/s raw), that is about 41% bus utilization before overhead.

| Watermark | Batch size | Interrupt rate | Read time per IMU at 20 MHz |
|---|---|---|---|
| 16 samples | 256 B | 2 kHz | about 103 µs |
| 32 samples | 512 B | 1 kHz | about 206 µs |

A **32-sample watermark (1 ms)** is a good default. FIFO capacity is about 2 KB, so 32 samples leaves headroom of more than 3 ms for latency.

---

## 3. Register configuration (Bank 0, per IMU)

```c
// Reset and identify
REG_BANK_SEL   (0x76) = 0x00;
DEVICE_CONFIG  (0x11) = 0x01;      // soft reset; wait ≥1 ms
WHO_AM_I       (0x75) -> expect 0x47

// Interface
INTF_CONFIG0   (0x4C): FIFO count in records, big-endian (check bit definitions)
INT_CONFIG     (0x14) = 0x03;      // INT1 push-pull, active high, pulsed

// Sensors: 32 kHz ODR code = 0x01
GYRO_CONFIG0   (0x4F) = (FS_SEL << 5) | 0x01;   // e.g., ±2000 dps
ACCEL_CONFIG0  (0x50) = (FS_SEL << 5) | 0x01;   // e.g., ±16 g
// Set the AAF/UI filter in Bank 1/2 for your bandwidth.
// The defaults are not tuned for 32 kHz.

// FIFO
FIFO_CONFIG    (0x16) = 0x40;      // stream-to-FIFO mode
FIFO_CONFIG1   (0x5F): enable accel + gyro + temp + timestamp (packet 3)
FIFO_CONFIG2/3 (0x60/0x61) = watermark (e.g., 32 records)
INT_SOURCE0    (0x65): route FIFO_THS to INT1

// Power on
PWR_MGMT0      (0x4E) = 0x0F;      // gyro + accel in low-noise mode
// Wait ≥45 ms for gyro startup; discard initial samples
```

Verify every bit field against your datasheet revision. Some field positions, such as the FIFO count format in INTF_CONFIG0, are easy to get wrong.

---

## 4. DMA architecture

```
IMU_A INT1 ──► EXTI ─┐
                     ├─► Scheduler (ISR) ─► SPI TX/RX DMA chain
IMU_B INT1 ──► EXTI ─┘                         │
                                               ▼
                          Double-buffered RX (ping/pong per IMU)
                                               │
                                               ▼
                           DMA TC ISR ─► release CS ─► next job / notify task
```

### Transaction sequence per watermark interrupt

1. **Read the FIFO count.** Assert CS, send `0x2E | 0x80` plus 2 dummy bytes, then deassert CS. This is short, so it can be polled or use a small DMA transfer.
2. **Read the FIFO data.** Assert CS, then send `0x30 | 0x80` plus N×16 dummy bytes over full-duplex DMA, and deassert CS on transfer complete.
3. Mark the buffer ready and swap ping/pong.

### DMA channel setup (STM32-style)

| Stream | Direction | Mode | Notes |
|---|---|---|---|
| SPI_RX | Peripheral → memory | Normal, memory increment, byte | Highest priority. Set up RX before TX. |
|