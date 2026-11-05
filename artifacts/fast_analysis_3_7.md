# Dual ICM-42688-P: SPI Timing and DMA Configuration at 32 kHz with APEX

## Two Problems with the Request

**1. "Cycle 3" is not a TDK term.** There is no "APEX Cycle 3" mode in the ICM-42688-P datasheet. It may come from your project, a driver, or a different part. If you tell me what it refers to, I'll adapt this. I assume you mean standard APEX features (tap, pedometer, tilt, raise-to-wake, significant motion, wake-on-motion).

**2. APEX cannot run at 32 kHz.** The DMP runs at 25, 50, or 100 Hz and consumes accelerometer data at no more than about 1 kHz. Tap detection needs accel ODR of 200 Hz (low-power mode) or 500 Hz/1 kHz (low-noise mode). Gyro and accel ODRs are independent, so the workable setup is:

| Sensor | ODR | Mode | Purpose |
|---|---|---|---|
| Gyro | 32 kHz | Low-noise | High-rate control or estimation |
| Accel | 1 kHz | Low-noise | APEX input plus your own use |
| DMP | 50 Hz | — | APEX algorithms |

Verify all register bit fields and timing values below against the current DS-000347 datasheet revision. I've flagged values I'm less certain of.

---

## 1. SPI Electrical Timing

| Parameter | Value | Notes |
|---|---|---|
| SPI mode | Mode 0 or 3 (CPOL=CPHA) | Mode 3 is common |
| f_SCLK max | 24 MHz | Use 20–24 MHz. Drop to ≤10 MHz if traces exceed ~10 cm or pass through connectors. |
| Bit order | MSB first | |
| Read/write flag | Bit 7 of address byte: 1 = read | |
| Address auto-increment | Yes, burst reads supported | |
| CS setup / hold | Tens of ns (see datasheet SPI timing table) | Insert ≥100 ns CS-high time between transactions |
| Interface lock | Set `I2C_SLEW`/`SPI_SLEW` and disable I²C (`INTF_CONFIG0`/`INTF_CONFIG6`) | Prevents accidental I²C mode |

Board notes:
- Use 22–33 Ω series termination on SCLK and MOSI at the MCU.
- Use separate CS lines with a pull-up on each.
- Put each IMU's INT1 on a separate EXTI-capable pin.

---

## 2. Bandwidth Budget

**Per IMU, gyro at 32 kHz.** The sample period is 31.25 µs.

| Read method | Bytes per transfer | Time at 24 MHz | Bus load |
|---|---|---|---|
| Direct registers (0x1D–0x2A: temp, accel, gyro) | 1 address + 14 data = 15 | ~5.0 µs + ~0.5 µs CS overhead | ~18% |
| FIFO packet 3 (16 B) | 16 B per sample | 512 kB/s ≈ 4.1 Mbit/s | ~17% |
| FIFO packet 4 (20 B, 20-bit high-res) | 20 B per sample | 640 kB/s ≈ 5.1 Mbit/s | ~21% |

**Two IMUs on one shared bus:** about 40% load. This is feasible, but jitter compounds.

**Recommended topology:** one SPI peripheral per IMU. This gives true parallel, deterministic DMA with no arbitration.

---

## 3. Read Strategy: Batched FIFO, Not Per-Sample IRQ

Taking 32k interrupts/s per IMU (64k total) wastes CPU. Instead, use FIFO watermark batching:

- Watermark = 16 packets, giving a 2 kHz interrupt rate per IMU and 0.5 ms latency.
- Each IRQ starts one DMA burst: 1 address byte (`FIFO_DATA` 0x30 | 0x80) plus 16 × 16 B = 257 bytes.
- 257 bytes at 24 MHz takes about 86 µs per 500 µs window.
- The 2 KB FIFO gives about 4 ms of headroom before overflow.

If you need minimum latency (for example, a 32 kHz control loop), use DRDY at 32 kHz with direct register reads chained entirely in DMA, with no CPU per sample (see §5b).

---

## 4. Register Configuration Sequence (Bank 0 unless noted)

```c
// --- Reset & identify ---
wr(0x76, 0x00);          // REG_BANK_SEL = 0
wr(0x11, 0x01);          // DEVICE_CONFIG: soft reset
delay_ms(2);
assert(rd(0x75) == 0x47);// WHO_AM_I

// --- Interface ---
wr(0x4C, 0x30);          // INTF_CONFIG0: FIFO count in records, big-endian, (verify bits)
wr(0x4D, 0x91);          // INTF_CONFIG1: default RTC off, PLL clock (verify)

// --- Interrupt pin ---
wr(0x14, 0x03);          // INT_CONFIG: INT1 push-pull, active high, pulsed
wr(0x64, 0x00);          // INT_CONFIG1: clear INT_ASYNC_RESET (bit4) - REQUIRED
                         //   for proper INT operation; set TPULSE 8µs for >4kHz ODR

// --- ODR / full-scale ---
wr(0x4F, 0x01);          // GYRO_CONFIG0: ±2000 dps, ODR = 32 kHz
wr(0x50, 0x06);          // ACCEL_CONFIG0: ±16 g, ODR = 1 kHz (APEX-compatible)
wr(0x52, 0x11);          // GYRO_ACCEL_CONFIG0: UI filter BW (tune for your loop)

// --- FIFO ---
wr(0x16, 0x40);          // FIFO_CON