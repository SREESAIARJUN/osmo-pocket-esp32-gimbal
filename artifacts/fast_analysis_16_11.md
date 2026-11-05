# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Two problems with the requested configuration

**1. APEX cannot run at a 32 kHz ODR.** The APEX engine (pedometer, tilt, tap, raise-to-wake, SMD, WoM) runs off the DMP, which is set by `APEX_CONFIG0.DMP_ODR` to **25, 50, 100 or 500 Hz** only. Several features also require a specific accel ODR (tap needs 200 Hz to 1 kHz, for example). You therefore cannot get 32 kHz raw data and APEX on the same sensor at the same time. Practical options:
- **Option A (recommended):** IMU #1 runs at 32 kHz raw for control or vibration work. IMU #2 runs at a lower ODR (for example, accel at 1 kHz with DMP at 500 Hz) for APEX.
- **Option B:** Both run at 32 kHz. You compute motion events on the MCU instead of using APEX.

**2. "Cycle 16" is not a term I can find in TDK's ICM-42688-P documentation.** It does not match a known register field, APEX mode or DMP setting. It may come from a different part (for example, ICM-45xxx), an internal document, or a misremembered name. Tell me where it comes from and I'll map it. Until then I've ignored it.

Verify all register addresses and bitfields below against the current datasheet (DS-000347) before use.

---

## 1. Bandwidth budget

| Parameter | Value |
|---|---|
| ODR | 32 kHz (period 31.25 µs) |
| FIFO packet (Packet 3: header + accel + gyro + temp + timestamp) | 16 bytes |
| FIFO packet (Packet 4, 20-bit high-res) | 20 bytes |
| Per-IMU payload (16 B packets) | 512 kB/s, about 4.1 Mbit/s |
| Both IMUs | about 8.2 Mbit/s |
| SPI max SCLK | 24 MHz |
| FIFO size | 2 KB, about 128 packets (about 4 ms of buffering at 32 kHz) |

Don't read per sample. Reading 32k interrupts/s × 2 IMUs, with CS overhead, is wasteful and fragile. Use **FIFO watermark plus DMA bursts**.

**Recommended watermark:** 32 packets (512 B), giving one burst per IMU per 1 ms.
- Burst time at 24 MHz: (1 + 512) × 8 / 24e6 ≈ **171 µs**.
- Both IMUs on one bus: about 342 µs per 1 ms, so about **34% bus utilisation**. This is comfortable.
- The FIFO still has about 3 ms of headroom if service is late.

---

## 2. SPI electrical and timing

| Parameter | Setting |
|---|---|
| Mode | 0 (CPOL=0, CPHA=0) or 3; the device supports both |
| Bit order | MSB first |
| SCLK | ≤ 24 MHz (start at 12 MHz during bring-up) |
| Address byte | bit7 = 1 for read, bits 6:0 = register |
| CS setup before first SCLK edge | ≥ ~39 ns (use ≥ 100 ns) |
| CS hold after last SCLK edge | ≥ ~18 ns (use ≥ 100 ns) |
| CS high between transactions | ≥ ~ 40 ns (use ≥ 500 ns) |
| Burst reads | Address auto-increments. FIFO_DATA (0x30) does **not** increment; it streams. |

**Interface selection:** after power-up, set `INTF_CONFIG0`/`DRIVE_CONFIG` as needed. Disable I²C (`INTF_CONFIG0.UI_SIFS_CFG = 11` → SPI only) so a glitch can't flip the interface mode.

**Bus topology:**
- **Preferred:** two separate SPI peripherals, one per IMU. You get fully parallel DMA, no arbitration, and deterministic latency.
- **Acceptable:** one shared bus with two CS lines. You must serialise the bursts with a small scheduler (see section 5).
- At 24 MHz, keep traces short (under about 10 cm), add 22–33 Ω series termination on SCLK and MOSI at the MCU, and use a solid ground reference.

---

## 3. Register configuration (per IMU, Bank 0 unless noted)

```
// Reset and select interface
DEVICE_CONFIG (0x11)   = 0x01        // soft reset, wait >= 1 ms
INTF_CONFIG0  (0x4C)   = 0x33        // FIFO count big-endian, count in records, SPI only (check bits)
INTF_CONFIG1  (0x4D)   : RTC/clock as needed (CLKIN on pin 9 for sync between IMUs)

// ODR and full-scale
GYRO_CONFIG0  (0x4F)   = FS | ODR=0x01   // 32 kHz
ACCEL_CONFIG0 (0x50)   = FS | ODR=0x01   // 32 kHz
// Choose UI filter / AAF bandwidth appropriately (Bank 1/2 AAF registers)

// FIFO
FIFO_CONFIG   (0x16)   = 0x40        // stream-to-FIFO mode
FIFO_CONFIG1  (0x5F)   = accel_en | gyro_en | temp_en | tmst_fsync_en
                         (+ hires_en if Packet 4)
FIFO_CONFIG2/3 (0x60/0x61) = watermark = 32 (records)
                         (+ FIFO_WM_GT_TH so the interrupt fires on >= WM)

// Interrupts
INT_CONFIG    (0x14)   = push-pull, active-high, pulsed or latched
INT_CONFIG1   (0x64)   : clear INT_ASYNC_RESET bit (datasheet requirement)
INT_SOURCE0   (0x65)   = FIFO_THS_INT1_EN (+ FIFO_FULL_INT1_EN for overflow detection)

// Power on (do this last)
PWR_MGMT0     (0x4E)   = 0x0F        // gyro LN + accel LN
// wait >= 45 ms gyro startup; do not write registers for 200 µs after PWR_MGMT0
```

**Synchronising the two IMUs.** Feed a shared 32.768 kHz CLKIN to both (`INTF_CONFIG1.RTC_MODE`), or use FSYNC tagging. Otherwise each IMU's internal oscillator drifts (±x% ODR tolerance) and their 32 kHz streams slowly slip relative to each other. Using the FIFO timestamp field is still recommended.

### If using APEX on IMU #2 (Option A)

```
ACCEL_CONFIG0 = ODR 1 kHz (or 500 Hz), LN mode
APEX_CONFIG0 (0x56) = DMP_ODR 500 Hz (or 50 Hz) | feature enables
SIGNAL_PATH_RESET.DMP_INIT_EN = 1, then wait for completion
Configure APEX_CONFIG1..9 (Bank 4) for the features you use
INT_SOURCE6/7 (Bank 4) → route APEX events to INT2
```

---

## 4. DMA transfer structure

Each service cycle per IMU uses two transactions:

1. **Read FIFO count:** CS low, TX `[0x80|0x2E, 0, 0]`, RX 2 bytes, CS high. This can be a short blocking or polled transfer (about 1.5 µs).
2. **Burst read FIFO:** CS low, TX `[0x80|0x30]` followed by N×16 dummy bytes. RX goes into a DMA buffer. CS goes high in the DMA-complete ISR.

Optimisation: since the watermark guarantees at least 32 records, you can skip the count read and always pull exactly 32 records. Check each packet's header byte; 0xFF or 0x80 marks an empty or invalid packet. Read the count every Nth cycle to catch accumulation.

### Example (STM32H7/F7, HAL-style, one SPI per IMU)

```c
#define WM_PKTS    32
#define PKT_SZ     16
#define BURST_LEN  (1 + WM_PKTS*PKT_SZ)   // 513 bytes

// DMA-capable RAM, cache-line aligned (H7: place in D2 SRAM or clean/invalidate D-cache)
__attribute__((aligned(32))) static uint8_t tx_buf[BURST_LEN];
__attribute__((aligned(32))) static uint8_t rx_buf[2][2][BURST_LEN]; // [imu][pingpong]
static volatile uint8_t pp[2];

void imu_init_dma(void) {
    memset(tx_buf, 0, sizeof tx_buf);
    tx_buf[0] = 0x80 | 0x30;     // FIFO_DATA read
    // SPI: Mode 0, 8-bit, MSB first, prescaler → ≤24 MHz
    // DMA RX: periph→mem, byte, memory increment, HIGH priority (RX > TX to avoid overrun)
    // DMA TX: mem→periph, byte, memory increment, MEDIUM priority
    //