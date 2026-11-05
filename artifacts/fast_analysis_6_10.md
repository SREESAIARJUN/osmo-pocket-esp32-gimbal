# Dual ICM-42688-P at 32 kHz ODR: SPI Timing and DMA Configuration

## Two Issues to Resolve First

**1. "Cycle 6"**
This isn't a term in TDK InvenSense documentation for the ICM-42688-P. It may be internal to your project, a vendor SDK, or a flight-stack release. Please tell me what it refers to so I can align the configuration. Everything below is based on the datasheet (DS-000347) and should be checked against your revision.

**2. APEX and 32 kHz conflict**
APEX features (pedometer, tilt, tap, raise-to-wake, SMD, WoM) run on the on-chip DMP. The DMP requires the accelerometer ODR to fall within its supported set:
- 25, 50, 100, or 500 Hz via `DMP_ODR` for most features
- 200 Hz, 500 Hz, or 1 kHz for tap

You cannot run the accelerometer at 32 kHz and use APEX on the same device at the same time. Realistic options:

| Option | Gyro ODR | Accel ODR | APEX |
|---|---|---|---|
| A. Split roles | IMU0: 32 kHz | IMU0: 32 kHz | IMU1 runs APEX at 50–500 Hz accel (gyro 32 kHz still possible) |
| B. Gyro-only high rate | 32 kHz | 500 Hz–1 kHz | Yes, on both |
| C. Host-side motion engine | 32 kHz | 32 kHz | No; implement tilt/tap/WoM in firmware |

The configuration below assumes **Option A** (most common for redundant flight control), with notes for B and C.

---

## 1. Bus Topology

**Recommended: one SPI peripheral per IMU.**
- This allows truly parallel DMA transfers with zero arbitration jitter.
- A shared bus works at 24 MHz (budget in §3) but serializes the reads and adds CS-switch latency.

```
MCU SPI1 ── IMU0 (CS0, INT1→EXTI_a)   32 kHz gyro+accel, FIFO
MCU SPI2 ── IMU1 (CS1, INT1→EXTI_b)   32 kHz gyro, APEX accel, INT2 = APEX events
Optional: shared 32.768 kHz CLKIN to both (pin 9) for drift-matched sampling
```

## 2. SPI Electrical and Timing Parameters

| Parameter | Value | Notes |
|---|---|---|
| Mode | 0 (CPOL=0, CPHA=0) or 3 | Both supported; pick one and keep it |
| Bit order | MSB first | |
| Max SCLK | 24 MHz | Use ≤1 MHz for register config at boot if signal integrity is unproven |
| Read/Write bit | Bit 7 of address byte (1 = read) | Auto-increment on burst |
| CS setup / hold | ~39 ns / ~18 ns (verify in datasheet table) | 1 SCLK of margin at 24 MHz is plenty |
| CS high between transactions | ≥ ~0.5 µs recommended | Not a datasheet limit; it is margin |
| Interface lockout | Set `INTF_CONFIG0` / `DRIVE_CONFIG` slew appropriately; disable I²C (`INTF_CONFIG0.UI_SIFS_CFG = 11b`) | Prevents I²C misinterpretation of SPI traffic |
| Trace length | Keep < ~10 cm at 24 MHz; series 22–33 Ω on SCLK | |

## 3. Bandwidth and Timing Budget (per IMU at 32 kHz)

Sample period: **31.25 µs**

**Option 1: Direct register burst** (0x1D–0x2A: TEMP 2 + ACCEL 6 + GYRO 6 = 14 bytes)
- 1 address + 14 data = 15 bytes = 120 bits
- At 24 MHz: **5.0 µs** of bus time, about 16% duty

**Option 2: FIFO batching** (recommended at 32 kHz)
- Packet 3 (accel + gyro + temp + timestamp, 16-bit) = 16 bytes per sample
- Packet 4 (high-res, 20-bit) = 20 bytes per sample
- Throughput: 32,000 × 16 B = 512 kB/s, about 4.1 Mbit/s per IMU
- With a watermark of 8 samples, read 2 + 128 bytes every 250 µs:
  - About 43 µs of bus time
  - About 4,000 interrupts/s instead of 32,000

**Shared-bus case** (if you must share): direct reads cost 2 × 5 µs + CS overhead ≈ 11 µs of 31.25 µs. This is workable, but FIFO batching is strongly preferred.

## 4. Register Configuration

Bank 0 unless noted. Select banks with `REG_BANK_SEL` (0x76).

```c
// --- Reset and interface ---
WR(0x76, 0x00);          // Bank 0
WR(0x11, 0x01);          // DEVICE_CONFIG: soft reset; wait >=1 ms
// WHO_AM_I (0x75) should read 0x47
WR(0x4C, 0x30 | 0x03);   // INTF_CONFIG0: FIFO count big-endian, count in records; disable I2C (SPI only)

// --- Optional external clock (CLKIN, pin 9) ---
WR(0x76, 0x01);
WR(0x7B, 0x04);          // INTF_CONFIG5 (bank1): PIN9_FUNCTION = CLKIN
WR(0x76, 0x00);
WR(0x4D, 0x95);          // INTF_CONFIG1: RTC_MODE=1, CLKSEL=01 (verify bits for your rev)

// --- ODR / full-scale ---
WR(0x4F, (0<<5) | 0x01); // GYRO_CONFIG0: ±2000 dps, ODR = 32 kHz
// IMU0:
WR(0x50, (0<<5) | 0x01); // ACCEL_CONFIG0: ±16 g, ODR = 32 kHz
// IMU1 (APEX): e.g. 500 Hz -> ODR code 0x0F; or 50 Hz -> 0x09
// WR(0x50, (0<<5) | 0x0F);

// --- Filters ---
// Configure gyro/accel AAF in bank 1 / bank 2 (GYRO_CONFIG_STATIC2..5, ACCEL_CONFIG_STATIC2..4)
// to match your control bandwidth. At 32 kHz, consider AAF ~ 2-4 kHz.

// --- Interrupt pin: mandatory settings for ODR >= 4 kHz ---
WR(0x14, 0x03);          // INT_CONFIG: INT1 push-pull, active high, pulsed
WR(0x64, 0x60);          // INT_CONFIG1: INT_TPULSE_DURATION=1 (8 us), INT_TDEASSERT_DISABLE=1
                         // Required by the datasheet for ODR >