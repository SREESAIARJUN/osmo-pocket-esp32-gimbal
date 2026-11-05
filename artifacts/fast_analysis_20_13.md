# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Caveats First

1. **"Cycle 20"** is not a term from the ICM-42688-P datasheet or TDK's documentation that I can identify. It may be internal to your project, a vendor reference design, or a different part. If you tell me what it refers to, I can adjust the design.
2. **APEX and 32 kHz ODR likely conflict.** The APEX/DMP features (pedometer, tilt, tap, SMD, wake-on-motion) run at low DMP rates (tens to hundreds of Hz) and have specific accelerometer ODR and power-mode requirements. They are designed for low-power motion detection, not for a 32 kHz low-noise data path. I don't believe you can run APEX alongside 32 kHz accel output in the configuration you probably want.
   - Before committing hardware, check the APEX section of the current datasheet (DS-000347) and TDK's eMD driver (`inv_imu_apex.c`).
   - A common workaround is to switch modes: run APEX for wake/low-power, then reconfigure to 32 kHz low-noise for active capture.
3. The timing values below come from my knowledge of the datasheet, not a fresh lookup. **Verify every number against the current revision.**

---

## 1. Bus Architecture

**Recommended: one SPI peripheral per IMU.**

| Option | Pros | Cons |
|---|---|---|
| Separate SPI buses (recommended) | Parallel DMA, no contention, deterministic latency, independent fault isolation | Uses 2 SPI peripherals |
| Shared bus, 2 CS lines | Fewer pins | Serialized transfers, arbitration logic, the second IMU's timestamp jitters |

---

## 2. SPI Electrical Timing (verify against datasheet)

| Parameter | Value |
|---|---|
| Max SCLK | 24 MHz |
| Mode | 0 or 3 (CPOL=CPHA=0 or 1), MSB first |
| Read flag | Address bit 7 = 1 (`addr \| 0x80`) |
| CS setup (tCSS) | ~39 ns min |
| CS hold (tCSH) | ~18 ns min |
| SCLK high/low | ~17 ns min |
| Data setup / hold | ~13 ns / ~8 ns |
| Burst reads | Auto-increment supported |

**Practical guidance:**
- Run at 20–24 MHz only with short traces (<5 cm) and series termination (22–33 Ω at the driver).
- 12–16 MHz is often more robust and still has ample margin (see budget below).
- Disable I²C so it cannot misinterpret SPI traffic: `INTF_CONFIG0.UI_SIFS_CFG = 0b11`.

---

## 3. Bandwidth Budget at 32 kHz

The sample period is **31.25 µs**.

### Option A: Direct register burst (per data-ready interrupt)

Read `TEMP_DATA1 (0x1D)` through `GYRO_DATA_Z0 (0x2A)`: 1 address byte + 14 data bytes = 15 bytes = 120 bits.

| SCLK | Transfer time | + CS/ISR overhead (~1–2 µs) | Utilization per bus |
|---|---|---|---|
| 24 MHz | 5.0 µs | ~7 µs | ~22% |
| 16 MHz | 7.5 µs | ~9.5 µs | ~30% |
| 12 MHz | 10 µs | ~12 µs | ~38% |

On a shared bus, double these figures.

**Problem:** this approach generates 64,000 interrupts per second total. The ISR/DMA-start latency then dominates and is fragile.

### Option B: FIFO with watermark (recommended)

- FIFO packet 3 (accel + gyro + temp + timestamp) is 16 bytes. Packet 4 (20-bit high-resolution) is 20 bytes.
- Watermark at 16 packets gives an interrupt rate of 2 kHz per IMU.
- Each read is 2 bytes of `FIFO_COUNT` + (1 + 16×16 = 257 bytes) of data. At 24 MHz this takes about 86 µs per 500 µs window, roughly 17% utilization.
- The FIFO holds 2 KB, about 128 packet-3 frames, which is about 4 ms of buffer at 32 kHz. Do not set the watermark above about 50% of that.
- The timestamps preserve exact sample timing regardless of read jitter.

---

## 4. IMU Register Configuration (32 kHz, low-noise)

```c
// Bank 0
write(DEVICE_CONFIG, 0x01);          // soft reset, wait >= 1 ms
write(INTF_CONFIG0, 0x30 | 0x03);    // FIFO count in records, big-endian, I2C disabled
write(PWR_MGMT0, 0x0F);              // gyro + accel low-noise mode (required for 32 kHz)
delay_us(200);                       // gyro startup: no writes during this window
write(GYRO_CONFIG0,  (FS_2000DPS<<5) | 0x01);  // ODR 32 kHz
write(ACCEL_CONFIG0, (FS_16G<<5)     | 0x01);  // ODR 32 kHz
// Anti-alias filter: Bank 1 (gyro) / Bank 2 (accel) AAF_DELT/DELTSQR/BITSHIFT.
// Set the bandwidth appropriate to 32 kHz per the datasheet AAF table.

// FIFO setup
write(FIFO_CONFIG, 0x40);            // stream-to-FIFO
write(FIFO_CONFIG1, 0x07);           // accel+gyro+temp (+0x08 for TMST_FSYNC)
write(FIFO_CONFIG2, WM_LO);          // watermark
write(FIFO_CONFIG3, WM_HI);
write(INT_CONFIG,  0x03);            // INT1 push-pull, active high, pulsed
write(INT_SOURCE0, 0x04);            // FIFO threshold -> INT1
// Optional: external 32 kHz CLKIN on pin 9 for drift-matched timing across both IMUs
```

To synchronize the two IMUs, feed both the same CLKIN (RTC mode) and/or use FSYNC. Otherwise their internal oscillators drift (on the order of ±1–2%), and samples walk relative to each other.

---

## 5. DMA Configuration (STM32H7 example)

### Per IMU
- 1 SPI peripheral (e.g. SPI1 → IMU0, SPI4 → IMU1)
- 2 DMA streams (TX + RX), RX priority ≥ TX
- INT1 → EXTI → trigger the read state machine

```c
// Buffers: must be DMA-accessible and cache-safe on H7
__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t tx_buf[2][288];
__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t rx_buf[2][288];   // ping-pong

// SPI
hspi.Init.Mode              = SPI_MODE_MASTER;
hspi.Init.Direction         = SPI_DIRECTION_2LINES;
hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
hspi.Init.CLKPolarity       = SPI_POLARITY_HIGH;   // Mode 3
hspi.Init.CLKPhase          = SPI_PHASE_2EDGE;
hspi.Init.NSS               = SPI_N