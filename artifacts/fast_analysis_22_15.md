# Dual ICM-42688-P: SPI Timing and DMA Configuration at 32 kHz

## Caveats

1. **"Cycle 22"** doesn't match any TDK document, silicon revision, or APEX version I know of. If it refers to an internal project milestone, a specific board revision, or a firmware release, I don't have its contents. The design below follows the public ICM-42688-P datasheet.
2. **APEX and 32 kHz ODR are in tension.** The APEX engine (pedometer, tilt, tap, raise-to-wake, SMD, WoM) runs at its own DMP rate of 25, 50, 100, or 500 Hz. It is designed for low-power accelerometer modes, and some features (e.g., tap) require specific accelerometer ODRs. Running the accelerometer at 32 kHz in Low-Noise mode while relying on APEX is not a documented combination. Validate it on hardware, or run APEX on one IMU and high-rate capture on the other.
3. **No MCU was specified.** The DMA section is written generically, with STM32-style terminology as an example.
4. **Verify all register addresses and timing values** against the current datasheet revision (DS-000347) before committing hardware. I've marked values you should double-check.

---

## 1. Bandwidth Budget

| Item | Value |
|---|---|
| ODR (gyro + accel, LN mode) | 32 kHz → 31.25 µs period |
| FIFO packet 3 (hdr + A + G + temp + timestamp) | 16 bytes |
| FIFO packet 4 (20-bit high-res) | 20 bytes |
| Per-IMU payload (16 B) | 512 kB/s ≈ 4.1 Mbit/s |
| Dual-IMU payload | ≈ 8.2 Mbit/s (+ command overhead) |
| Max SPI clock | 24 MHz (datasheet) |

**Recommendation:** use **two separate SPI buses**, one per IMU. On a shared bus at 24 MHz, utilization is about 35–40% before CS overhead and interrupt jitter, which works but leaves little margin and couples the timing of the two sensors.

## 2. SPI Electrical and Timing

| Parameter | Setting |
|---|---|
| Mode | Mode 0 (CPOL=0, CPHA=0) or Mode 3; pick one and use it for both devices |
| Bit order | MSB first |
| Read/write bit | Address MSB: 1 = read, 0 = write |
| SCLK | 24 MHz max. Use 12–20 MHz if trace length > ~5 cm or no series termination |
| Word size | 8-bit |
| CS | Software/GPIO-controlled CS is recommended so a burst stays under one assertion |
| CS setup/hold, SCLK high/low | Take from the datasheet SPI timing table (verify; on the order of tens of ns). Add ≥100 ns CS-to-first-edge margin in firmware |
| Series resistors | 22–33 Ω on SCLK/MOSI at the source |
| Interface lock | Set `INTF_CONFIG0`/`DRIVE_CONFIG` appropriately and disable I²C (`UI_SIFS_CFG` = SPI only) to prevent mode ambiguity |

**Direct-register burst cost (alternative to FIFO):** 1 address byte + 14 data bytes (TEMP_DATA1 0x1D → GYRO_DATA_Z0 0x2A) = 120 bits. That takes 5 µs at 24 MHz, which fits in 31.25 µs. However, it forces one interrupt per sample (64k IRQs/s for two IMUs). **The FIFO approach below is strongly preferred.**

## 3. Sensor Register Configuration (Bank 0 unless noted)

```
// Select bank 0
REG_BANK_SEL   (0x76) = 0x00
DEVICE_CONFIG  (0x11) = 0x01      // soft reset; wait ≥1 ms
INTF_CONFIG0   (0x4C) = 0x30 *    // big-endian data/FIFO count, SPI; verify bits
INT_CONFIG     (0x14) = 0x03      // INT1 push-pull, active high, pulsed (adjust)
FIFO_CONFIG    (0x16) = 0x40      // stream-to-FIFO
GYRO_CONFIG0   (0x4F) = 0x01      // ±2000 dps (FS=000), ODR=32 kHz (0001)
ACCEL_CONFIG0  (0x50) = 0x01      // ±16 g (FS=000), ODR=32 kHz
FIFO_CONFIG1   (0x5F) = 0x0F      // accel+gyro+temp+tmst; add bit4 for hi-res (20 B)
FIFO_CONFIG2/3 (0x60/0x61) = watermark (bytes or records per INTF_CONFIG0 setting)
INT_SOURCE0    (0x65) = 0x04      // FIFO threshold → INT1
PWR_MGMT0      (0x4E) = 0x0F      // gyro + accel Low-Noise; wait ≥200 µs, no writes during
```
`*` Check bit definitions in your datasheet revision.

**Anti-alias and UI filters (Banks 1 and 2):** at 32 kHz, configure the gyro and accel AAF (GYRO_CONFIG_STATIC2–5, ACCEL_CONFIG_STATIC2–4) for your bandwidth target. The defaults may not suit a 32 kHz pipeline.

**Watermark choice:** 16 records → interrupt every 500 µs → 256-byte DMA burst per IMU. With 2 KB of FIFO, this leaves about 7× headroom against interrupt latency.

**APEX (if attempted, per caveat 2):**
```
APEX_CONFIG0 (0x56): DMP_ODR = 500 Hz (or lower), enable desired features
SIGNAL_PATH_RESET (0x4B): DMP_INIT_EN, then wait for completion
Bank 4: feature thresholds (tap, WoM, tilt wait time, etc.)
INT_SOURCE6/7 (Bank 4): route APEX events to INT2 to keep them off the FIFO IRQ
```
Read APEX status (INT_STATUS3) only in the INT2 handler, not in the high-rate path.

## 4. DMA Architecture (per IMU, independent bus)

**Trigger chain**
```
INT1 (FIFO WM) → EXTI ISR:
   assert CS
   start DMA: TX buffer [0x2E|0x80, 0x00, 0x00] → read FIFO_COUNT (2 B)
DMA complete ISR:
   compute N = count (cap at buffer size, align to packet size)
   start DMA: TX [0x30|0x80, dummy × N] / RX N+1 bytes
DMA complete ISR:
   deassert CS
   hand buffer to processing (double-buffer swap)
```

**Lower-latency variant:** skip the count read and always read exactly `watermark × packet_size` bytes. Validate each packet header; an empty-FIFO header is 0x80/0xFF. This cuts one transaction and one interrupt.

**DMA channel settings**

| Setting | RX stream | TX stream |
|---|---|---|
| Direction | Periph → Mem | Mem → Periph |
| Data width | Byte / Byte | Byte / Byte |
| Mem increment | Yes | No (constant dummy 0x00) after the address byte |
| Mode | Normal (re-armed per burst) or circular with double