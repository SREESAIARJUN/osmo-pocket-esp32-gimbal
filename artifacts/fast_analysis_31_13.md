# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

I don't know what "Cycle 31" refers to. It isn't a term from the ICM-42688-P datasheet, so it's probably your project's internal milestone or revision name. Below is a datasheet-grounded reference design. Map it onto your Cycle 31 requirements and check every value marked **[verify]** against the current TDK datasheet revision (DS-000347).

---

## 1. Important Caveat: APEX at 32 kHz ODR

APEX (pedometer, tilt, tap, raise-to-wake, significant motion) runs on the on-chip DMP at a low **DMP_ODR** of 25 or 50 Hz. Its constraints conflict with a 32 kHz configuration:

- **Tap detection** is specified only for accel ODR 200 Hz, 500 Hz or 1 kHz. It will **not** work at 32 kHz.
- **Pedometer, tilt and raise-to-wake** need accel ODR ≥ DMP_ODR. TDK documents and tests them at ODRs of 50 Hz to 1 kHz. Behaviour at 32 kHz is not guaranteed. **[verify / test]**
- **Wake-on-Motion (WoM)** is not an APEX/DMP feature. It works at any ODR.

Recommended architectures:

| Option | IMU A | IMU B | Notes |
|---|---|---|---|
| **A (recommended)** | 32 kHz, high-rate data | 32 kHz, high-rate data | Use WoM for motion events. Skip APEX. |
| B | 32 kHz, high-rate data | Accel 1 kHz with APEX, gyro 32 kHz or off | IMU B handles event detection. |
| C | Both 32 kHz | Both 32 kHz | Switch to a low-ODR/APEX mode when idle. Requires a reconfiguration sequence. |

---

## 2. Bandwidth Budget

| Item | Value |
|---|---|
| ODR (gyro + accel, Low-Noise mode) | 32 kHz, so 31.25 µs period |
| FIFO packet 3 (header, accel, gyro, temp, timestamp) | 16 bytes |
| FIFO packet 4 (20-bit high-resolution) | 20 bytes |
| Per-IMU data rate, packet 3 | 512 kB/s (4.10 Mbit/s) |
| Per-IMU data rate, packet 4 | 640 kB/s (5.12 Mbit/s) |
| Both IMUs, packet 3 | 1.024 MB/s (8.19 Mbit/s) |
| FIFO depth | 2 KB, about 128 packet-3 records, about 4.0 ms of buffering |

At 24 MHz SCLK, one IMU's 1 ms worth of data (32 packets = 512 bytes plus 1 address byte) takes about **171 µs**. That is about 17% bus utilisation per IMU. A shared bus at about 34% utilisation is feasible, but **separate SPI peripherals are strongly preferred**. They give you parallel DMA, independent timing, and no CS arbitration jitter.

---

## 3. SPI Electrical and Timing Parameters

Values are from the datasheet SPI timing table, at VDDIO 1.8 V with a 30 pF load. **[verify]**

| Parameter | Symbol | Min | Max |
|---|---|---|---|
| SCLK frequency | f_SCLK | — | 24 MHz |
| SCLK low / high period | t_LOW, t_HIGH | 17 ns | — |
| CS setup time | t_SU;CS | 39 ns | — |
| CS hold time | t_HD;CS | 18 ns | — |
| SDI setup time | t_SU;SDI | 13 ns | — |
| SDI hold time | t_HD;SDI | 8 ns | — |
| SDO valid after SCLK falling edge | t_VD;SDO | — | 21 ns |
| SDO output hold | t_HD;SDO | 3.5 ns | — |
| SDO disable after CS rises | t_DIS;SDO | — | 28 ns |

**Practical settings**

- **SPI mode:** 0 or 3 (CPOL = CPHA = 0 or 1), MSB first, 8-bit frames.
- **Clock:** 24 MHz gives a 20.8 ns half-period against a 21 ns t_VD;SDO, which leaves essentially zero margin. Options:
  - Use **20 to 21 MHz** for robust operation, or
  - Use 24 MHz only with short traces (under 5 cm), adequate drive strength, and MCU input-delay compensation (for example, STM32H7 `MasterSSIdleness` or sample-delay settings).
- **Address byte:** bit 7 = 1 for a read, 0 for a write. Burst reads auto-increment, except FIFO_DATA (0x30), which streams.
- **CS timing:** use hardware NSS or GPIO with at least 50 ns setup and hold. Allow at least 100 ns of CS-high time between transactions.
- **Disable I²C** so a glitch can't switch the interface: set INTF_CONFIG0 (0x4C) bits [1:0] `UI_SIFS_CFG = 0b11`.
- **Drive strength:** adjust DRIVE_CONFIG (0x13) `SPI_SLEW_RATE` to suit your trace length. Use faster slew for 24 MHz and slower slew to reduce EMI.

---

## 4. Register Configuration (Bank 0, per IMU)

```c
// Bank select: REG_BANK_SEL (0x76) = 0
write(0x11, 0x01);   // DEVICE_CONFIG: soft reset, then wait >= 1 ms
write(0x4C, 0x33);   // INTF_CONFIG0: big-endian, FIFO count in records [verify bit map], SPI only
write(0x4D, ...);    // INTF_CONFIG1: RTC_MODE if using external CLK