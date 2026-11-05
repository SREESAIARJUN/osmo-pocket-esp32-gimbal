# Dual ICM-42688-P at 32 kHz: SPI and DMA Reference Design

**"Cycle 27":** I don't have context for this. If it's an internal project iteration, a spec revision, or a prior conversation, please share the constraints (MCU, bus topology, latency budget) and I'll tailor this. Below is a complete, MCU-agnostic design with STM32-style DMA examples.

**Important conflict: APEX vs. 32 kHz.** The APEX motion engine (pedometer, tilt, tap, raise-to-wake, significant motion) runs on the on-chip DMP at fixed rates of about 25, 50, 100 or 500 Hz. Its features expect specific accel ODRs, typically 50 Hz to 1 kHz. Tap is specified for roughly 200 Hz to 1 kHz. **APEX is not designed to operate with the accelerometer at 32 kHz.** Workable options:

1. **Gyro at 32 kHz, accel at 1 kHz with APEX enabled.** This is the recommended option if you need APEX.
2. **Both sensors at 32 kHz, APEX disabled.** Implement motion detection in host firmware instead.
3. **Mode switching.** Use 32 kHz during active capture and a low-rate APEX configuration when idle.

Verify the exact APEX/ODR constraints against your datasheet revision (DS-000347) and the TDK APEX application note. The rest of this design assumes option 1 or 2.

---

## 1. Bandwidth Budget

| Item | Value |
|---|---|
| ODR | 32 kHz → 31.25 µs period |
| FIFO packet (accel + gyro + temp + timestamp, 16-bit) | 16 bytes |
| FIFO packet (high-res, 20-bit) | 20 bytes |
| Per IMU, 16-byte packets | 512 kB/s ≈ 4.1 Mbit/s |
| Both IMUs | ≈ 8.2 Mbit/s payload |
| SPI clock | 24 MHz max (datasheet limit) |

**Topology:**
- **Recommended:** one SPI peripheral per IMU. This gives parallel DMA, no arbitration, and deterministic timing.
- **Acceptable:** a shared bus with two CS lines. At 24 MHz this is about 35% bus utilization, but you must stagger the reads and handle contention.

## 2. SPI Electrical and Timing

| Parameter | Setting / Requirement |
|---|---|
| Mode | Mode 0 (CPOL=0, CPHA=0) or Mode 3; device supports both |
| SCLK | 24 MHz max. Start at 12 MHz for bring-up, then step up. |
| Bit order | MSB first, 8-bit frames |
| Read/Write | Bit 7 of the address byte: 1 = read, 0 = write |
| CS setup / hold | Tens of ns. Use ≥ 100 ns for margin. Check the datasheet's SPI timing table for your revision. |
| CS high between transactions | ≥ 100 ns recommended |
| Burst reads | Address auto-increments; FIFO_DATA does not increment (it streams) |
| Signal integrity | Short traces (< 5 cm at 24 MHz), 22–33 Ω series resistor on SCLK/MOSI near the MCU, solid ground reference. Use separate CS per device, never shared. |
| Interface lock | Set I²C disable (`INTF_CONFIG0`) and SPI-only mode early, so stray edges can't switch the interface |

**Transaction timing at 24 MHz:**
- Data-ready read (1 address byte + 14 data bytes = 15 bytes): about 5 µs plus CS overhead, roughly 6 µs per IMU.
- FIFO burst of 8 packets (1 + 128 bytes): about 43 µs.

## 3. Sampling Strategy

**Option A: Per-sample DRDY interrupts (32 kHz IRQ per IMU)**
- Lowest latency, but about 64k interrupts per second. This is only viable on fast MCUs (e.g. H7 or M7-class) with tight ISRs.

**Option B: FIFO watermark with DMA burst (recommended)**
- Set the watermark to N packets. For N = 8, you get a 4 kHz interrupt per IMU and 250 µs of latency.
- Read FIFO_COUNT (2 bytes), then burst-read `count × 16` bytes from FIFO_DATA with DMA.
- The packet timestamp gives exact sample timing regardless of ISR jitter.

**Clock sync for a dual IMU (strongly recommended):**
- Feed a shared external 32.768 kHz clock into both IMUs' CLKIN pin (pin 9, RTC mode).
- This locks both ODRs to a common reference, so the sensors don't drift apart.
- Without it, each IMU's internal oscillator (±1–2%) will slowly slip samples relative to the other.

## 4. Register Configuration (per IMU)

Bank 0 unless noted. Verify addresses against your datasheet revision.

```
// Reset & identify
REG_BANK_SEL    (0x76) = 0x00
DEVICE_CONFIG   (0x11) = 0x01        // soft reset, wait ≥ 1 ms
WHO_AM_I        (0x75) → expect 0x47

// Interface
INTF_CONFIG0    (0x4C) = 0x30 | ...  // FIFO count in records, big-endian; disable I2C per datasheet bits
INTF_CONFIG1    (0x4D) |= 0x04       // RTC_MODE = 1 (if using external CLKIN)
// Bank 1: INTF_CONFIG5 (0x7B) PIN9_FUNCTION = CLKIN

// Sensors – Low-Noise mode
GYRO_CONFIG0    (0x4F) = (FS_SEL<<5) | 0x01   // ODR 32 kHz; FS e.g. 0x00 = ±2000 dps
ACCEL_CONFIG0   (0x50) = (FS_SEL<<5) | 0x01   // 32 kHz (option 2), or 0x06 = 1 kHz (option 1 / APEX)
GYRO_ACCEL_CONFIG0 (0x52)                     // UI filter BW, set for your application
PWR_MGMT0       (0x4E) = 0x0F                 // gyro LN + accel LN; wait ≥ 200 µs, no writes during that time

// FIFO
FIFO_CONFIG     (0x16) = 0x40        // stream-to-FIFO
FIFO_CONFIG1    (0x5F) = 0x0F | TMST // accel, gyro, temp, timestamp; add HIRES bit for 20-byte packets
FIFO_CONFIG2/3  (0