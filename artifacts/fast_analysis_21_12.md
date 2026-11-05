# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Issues to resolve first

1. **"Cycle 21" is not a term I recognize.** It doesn't appear in TDK's ICM-42688-P documentation as far as I know. It may be an internal project name, a silicon revision, or a driver/SDK release. If it refers to a specific document or errata, tell me and I'll adjust.

2. **APEX and 32 kHz ODR probably conflict.** The APEX features (pedometer, tilt, raise-to-wake, tap, wake-on-motion) run on the on-chip DMP at a low DMP rate (25/50/100/500 Hz, set via `APEX_CONFIG0.DMP_ODR`). They also assume particular accelerometer ODR and power modes, typically low-power mode at low ODR. Running the accelerometer in low-noise mode at 32 kHz while also running pedometer, tilt, or tap is not a combination I can confirm is supported. Check the APEX section and register descriptions in your datasheet revision before committing. Practical options:
   - Use only **Wake-on-Motion** (threshold-based) alongside high-ODR streaming, if your datasheet permits it.
   - Compute motion features on the host from the 32 kHz stream.
   - Time-multiplex: switch to APEX mode at low ODR when idle, and to 32 kHz when active.

3. **Verify every number below against your datasheet revision.** Timing values and register addresses are from memory of DS-000347. Anything marked ⚠ is especially worth checking.

---

## 1. Bus architecture

**Recommended: one SPI peripheral per IMU, each with its own DMA streams.**

- This allows truly concurrent reads and isolates signal-integrity problems.
- If you must share one bus, use separate CS lines and serialize transactions. The bandwidth (below) still fits, but latency and jitter double.

**Synchronize both IMUs** by driving their **CLKIN** pin (pin 9, RTC mode, ~32.768 kHz, via `INTF_CONFIG1.RTC_MODE` ⚠) from the same MCU timer or crystal. Otherwise each IMU's internal oscillator drifts independently (roughly ±1% class), and the two sample streams slide relative to each other.

---

## 2. SPI electrical and timing parameters

| Parameter | Value | Notes |
|---|---|---|
| SPI mode | Mode 0 or Mode 3 | CPOL=CPHA=0 or 1 |
| SCLK max | 24 MHz | Run 12–20 MHz for margin on long traces |
| Bit order | MSB first | |
| R/W bit | Bit 7 of the address byte (1 = read) | |
| Address auto-increment | Yes, burst reads | |
| CS setup time | ~39 ns ⚠ | |
| CS hold time | ~18 ns ⚠ | |
| SDI setup / hold | ~13 / 8 ns ⚠ | |
| SDO valid after SCLK | ~18–22 ns ⚠ | Limits max clock with long traces |
| CS high between transactions | Leave ≥ 500 ns of margin | |
| I/O voltage | VDDIO 1.71–3.6 V | |

Also set `INTF_CONFIG0` to select SPI and disable I²C (`I2C_SLEW`/`UI_SIFS_CFG` fields ⚠). This prevents the part from misinterpreting SPI traffic as I²C.

---

## 3. Bandwidth budget (per IMU)

**Use the FIFO, not per-sample data-ready reads.** At 32 kHz the sample period is only 31.25 µs. That would mean 64,000 interrupts per second across two IMUs, which is wasteful and jitter-prone.

| FIFO packet type | Size | Contents | Data rate at 32 kHz |
|---|---|---|---|
| Packet 3 | 16 B | header, accel 6, gyro 6, temp 1, timestamp 2 | 512 kB/s (4.1 Mbit/s) |
| Packet 4 (20-bit hi-res) | 20 B | same, at higher resolution | 640 kB/s (5.1 Mbit/s) |

- **SPI load at 24 MHz:** about 17–21% per bus, plus framing overhead. This is comfortable.
- **FIFO depth:** 2 KB, which holds 128 × 16 B packets, or about **4 ms** of buffering.
- **Watermark choice:** 16 packets (256 B), giving an interrupt every **500 µs** (2 kHz per IMU).
  - This leaves 3.5 ms of slack before overflow.
  - A burst read of 256 B at 24 MHz takes about 85 µs on the wire.

---

## 4. IMU register configuration (bank 0 unless noted)

```c
#define REG_DEVICE_CONFIG   0x11
#define REG_INT_CONFIG      0x14
#define REG_FIFO_CONFIG     0x16
#define REG_FIFO_COUNTH     0x2E
#define REG_FIFO_DATA       0x30
#define REG_INTF_CONFIG0    0x4C
#define REG_PWR_MGMT0       0x4E
#define REG_GYRO_CONFIG0    0x4F
#define REG_ACCEL_CONFIG0   0x50
#define REG_FIFO_CONFIG1    0x5F
#define REG_FIFO_CONFIG2    0x60
#define REG_FIFO_CONFIG3    0x61
#define REG_INT_CONFIG0     0x63
#define REG_INT_SOURCE0     0x65
#define REG_WHO_AM_I        0x75   // expect 0x47
#define REG_BANK_SEL        0x76

void icm_init(imu_t *d) {
    wr(d, REG_BANK_SEL, 0);
    wr(d, REG_DEVICE_CONFIG, 0x01);          // soft reset
    delay_ms(2);
    if (rd(d, REG_WHO_AM_I) != 0x47) fault();

    wr(d, REG_INT_CONFIG, 0x03);             // INT1 push-pull, active high, pulsed ⚠ check polarity bits
    wr(d, REG_INTF_CONFIG0, 0x30);           // big-endian count+data, count in BYTES ⚠ verify bit map
    wr(d, REG_GYRO_CONFIG0, (0 << 5) | 0x01);  // ±2000 dps, ODR = 32 kHz
    wr(d, REG_ACCEL_C