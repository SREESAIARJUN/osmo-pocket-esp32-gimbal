# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Issues to resolve first

**1. "Cycle 28"**
I don't recognize this as a datasheet term, silicon revision, or standard configuration name for the ICM-42688-P. If it refers to your internal project, a specific MCU, or a vendor app note, please tell me. Below I assume a generic Cortex-M7/M4-class MCU (STM32H7-style DMA).

**2. APEX and 32 kHz ODR conflict**
The APEX/DMP features (pedometer, tilt, tap, raise-to-wake, SMD) are designed around low accel ODRs. `DMP_ODR` in `APEX_CONFIG0` allows only 25, 50, 100, or 500 Hz. Tap detection is specified for accel ODRs of roughly 200 Hz to 1 kHz. Running accel at 32 kHz while expecting APEX outputs is not a documented configuration. Realistic options:

- **Option A (recommended):** Gyro at 32 kHz, accel at 1 kHz. Use APEX tap/WoM/tilt from accel and do any high-rate motion processing on the MCU.
- **Option B:** Use both sensors at 32 kHz and implement the motion detection in MCU firmware instead of APEX.
- **Option C:** Use APEX only in a separate low-rate mode and switch modes as needed.

Verify the exact APEX ODR constraints in your datasheet revision (DS-000347) and InvenSense's APEX app note.

## 1. SPI physical layer

| Parameter | Value | Notes |
|---|---|---|
| SPI mode | Mode 0 or Mode 3 | Mode 3 (CPOL=1, CPHA=1) is common |
| Max SCLK | 24 MHz | Use 20–24 MHz; 12 MHz is a safer margin on long traces |
| Bit order | MSB first | |
| Read flag | Address bit 7 = 1 | e.g., read 0x30 is sent as 0xB0 |
| Burst | Auto-increment | FIFO_DATA (0x30) does not increment and streams the FIFO |
| CS setup/hold | ~tens of ns | Check the SPI timing table; ≥100 ns of GPIO margin is safe |
| I²C | Disable it | `INTF_CONFIG0[1:0] = 0b11` prevents glitch misinterpretation |
| Slew | `DRIVE_CONFIG` (0x13) | Tune `SPI_SLEW_RATE` for your trace length and EMI |

**Bus topology:** Use one SPI peripheral per IMU. This allows parallel DMA, independent timing, and no CS arbitration. A single shared bus works, but reads must be serialized (budget below).

## 2. Data rate and timing budget

**Per IMU at 32 kHz (period 31.25 µs):**

| Read method | Bytes per sample | Bus time at 24 MHz |
|---|---|---|
| Direct register read (0x1D–0x2A: temp + accel + gyro) | 1 addr + 14 = 15 B | 5.0 µs |
| FIFO packet 3 (hdr + A + G + temp + tmst) | 16 B | 5.3 µs |
| FIFO packet 4 (20-bit hi-res) | 20 B | 6.7 µs |

Throughput is about 512 kB/s per IMU (packet 3), or about 1 MB/s for both. That is about 33% utilization of one 24 MHz bus if shared.

**Do not interrupt per sample.** Two IMUs at 32 kHz would generate 64k IRQs/s. Use FIFO watermark batching instead:

| Watermark | IRQ rate per IMU | Burst size | Burst time at 24 MHz |
|---|---|---|---|
| 8 packets | 4 kHz | 1 + 128 B | ~43 µs |
| 16 packets | 2 kHz | 1 + 256 B | ~86 µs |
| 32 packets | 1 kHz | 1 + 512 B | ~171 µs |

The FIFO is 2 KB, about 128 packet-3 entries, so keep the watermark ≤ ~50% for latency tolerance. 16 packets is a good default.

On a shared bus, the worst case is that IMU-B waits about 86 µs while IMU-A drains. That is fine with FIFO buffering.

## 3. Register initialization sequence (per IMU)

```
// Bank 0
0x76 REG_BANK_SEL   = 0x00
0x11 DEVICE_CONFIG  = 0x01        // soft reset; wait ≥1 ms
0x75 WHO_AM_I       → expect 0x47
0x4C INTF_CONFIG0   = 0x33        // big-endian, FIFO count in records, I2C disabled (bits[1:0]=11)
                                  // verify bit meanings for your use; COUNT_REC simplifies parsing
0x4D INTF_CONFIG1   : keep default clock (PLL auto-select)

// Sensor config
0x4F GYRO_CONFIG0   = 0x01        // FS=±2000 dps (000), ODR=32 kHz (0001)
0x50 ACCEL_CONFIG0  = 0x01        // FS=±16 g, ODR=32 kHz   (Option A: 0x06 = 1 kHz)
0x51 GYRO_CONFIG1   : filter order as needed
0x52 GYRO_ACCEL_CONFIG0 : UI filter BW (low-latency settings recommended at 32 kHz)

// FIFO
0x16 FIFO_CONFIG    = 0x40        // stream-to-FIFO
0x5F FIFO_CONFIG1   = 0x0F        // accel+gyro+temp+tmst (packet 3); add 0x10 for hi-res
0x60 FIFO_CONFIG2   = 0x10        // watermark low byte (16 records)
0x61 FIFO_CONFIG3   = 0x00        // watermark high
0x5F FIFO_CONFIG1  |= 0x20        // FIFO_WM_GT_TH

// Interrupt
0x14 INT_CONFIG     = 0x03        // INT1 push-pull, active-high, pulsed
0x63 INT_CONFIG0    : FIFO_THS clear-on-FIFO-read
0x64 INT_CONFIG1    : clear bit4 (INT_ASYNC_RESET) per datasheet note
0x65 INT_SOURCE0    = 0x04        // FIFO_THS_INT1_EN

// Power on (last)
0x4E PWR_MGMT0      = 0x0F        // gyro LN + accel LN
// wait ≥45 ms gyro startup (≥200 µs between PWR_MGMT0 write and other register writes)
```

**APEX (Option A, accel ≤1 kHz):**

```
0x56 APEX_CONFIG0   : DMP_ODR = 500 Hz (or 100/50/25), DMP_POWER_SAVE=0
0x4B SIGNAL_PATH_RESET = 0x40 (DMP_