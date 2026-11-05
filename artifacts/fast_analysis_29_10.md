# Dual ICM-42688-P at 32 kHz ODR: SPI Timing and DMA Configuration

## Caveats

1. **"Cycle 29"** isn't a term I recognize from the ICM-42688-P datasheet or TDK documentation. It may be an internal project milestone, a document revision, or a specific errata or app note. If it implies specific constraints (MCU, bus topology, latency budget), share them and I'll adapt this.
2. **APEX at 32 kHz is a likely conflict.** The APEX/DMP features (pedometer, tilt, raise-to-wake, tap, SMD) run at `DMP_ODR` of 25, 50 or 100 Hz and are intended for low-ODR/low-power accelerometer configurations. Wake-on-Motion is the most ODR-independent. Check the APEX section of your datasheet revision before assuming APEX runs alongside 32 kHz low-noise mode. Section 5 gives a fallback strategy.
3. Timing values below are from the datasheet as I recall them. **Verify them against your datasheet revision** before using them for sign-off.

---

## 1. SPI Electrical Timing (ICM-42688-P)

| Parameter | Symbol | Min | Max | Unit |
|---|---|---|---|---|
| SCLK frequency | f_SCLK | – | 24 | MHz |
| SCLK low/high period | t_LOW / t_HIGH | 17 | – | ns |
| CS setup | t_SU;CS | 39 | – | ns |
| CS hold | t_HD;CS | 18 | – | ns |
| SDI setup | t_SU;SDI | 13 | – | ns |
| SDI hold | t_HD;SDI | 8 | – | ns |
| SDO valid after SCLK fall | t_VD;SDO | – | 21 | ns |
| SDO disable after CS high | t_DIS;SDO | – | 28 | ns |

**Protocol**
- SPI mode 0 or 3, MSB first.
- First byte is `R/W̄ | addr[6:0]`. Bit 7 = 1 means read.
- Burst reads auto-increment.
- **Recommended SCLK: 20–24 MHz.** Use about 12 MHz during init if your traces are long.
- Keep traces under 10 cm. Add a 22–33 Ω series resistor on SCLK at the MCU.
- Disable I²C (`INTF_CONFIG0`/`INTF_CONFIG6` per datasheet) to prevent mode ambiguity.

---

## 2. Bandwidth Budget

Use the **FIFO with watermark batching**. A per-sample DRDY interrupt at 32 kHz arrives every 31.25 µs, which is too frequent to service reliably.

**FIFO packet:** Packet 3 is 16 bytes: header 1 B, accel 6 B, gyro 6 B, temp 1 B, timestamp 2 B. Use Packet 4 (20 B) if you need 20-bit high-resolution data.

**Trick:** `FIFO_COUNTH` (0x2E), `FIFO_COUNTL` (0x2F) and `FIFO_DATA` (0x30) are contiguous. A single burst starting at 0x2E returns the count and then the data in one CS-low transaction.

| Item | Per IMU | Notes |
|---|---|---|
| Raw data rate | 16 B × 32 kHz = 512 kB/s | 4.10 Mbit/s |
| Watermark | 16 packets = 256 B | Interrupt rate of 2 kHz (500 µs) |
| Burst length | 1 addr + 2 count + 256 data = **259 B** | |
| Burst time @ 24 MHz | 259 × 8 / 24 MHz ≈ **86.3 µs** | Plus about 1 µs CS/DMA overhead |
| Burst time @ 20 MHz | ≈ **103.6 µs** | |

**Topology recommendation: two separate SPI buses, one per IMU.**
- On separate buses each bus is about 17–21% utilized, with no contention and deterministic latency.
- On a shared bus, utilization is about 35–42%. This is still workable, but the second IMU's read is delayed by up to about 104 µs, and you need arbitration logic.

FIFO headroom: the 2 kB FIFO holds about 128 packets, roughly 4 ms. That leaves about 3.5 ms of slack beyond the watermark before overflow.

---

## 3. Register Initialization (per IMU, Bank 0)

```c
// Sequence per IMU; delays are mandatory
wr(0x76, 0x00);        // REG_BANK_SEL = bank 0
wr(0x11, 0x01);        // DEVICE_CONFIG: soft reset
delay_ms(2);
assert(rd(0x75) == 0x47);  // WHO_AM_I

wr(0x4D, /*INTF_CONFIG1*/ 0x91);  // RTC/clock: PLL auto-select (keep default CLKSEL=01)
wr(0x14, 0x03);        // INT_C