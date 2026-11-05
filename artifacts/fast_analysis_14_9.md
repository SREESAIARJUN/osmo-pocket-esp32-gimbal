# Dual ICM-42688-P: SPI Timing and DMA Configuration at 32 kHz

## Before You Start: Three Problems With the Request

**1. "Cycle 14" is unclear.** This isn't a TDK/InvenSense term I recognize. It might be an internal project milestone, a course module, or a document revision. If it points to specific requirements (MCU, bus topology, latency budget), share them and I'll adapt this.

**2. APEX and 32 kHz conflict.** APEX features (pedometer, tilt, tap, raise-to-wake, WoM, SMD) run on the on-chip DMP at low rates, configured via `DMP_ODR` (25/50/100/500 Hz depending on feature). They are designed for low-power, low-ODR operation. Some features require specific accel ODR or power-mode settings that are incompatible with 32 kHz low-noise gyro+accel streaming. You should:
- Run 32 kHz raw data for control or estimation, and do motion detection (tap, WoM, etc.) on the host, **or**
- Use APEX on a separate, low-rate configuration, **or**
- Check the current datasheet and the TDK APEX application note to see which features are valid for your ODR and power mode.

I've marked APEX configuration below as conditional rather than inventing a working combination.

**3. Verify every register value against your datasheet revision.** The values below match the public ICM-42688-P datasheet as I know it, but check them before production use, especially the bank 1/2/4 registers.

---

## 1. SPI Electrical and Protocol Parameters

| Parameter | Value |
|---|---|
| SPI mode | Mode 0 or Mode 3 (CPOL=CPHA=0 or CPOL=CPHA=1) |
| Max SCLK | 24 MHz (datasheet max). Use about 20 MHz if your traces are long. |
| Bit order | MSB first |
| Read/write flag | Bit 7 of address byte: `1` = read, `0` = write |
| Burst read | Auto-increment. FIFO_DATA does not increment, so it streams the FIFO. |
| Interface lock | Set `DEVICE_CONFIG`/`INTF_CONFIG0` so I²C is disabled (`UI_SIFS_CFG = 0b11`) to avoid spurious I²C decoding |
| CS setup/hold, SCLK high/low | Use datasheet SPI timing table. Add ≥100 ns CS-high idle between transactions as margin. |
| Power-on delay | ≥1 ms after soft reset before register access. Gyro needs ~30–45 ms to stabilize after enable. |

**Topology recommendation:** put each IMU on its **own SPI peripheral**. Sharing a bus at 32 kHz is possible (see the budget below), but separate buses give you parallel DMA, no CS arbitration, and lower jitter.

---

## 2. Timing Budget at 32 kHz

Sample period: **31.25 µs**.

### Option A: Direct register burst (lowest latency)
Read `TEMP_DATA1` (0x1D) through `GYRO_DATA_Z0` (0x2A): 1 address byte + 14 data bytes = **15 bytes = 120 bits**.

| SCLK | Transfer time | + CS/DMA overhead (~1 µs) | One IMU | Two IMUs, shared bus |
|---|---|---|---|---|
| 24 MHz | 5.0 µs | ~6.0 µs | 19% | ~38% |
| 20 MHz | 6.0 µs | ~7.0 µs | 22% | ~45% |
| 10 MHz | 12.0 µs | ~13 µs | 42% | ~83% (not recommended) |

The risk with Option A is that DRDY-driven reads at 32 kHz mean 64k interrupts/s for two IMUs. That is feasible on Cortex-M7/H7-class MCUs, but heavy elsewhere.

### Option B: FIFO batching (recommended)
Packet 3 (accel + gyro + temp + timestamp) is **16 bytes**. Packet 4 (20-bit high-res) is **20 bytes**.

Example: watermark at 8 packets, giving a read every 250 µs (4 kHz interrupt rate):
- Read `FIFO_COUNTH/L` (0x2E/0x2F) + `FIFO_DATA` (0x30): 1 + 2 + 128 bytes ≈ 131 bytes
- At 24 MHz: ~44 µs per IMU per 250 µs, about **18% bus load**
- Interrupt rate drops 8× and you get on-chip timestamps per sample

Simpler variant: read a fixed number of packets with `FIFO_DATA` directly, using the watermark as the count. Each packet has a header byte, so you can check validity (`0xFF`/`0x80` patterns indicate empty or invalid).

---

## 3. Register Configuration (per IMU)

```c
// Bank 0 unless noted. Write REG_BANK_SEL (0x76) before bank-specific access.
#define REG_BANK_SEL      0x76
#define WHO_AM_I          0x75   // expect 0x47
#define DEVICE_CONFIG     0x11
#define INTF_CONFIG0      0x4C
#define INTF_CONFIG1      0x4D
#define PWR_MGMT0         0x4E
#define GYRO_CONFIG0      0x4F
#define ACCEL_CONFIG0     0x50
#define GYRO_CONFIG1      0x51
#define GYRO_ACCEL_CONFIG0 0x52
#define FIFO_CONFIG       0x16
#define FIFO_CONFIG1      0x5F
#define FIFO_CONFIG2      0x60   // watermark low
#define FIFO_CONFIG3      0x61   // watermark high
#define INT_CONFIG        0x14
#define INT_CONFIG1       0x64
#define INT_SOURCE0       0x65
#define INT_STATUS        0x2D
#define FIFO_COUNTH       0x2E
#define FIFO_DATA         0x30
#define TEMP_DATA1        0x1D

void icm_init(imu_t *d) {
    wr(d, REG_BANK_SEL, 0x00);
    wr(d, DEVICE_CONFIG, 0x01);          // soft reset
    delay_ms(2);
    if (rd(d, WHO_AM_I) != 0x47) fault();

    wr(d, INTF_CONFIG0, 0x33);           // big-endian data/count, count in records, SPI only (I2C disabled)
                                         // verify bit fields; 0x30 | UI_SIFS_CFG=0b11
    wr(d, GYRO_CONFIG0,  0x01);          // ±2000 dps, ODR 32 kHz
    wr(d, ACCEL_CONFIG0, 0x01);          // ±16 g,     ODR 32 kHz
    // Anti-alias / UI filter: tune GYRO_CONFIG_STATIC2..5 (bank 1) and
    // ACCEL_CONFIG_STATIC2..4 (bank 2) for your bandwidth.

    wr(d, INT_CONFIG, 0x03);             // INT1 push-pull, active high, pulsed
    wr(d, INT_CONFIG1, 0x00);            // clear INT_ASYNC_RESET bit (required for proper INT operation per DS)

    // --- Option B: FIFO ---
    wr(d, FIFO_CONFIG1, 0x07);           // accel + gyro + temp in FIFO (packet 3); add TMST bit for timestamps
    wr(d, FIFO_CONFIG2, WM & 0xFF);      // watermark (bytes or records per INTF_CONFIG0)
    wr(d, FIFO_CONFIG3, (WM >> 8) & 0x0F);
    wr(d, FIFO_CONFIG, 0x40);            // stream-to-FIFO mode
    wr(d, INT_S