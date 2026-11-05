# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Issues to resolve first

**1. APEX and 32 kHz ODR are very likely incompatible.** The APEX/DMP features (pedometer, tilt, tap, raise-to-wake, significant motion) run at DMP rates of 25, 50, 100 or 500 Hz. Tap detection expects accel ODR of 200 Hz to 1 kHz. I'm not aware of a supported configuration that runs APEX with accel at 32 kHz. Two workable options:
- **Option A:** Run both IMUs at 32 kHz for raw data. Implement motion detection (tap, step, etc.) in host firmware from the FIFO stream.
- **Option B:** Run one IMU at 32 kHz for raw data and the other at ≤1 kHz with APEX enabled.

Check this against the current datasheet (DS-000347) and InvenSense app notes before committing hardware.

**2. I don't know what "Cycle 9" refers to.** It may be an internal project iteration, a design-review milestone, or a spec revision. If it implies specific constraints (MCU, bus topology, latency budget), share them and I'll adapt the configuration. Below I assume a generic STM32H7-class MCU.

---

## 1. SPI Bus Timing

| Parameter | Value | Notes |
|---|---|---|
| SPI mode | Mode 0 or Mode 3 | Mode 3 (CPOL=1, CPHA=1) is common |
| f_SCLK max | 24 MHz | Use 20–24 MHz. Derate to about 10 MHz on long or flex traces |
| Bit order | MSB first | |
| Read flag | Bit 7 of address = 1 | Auto-increment for burst reads |
| CS setup (t_CSS) | ~39 ns min | Verify in datasheet SPI timing table |
| CS hold (t_CSH) | ~18 ns min | |
| SDI setup / hold | ~13 ns / ~8 ns | |
| SDO output valid | ~21.5 ns max | Limits usable SCLK with long traces |
| CS high between transactions | ≥ ~100 ns recommended | |

Treat the nanosecond figures as approximate and confirm them against the datasheet revision you are designing to.

**Signal integrity:**
- Put 22–33 Ω series resistors on SCLK and MOSI near the MCU.
- Keep traces under about 5 cm at 24 MHz.
- Fit a 2.2–10 kΩ pull-up on each CS line.

### Bandwidth budget (per IMU)

FIFO Packet 3 is accel + gyro + temp + timestamp = 16 bytes. The 20-bit high-resolution Packet 4 is 20 bytes.

| Item | Packet 3 (16 B) | Packet 4 (20 B) |
|---|---|---|
| Payload rate | 512 kB/s | 640 kB/s |
| Bits/s | 4.10 Mbit/s | 5.12 Mbit/s |
| Bus utilization @ 24 MHz | ~17% | ~21% |

**Topology recommendation:** use separate SPI peripherals per IMU (e.g., SPI1 and SPI4) so reads are parallel and jitter-free. A shared bus works (about 35–45% utilization) but adds arbitration latency.

### Read strategy

Do not interrupt at 32 kHz on data-ready. Use FIFO watermark interrupts instead:

- **Watermark:** 16 packets, which gives a 2 kHz interrupt rate and a 500 µs period.
- **Burst size:** 1 address byte + 2 count bytes (or a fixed-size read) + 256 data bytes.
- **Transfer time:** about 86 µs @ 24 MHz for 257 bytes, roughly 17% of the 500 µs window.

---

## 2. Register Initialization (per IMU, Bank 0)

```c
// Bank select: REG_BANK_SEL (0x76) = 0
write(0x76, 0x00);

write(0x11, 0x01);          // DEVICE_CONFIG: soft reset
delay_ms(2);
assert(read(0x75) == 0x47); // WHO_AM_I

write(0x4C, 0x30);          // INTF_CONFIG0: FIFO count in records, big-endian
write(0x4D, 0x00);          // INTF_CONFIG1: (optional) RTC/clock select per board
write(0x4E, 0x0F);          // PWR_MGMT0: gyro LN + accel LN (32 kHz requires LN)
delay_ms(1);                // >200 µs after gyro enable before further writes

write(0x4F, 0x01 | (0<<5)); // GYRO_CONFIG0: ODR=32 kHz, FS=±2000 dps
write(0x50, 0x01 | (0<<5)); // ACCEL_CONFIG0: ODR=32 kHz, FS=±16 g

write(0x16, 0x40);          // FIFO_CONFIG: Stream-to-FIFO mode
write(0x5F, 0x07);          // FIFO_CONFIG1: accel+gyro+temp (add 0x10 for hi-res,
                            //   0x08 for timestamp as configured)
write(0x60, 16);            // FIFO_CONFIG2: watermark low byte (records)
write(0x61, 0x00);          // FIFO_CONFIG3: watermark high byte

write(0x14, 0x03);          // INT_CONFIG: INT1 push-pull, active-high, pulsed
write(0x64, 0x00);          // INT_CONFIG1: clear INT_ASYNC_RESET (bit4) - required
write(0x65, 0x04);          // INT_SOURCE0: FIFO_THS -> INT1
```

**Bank 1/2 settings (optional):**
- AAF and notch filter tuning matters at 32 kHz. Set the gyro AAF in Bank 1 (0x0C–0x0E) and the accel AAF in Bank 2 (0x03–0x05) to match your bandwidth target.
- Return to Bank 0 when finished.

**Synchronization:**
- Feed both IMUs a common 32.768 kHz CLKIN on pin 9. Set INTF_CONFIG1 RTC_MODE and the pin-9 function in Bank 1.
- This keeps the two sample streams phase-locked and their timestamps comparable.

---

## 3. DMA Configuration (STM32H7 example)

| Item | IMU A | IMU B |
|---|---|---|
| SPI | SPI1 | SPI4 |
| RX DMA | DMA1 Stream0 (DMAMUX: SPI1_RX) | DMA1 Stream2 (SPI4_RX) |
| TX DMA | DMA1 Stream1 (SPI1_TX) | DMA1 Stream3 (SPI4_TX) |
| Priority | RX = Very High, TX = High | same |
| Width | Byte / Byte | Byte / Byte |
| Mode | Normal (re-armed per watermark) | Normal