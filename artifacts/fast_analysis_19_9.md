# Dual ICM-42688-P at 32 kHz: SPI and DMA Configuration

## Two Issues to Resolve First

**1. "Cycle 19"** doesn't match any TDK/InvenSense document, datasheet revision, or standard I know of. If it's an internal project milestone or spec from your team, I don't have that context. Share the requirements it defines and I can adapt the configuration below.

**2. APEX and 32 kHz ODR are incompatible.** The APEX motion engine (pedometer, tilt, tap, raise-to-wake, WoM, SMD) runs on the on-chip DMP at fixed low rates (DMP_ODR = 25/50/100/500 Hz) and needs the accelerometer ODR set to match, typically ≤1 kHz. You can't get a 32 kHz raw stream and APEX from the same sensor at the same time. Your options:

| Option | Setup |
|---|---|
| A | Both IMUs at 32 kHz raw. Run motion detection (tap, step, tilt) in host firmware. |
| B | IMU #1 at 32 kHz raw. IMU #2 at ≤1 kHz accel with APEX enabled. |
| C | Switch modes at runtime: APEX in low-power idle, 32 kHz when active. |

The configuration below assumes **Option A, 32 kHz raw on both**, with notes for B and C.

---

## 1. Bandwidth Budget

| Item | Value |
|---|---|
| FIFO packet | Packet 3 (Accel + Gyro + Temp + Timestamp) = 16 B; Packet 4 (high-res, 20-bit) = 20 B |
| Per IMU @ 32 kHz (16 B packet) | 512 kB/s ≈ 4.1 Mbit/s |
| Per IMU @ 32 kHz (20 B packet) | 640 kB/s ≈ 5.1 Mbit/s |
| Both IMUs (20 B) | ≈ 10.2 Mbit/s payload |
| Max SPI SCLK | 24 MHz |

**Recommendation: use two independent SPI buses**, one per IMU. A shared 24 MHz bus works on paper (about 45–50% utilization with overhead), but separate buses remove arbitration jitter and let both DMA streams run concurrently.

Don't use one interrupt per sample (a 31.25 µs period). Read the FIFO in watermark-triggered bursts instead.

---

## 2. SPI Bus Timing

Values are from the ICM-42688-P datasheet (DS-000347). Verify them against your revision.

| Parameter | Symbol | Min | Note |
|---|---|---|---|
| SCLK frequency | f_SCLK | — | 24 MHz max; 10 MHz max for register writes during init is a safe practice |
| SCLK high/low time | t_HIGH / t_LOW | ~17 ns | |
| CS setup time | t_CSS | ~39 ns | |
| CS hold time | t_CSH | ~18 ns | |
| SDI setup / hold | t_SU / t_HD | ~13 / 8 ns | |
| SDO output valid | t_VD | — | ~18 ns max. Tight at 24 MHz with long traces |
| CS high between transactions | t_CSD | ~ hundreds of ns | Use ≥1 µs margin in firmware |

**SPI settings:**
- Mode 0 (CPOL=0, CPHA=0) or Mode 3. Pick one and keep it.
- MSB first, 8-bit frames.
- Read bit: address | 0x80. Burst reads auto-increment, except FIFO_DATA, which streams.
- Hardware design: keep traces under ~5 cm at 24 MHz, put series termination (22–33 Ω) on SCLK, and run SDO timing analysis. If margins are marginal, drop to 16–20 MHz. Bandwidth still fits.
- Write **INTF_CONFIG0 / DEVICE_CONFIG** early to lock in SPI and disable I²C (the I3C/I²C disable bits in INTF_CONFIG6 / INTF_CONFIG4 depending on revision).

---

## 3. Register Configuration (per IMU)

Bank 0 unless noted. REG_BANK_SEL = 0x76.

```c
// --- Reset ---
write(0x76, 0x00);          // Bank 0
write(0x11, 0x01);          // DEVICE_CONFIG: soft reset
delay_ms(2);
// Verify WHO_AM_I (0x75) == 0x47

// --- Interface ---
write(0x4C, 0x30);          // INTF_CONFIG0: FIFO count in records, big-endian
                            //   (adjust bits per your parsing preference)
write(0x4D, 0x91);          // INTF_CONFIG1: default clock sel = PLL when available

// --- ODR & full scale ---
// GYRO_CONFIG0 (0x4F): [7:5]=FS_SEL, [3:0]=ODR. ODR code 0x01 = 32 kHz
write(0x4F, (0b000 << 5) | 0x01);   // ±2000 dps, 32 kHz
// ACCEL_CONFIG0 (0x50): [7:5]=FS_SEL, [3:0]=ODR
write(0x50, (0b000 << 5) | 0x01);   // ±16 g, 32 kHz

// --- Filters (UI path) ---
// GYRO_ACCEL_CONFIG0 (0x52): UI filter BW. Choose per application.
// Configure AAF in Bank 1 (GYRO_CONFIG_STATIC2-5) and Bank 2 (ACCEL_CONFIG_STATIC2-4).
// At 32 kHz, set AAF bandwidth deliberately; don't rely on defaults.

// --- FIFO ---
write(0x16, 0x40);          // FIFO_CONFIG: Stream-to-FIFO mode
write(0x5F, 0x0F | 0x10);   // FIFO_CONFIG1: accel+gyro+temp+tmst_fsync, (+HIRES bit 0x10 for 20B)
// Watermark: FIFO_CONFIG2 (0x60) low, FIFO_CONFIG3 (0x61) high
// 16 records @ 20 B = 320 B -> interrupt at 2 kHz
write(0x60, 16);  write(0x61, 0);

// --- Interrupt ---
write(0x14, 0x03);          // INT_CONFIG: INT1 push-pull, active high, pulsed
write(0x65, 0x04);          // INT_SOURCE0: FIFO threshold -> INT1
write(0x63, 0x00);          // INT_CONFIG1: clear INT_ASYNC_RESET bit (bit4) per datasheet note

// --- Power on (Low-Noise mode required for 32 kHz) ---
write(0x4E, 0x0F);          // PWR_MGMT0: gyro LN + accel LN
delay_ms(45);               // gyro startup; discard first ~ms of data
```

**Watermark sizing.** The FIFO is 2 KB, about 102 records at 20 B. A watermark of 16 gives about 1.6 ms of headroom before overflow if a read is delayed by up to ~80 records. For tighter latency use 8; for lower CPU load use 32.

---

## 4. DMA Architecture

The example is STM32-style (H7/G4/F7), but the pattern is portable.

```
IMU1 INT1 ─► EXTI ─► start SPI1 DMA (TX dummy + RX buffer) ─► DMA TC IRQ ─► CS high, push to ring buffer
IMU2 INT1 ─► EXTI ─► start SPI2 DMA                         ─► DMA TC IRQ ─► CS high, push to ring buffer
```

**Read sequence per watermark interrupt:**
1. CS low.
2. Read FIFO_COUNTH/L (0x2E, 0x2F): a 3-byte transaction. You can skip this by always reading a fixed watermark count and validating headers.
3. CS high, then CS low.
4. Burst read FIFO_DATA (0x30): 1 address byte plus N×20 bytes via DMA.
5. On DMA complete: CS high, swap the ping-pong buffer, signal the processing task.

**Lower-latency variant:** always read exactly `WM × 20` bytes in one transaction, starting at 0x30. Check each packet header (bit 7 = empty/invalid, 0xFF means FIFO empty) and drop invalid records. This removes the count-read round trip.

**DMA settings per bus:**

| Setting | RX stream | TX stream |
|---|---|---|
| Direction | Periph → Mem | Mem → Periph |
| Data width | Byte | Byte |
| Mem increment