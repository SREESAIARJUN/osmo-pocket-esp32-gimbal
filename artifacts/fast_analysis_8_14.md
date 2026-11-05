# Dual ICM-42688-P at 32 kHz: SPI and DMA Configuration

## Corrections to the Premise

**1. The ICM-42688-P does not have the APEX motion engine.** APEX (pedometer, tilt, tap, raise-to-wake) belongs to other TDK InvenSense parts in the family, such as the ICM-42688-V, ICM-42605, and ICM-42670-P. The -P variant offers Wake-on-Motion and Significant Motion Detection through its interrupt logic. Check the feature list in your datasheet revision before designing around APEX.

**2. "Cycle 8" isn't a configuration term I can find in TDK documentation.** If it comes from an internal spec, vendor app note, or another sensor, please share the source and I can map it.

**3. APEX and 32 kHz ODR are mutually exclusive in practice.** On parts that have APEX, the DMP runs at roughly 25–100 Hz and requires accel ODR in that range. A 32 kHz ODR requires Low-Noise mode on both accel and gyro, with no DMP features.

The configuration below targets what the ICM-42688-P actually supports: **32 kHz LN-mode accel and gyro, FIFO-based, two sensors, DMA-driven.**

---

## 1. Bandwidth Budget

| Item | Value |
|---|---|
| ODR (per IMU) | 32 kHz → 31.25 µs period |
| FIFO packet 3 (accel + gyro + temp + timestamp + header) | 16 bytes |
| Data rate per IMU | 512 KB/s ≈ 4.1 Mbit/s |
| Two IMUs | ≈ 8.2 Mbit/s payload |
| SPI SCLK max (datasheet) | 24 MHz |

**Direct register read per sample** (1 addr byte + 14 data bytes, TEMP_DATA1 0x1D → GYRO_DATA_Z0 0x2A):
- 120 bits at 24 MHz = 5.0 µs, plus CS overhead (about 0.5 µs) ≈ 5.5 µs.
- That is about 18% of the 31.25 µs window per IMU, or about 36% if both share one bus.

This works, but per-sample interrupts at 64 kHz combined are punishing on most MCUs. **Use FIFO plus watermark plus DMA bursts instead.**

---

## 2. SPI Bus Topology

**Recommended: one SPI peripheral per IMU.** This gives:
- independent DMA streams,
- no CS arbitration,
- parallel reads,
- easier signal integrity at 24 MHz.

**If you must share one bus,** use separate CS lines and serialize transactions in the DMA-complete ISR. At 24 MHz, total utilization stays under about 40% with FIFO bursts.

### SPI Settings

| Parameter | Setting |
|---|---|
| Mode | 0 (CPOL=0, CPHA=0) or 3 (CPOL=1, CPHA=1) |
| Bit order | MSB first |
| Frame | 8-bit |
| Read | Address byte with bit 7 = 1 |
| Write | Address byte with bit 7 = 0 |
| SCLK, configuration phase | ≤ 1 MHz (conservative, for init writes) |
| SCLK, streaming | Up to 24 MHz |
| Auto-increment | Native on burst reads; FIFO_DATA (0x30) does not increment and streams the FIFO |

### Timing Parameters

Confirm these against the "SPI Timing Characteristics" table in your datasheet revision. The exact ns values below are approximate.

- **fSCLK max:** 24 MHz (tSCLK ≈ 41.7 ns)
- **CS setup and hold:** tens of ns. Add at least 100 ns of guard in firmware to be safe.
- **SDI setup and hold:** on the order of 10 ns
- **SDO valid after SCLK edge:** on the order of 20 ns. Ensure your MCU input setup time fits within half a clock period at 24 MHz, or drop to about 16–20 MHz on long traces.
- **CS high between transactions:** at least about 100 ns recommended

### PCB Notes

- Keep SCLK, MOSI, and MISO short, under about 5 cm.
- Use 22–33 Ω series termination at the source on SCLK.
- Give each IMU a solid ground reference and local 0.1 µF + 2.2 µF decoupling on VDD and VDDIO.

---

## 3. Sensor Register Configuration (Both IMUs, Bank 0 Unless Noted)

| Step | Register | Value | Purpose |
|---|---|---|---|
| 1 | DEVICE_CONFIG (0x11) | 0x01 | Soft reset; wait ≥ 1 ms |
| 2 | WHO_AM_I (0x75) | expect 0x47 | Verify part |
| 3 | INTF_CONFIG0 (0x4C) | e.g. 0x30 | Big-endian sensor data, FIFO count in records or bytes per your parser; disable I2C if SPI-only (see INTF_CONFIG0/1 bits) |
| 4 | GYRO_CONFIG0 (0x4F) | FS_SEL \| ODR=0x1 | ODR code 0x1 = 32 kHz |
| 5 | ACCEL_CONFIG0 (0x50) | FS_SEL \| ODR=0x1 | 32 kHz |
| 6 | GYRO_CONFIG1 / ACCEL_CONFIG1, Bank 1/2 AAF registers | per app | Set UI filter order and anti-alias filter bandwidth appropriate for 32 kHz |
| 7 | FIFO_CONFIG1 (0x5F) | ACCEL_EN \| GYRO_EN \| TEMP_EN \| TMST_FSYNC_EN | Selects packet 3 (16 B) |
| 8 | FIFO_CONFIG2/3 (0x60/0x61) | watermark | E.g. 16 packets = 256 B, which gives 500 µs latency and 2 kHz IRQ rate |
| 9 | FIFO_CONFIG (0x16) | 0x40 | Stream-to-FIFO mode |
| 10 | INT_CONFIG (0x14) | push-pull, active high, pulsed | |
| 11 | INT_SOURCE0 (0x65) | FIFO_THS_INT1_EN | Watermark → INT1 |
| 12 | PWR_MGMT0 (0x4E) | 0x0F | Gyro LN + accel LN; wait ≥ 200 µs, ignore first ~30–45 ms of gyro data |

### Clock Sync (Optional)

If you need the two IMUs sample-aligned, enable CLKIN on pin 9:
- Feed a shared 32.768 kHz reference via INTF_CONFIG1 RTC_MODE and the Bank 1 pin config.
- Otherwise each IMU's internal oscillator drifts (about ±1%), so align using FIFO timestamps.

---

## 4. DMA Read Transaction (per IMU, per Watermark IRQ)

```
INT1 rising edge (EXTI)
 └─► Txn A: CS low, TX [0x2E|0x80, 0x00, 0x00]   ; FIFO_COUNTH/L
            RX → count (bytes or records)        ; CS high
 └─► Txn B: CS low, TX [0x30|0x80, dummy × N]    ; FIFO_DATA
            RX DMA → ping-pong buffer, N = count (cap at buffer size, multiple of 16)
            CS high on DMA TC
 └─► DMA TC ISR: swap buffer, signal processing task
```

An alternative is to skip Txn A: read a fixed N = watermark × 16 bytes, then validate each packet header. A header of 0xFF or an empty marker indicates an empty or invalid slot.

### DMA Stream Setup (STM32-Style, Adapt to Your MCU)

| Setting | RX stream | TX stream |
|---