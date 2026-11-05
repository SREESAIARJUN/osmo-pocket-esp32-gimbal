# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## ⚠️ Two Problems With the Request

**1. APEX cannot run at a 32 kHz ODR.**
The APEX motion engine (pedometer, tilt, tap, raise-to-wake, SMD, WOM) runs on the on-chip DMP at fixed low rates. `APEX_CONFIG0.DMP_ODR` offers only 25, 50, 100 or 500 Hz. APEX also requires the accelerometer ODR to be configured at or near that rate, and typically in low-power mode. So you must choose one of these:
- **Option A:** 32 kHz raw gyro/accel streaming, with APEX disabled (the usual choice for flight control or vibration analysis).
- **Option B:** APEX enabled, with the accelerometer at ≤1 kHz. Gyro ODR can still be set independently, but verify this against the datasheet for your use case.
- **Option C:** Two different roles. For example, IMU #1 runs 32 kHz streaming and IMU #2 runs APEX at a low rate.

**2. "Cycle 5" is not a TDK term I recognize.**
I'm not aware of any ICM-42688-P mode, silicon revision or APEX feature called "Cycle 5." It may be internal terminology from your project. Please tell me what it refers to. I won't invent a configuration for it.

The rest of this answer covers **Option A**, plus notes for APEX on a second device.

---

## 1. SPI Electrical and Timing Parameters

These values are typical for the ICM-426xx family. **Verify each one against Table "SPI Timing Characteristics" in your datasheet revision.**

| Parameter | Value |
|---|---|
| SPI mode | Mode 0 or Mode 3 (CPOL=CPHA=0 or 1) |
| SCLK max | 24 MHz |
| CS setup (CS↓ to first SCLK edge) | ≥ ~39 ns |
| CS hold (last SCLK to CS↑) | ≥ ~18 ns |
| SDI setup / hold | ~13 ns / ~8 ns |
| SDO valid after SCLK | ≤ ~20 ns (sets the real max clock with long traces) |
| Read flag | Address bit 7 = 1 |
| Address auto-increment | Yes (burst reads) |

**Practical guidance:**
- Run at **20–24 MHz** only with short traces (<5 cm), matched lengths and a solid ground reference.
- Otherwise use 10–12 MHz. The budget below still works at that speed.
- Set `INTF_CONFIG0` (0x4C) so that big-endian data and FIFO count reporting match your parser.
- Disable I²C on the device if SPI-only (`INTF_CONFIG0` UI_SIFS_CFG bits). This avoids interface glitches.

---

## 2. Bandwidth Budget at 32 kHz

The sample period is **31.25 µs**.

### Direct register read (not recommended at 32 kHz)
- A burst from `TEMP_DATA1` (0x1D) is 1 address byte + 14 data bytes = 120 bits.
- At 24 MHz that takes **5.0 µs** per IMU. Adding CS and DMA overhead brings it to about 6 µs.
- Two IMUs take about 12 µs out of the 31.25 µs period. That is feasible, but it needs a **64 kHz combined interrupt rate**, with jitter and the risk of missed samples.

### FIFO batching (recommended)
FIFO packet sizes:
- **Packet 3** (accel + gyro + temp + timestamp): 16 bytes.
- **Packet 4** (20-bit hi-res): 20 bytes.

Example with a watermark of 8 packets:
- **Interrupt rate:** 4 kHz per IMU (every 250 µs).
- **Bytes per IMU per read:** 2 (FIFO count) + 1 + 8×16 = about 131 bytes. That takes about 44 µs at 24 MHz.
- **SPI load per IMU:** about 17.5%.
- **Two IMUs:**
  - Same bus: about 35% utilization. This is fine.
  - Separate buses: about 17.5% each. Use this if possible, since it allows truly parallel, deterministic reads.

**Separate SPI peripherals are strongly preferred** for dual IMUs at this rate.

---

## 3. Device Register Configuration (Option A, per IMU)

```c
// Bank 0 (REG_BANK_SEL 0x76 = 0)
write(0x11, 0x01);   // DEVICE_CONFIG: soft reset; wait >= 1 ms
read (0x75);         // WHO_AM_I: expect 0x47
write(0x4C, ...);    // INTF_CONFIG0: endian / FIFO count format, disable I2C
write(0x4D, ...);    // INTF_CONFIG1: clock select (PLL auto); see AFSR note below
write(0x4F, 0x01 | (FS_SEL << 5));   // GYRO_CONFIG0: ODR = 32 kHz (0x1)
write(0x50, 0x01 | (FS_SEL << 5));   // ACCEL_CONFIG0: ODR = 32 kHz (0x1)
write(0x16, 0x40);   // FIFO_CONFIG: stream-to-FIFO mode
write(0x5F, 0x0F);   // FIFO_CONFIG1: accel+gyro+temp+timestamp (verify bit map)
write(0x60, WM_L);   // FIFO_CONFIG2: watermark low  (bytes or records per INTF_CONFIG0)
write(0x61, WM_H);   // FIFO_CONFIG3: watermark high
write(0x14, 0x03);   // INT_CONFIG: INT1 push-pull, active high, pulsed
write(0x65, 0x04);   // INT_SOURCE0: FIFO threshold -> INT1
write(0x4E, 0x0F);   // PWR_MGMT0: gyro + accel low-noise mode; wait >= 200 µs before other writes
```

**Configuration notes:**
- **Anti-alias filter (AAF):** configure it in Bank 1 (gyro) and Bank 2 (accel) for your bandwidth.
- **Notch filter:** also in Bank 1.
- **AFSR:** the gyro auto full-scale-range switching can cause discontinuities. Many flight stacks (PX4, Betaflight) disable it via `INTF_CONFIG1`. Check current PX4 driver source for the exact bit values.
- **Time alignment:**
  - Feed both IMUs a shared **32.768 kHz CLKIN** on pin 9. Enable RTC mode in `INTF_CONFIG1` and select the CLKIN pin function in Bank 1.
  - This locks both ODRs to a common clock. Without it, the two devices will drift relative to each other by roughly a few hundred ppm.

---

## 4. DMA Configuration (STM32H7 example; the pattern applies to any MCU)

### Topology
- **IMU1:** SPI