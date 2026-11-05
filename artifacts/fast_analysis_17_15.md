# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Issues to resolve before implementation

**1. 32 kHz ODR and APEX are not compatible on the same device.**
APEX (pedometer, tilt, tap, raise-to-wake, SMD) runs on the on-chip DMP at low rates, set by `DMP_ODR` = 25/50/100/500 Hz. It expects the accelerometer at a correspondingly low ODR, typically in low-power mode. 32 kHz requires low-noise mode for both sensors. Most APEX functions are not designed to run with the accelerometer at 32 kHz. Check the APEX section of the datasheet and TDK's AN for your specific function. Practical options:
- **Split roles:** IMU A runs at 32 kHz for high-rate data. IMU B runs at ≤1 kHz with APEX enabled.
- **Host-side detection:** Run both at 32 kHz and do motion detection on the host. Wake-on-Motion threshold interrupts still work at high ODR.

**2. "Cycle 17" is not a term I recognize.** It doesn't correspond to anything I know in TDK documentation. It may be internal to your project, a vendor app note, or a firmware release. If you share what it refers to, I can adapt this. Everything below is based on the public datasheet (DS-000347).

**3. Timing values below are typical datasheet figures.** Verify them against your datasheet revision before committing to hardware.

---

## 1. SPI bus timing (per device)

| Parameter | Symbol | Value |
|---|---|---|
| SCLK frequency | f_SCLK | ≤ 24 MHz (use 20–24 MHz with good layout; 10 MHz for register config at boot) |
| SPI mode | — | Mode 0 or Mode 3 (CPOL=CPHA=0 or 1) |
| SCLK low / high | t_LOW / t_HIGH | ≥ ~17 ns |
| CS setup | t_CSS | ≥ ~39 ns |
| CS hold | t_CSH | ≥ ~18 ns |
| SDI setup / hold | t_SU / t_HD | ~13 ns / ~8 ns |
| SDO valid | t_VD | ~21.5 ns max (Cload 30 pF) |
| CS high between transactions | t_CSD | ≥ ~ tens of ns (insert ≥100 ns for margin) |
| Read bit | — | MSB of address byte = 1 |

**Topology:** Use separate SPI peripherals, one per IMU, so both can be serviced in parallel with independent DMA streams. Sharing a bus works on bandwidth (see budget below), but it serializes reads and complicates latency.

**Signal integrity at 24 MHz:**
- Keep traces short (< ~5 cm).
- Use 22–33 Ω series termination on SCLK and MOSI near the MCU.
- Keep the ground return continuous.
- Set the AP-side drive strength via `DRIVE_CONFIG` (Bank 0, 0x13) if you see ringing.

**Interface setup:**
- `INTF_CONFIG0`: big-endian data (default). Set FIFO count in records or bytes as you prefer.
- `INTF_CONFIG1`: disable I²C on SPI-only designs to avoid accidental mode switching.

---

## 2. Sensor configuration for 32 kHz

```
PWR_MGMT0     (0x4E) = 0x0F   // Gyro LN + Accel LN (wait ≥200 µs after gyro on)
GYRO_CONFIG0  (0x4F) = FS | ODR=0x01   // 32 kHz
ACCEL_CONFIG0 (0x50) = FS | ODR=0x01   // 32 kHz
GYRO_CONFIG1 / ACCEL_CONFIG1 / GYRO_ACCEL_CONFIG0: set UI filter order/BW
Bank 1/2: configure AAF (anti-alias filter) for the 32 kHz output bandwidth
```

**Interrupt configuration (required for ODR ≥ 4 kHz):**
```
INT_CONFIG  (0x14): INT1 push-pull, active high, pulsed mode
INT_CONFIG1 (0x64): INT_TPULSE_DURATION = 1 (8 µs)
                    INT_TDEASSERT_DISABLE = 1
                    INT_ASYNC_RESET = 0
INT_SOURCE0 (0x65): UI_DRDY_INT1_EN  (direct mode)
              or    FIFO_THS_INT1_EN (FIFO mode, recommended)
```

**Synchronizing the two IMUs:** Feed a common 32.768 kHz clock to pin 9 (CLKIN) of both devices.
- `INTF_CONFIG5` (Bank 1): PIN9_FUNCTION = CLKIN
- `INTF_CONFIG1`: RTC_MODE = 1

This locks both ODRs to one clock, which removes inter-IMU drift. Optionally enable the FIFO timestamp (`TMST_CONFIG`) for alignment.

---

## 3. Bandwidth budget

| Mode | Bytes per transaction | Time at 24 MHz | Per-IMU load at 32 kHz |
|---|---|---|---|
| Direct register burst (0x1D–0x2A: temp + accel + gyro) | 1 addr + 14 = 15 B | ~5.0 µs + CS overhead | ~16–20% of the 31.25 µs period |
| FIFO packet 3 (accel + gyro + temp + timestamp) | 16 B/sample | — | 512 kB/s ≈ 4.1 Mbit/s |
| FIFO packet 4 (20-bit high-res) | 20 B/sample | — | 640 kB/s ≈ 5.1 Mbit/s |

**Recommendation: use FIFO mode with a watermark rather than per-sample DRDY.**
- An interrupt every 31.25 µs on two devices costs 64k ISRs/s, which is wasteful.
- The FIFO is 2 KB, about 128 packet-3 samples, or 4 ms at 32 kHz.
- Set the watermark to **32 packets (512 B, 1 ms)**. That gives ~1000 interrupts/s per IMU with ~3 ms of overflow margin.

---

## 4. DMA configuration (generic, STM32-style example)

Per IMU, use one SPI instance with dedicated RX and TX DMA streams:

```
SPIx:  Master, Mode 3, 8-bit, MSB first, prescaler → ≤24 MHz
       Software NSS (GPIO) or hardware NSS with proper t_CSS

DMA RX stream:  Periph→Mem, byte, mem-increment, NORMAL mode,
                priority VERY_HIGH, FIFO enabled (burst INCR4 if aligned)
DMA TX stream:  Mem→Periph, byte, mem-increment,
                priority HIGH
                (TX buffer = [0x30|0x80 (FIFO_DATA read), 0x00 × N])
Buffers:        Ping-pong (2 × 520 B) in DMA-accessible, non-cached RAM,
                or clean/invalidate D-cache around transfers on Cortex-M7
```

**Transaction sequence (on INT1 FIFO watermark):**
1. EXTI ISR: assert CS. Read `FIFO_COUNTH/L` (0x2E/0x2F) with a short 3-byte blocking transfer. Altern