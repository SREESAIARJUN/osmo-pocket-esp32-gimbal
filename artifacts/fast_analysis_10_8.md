# Dual ICM-42688-P SPI Timing and DMA Configuration at 32 kHz

## Issues to Resolve First

**1. "Cycle 10" isn't a term I can verify.** It doesn't match any TDK InvenSense datasheet, DMP firmware, or APEX version I know of. It might be an internal project milestone, a vendor SDK release, or a different part. I won't invent parameters for it. Tell me where it comes from and I can adjust.

**2. APEX and 32 kHz don't operate together the way the request implies.** APEX features (pedometer, tilt, tap, raise-to-wake, SMD, WoM) run on the on-chip DMP at its own `DMP_ODR`, which is selectable among a few low rates (on the order of 25/50/100/500 Hz). They're designed for low-power, low-ODR accelerometer operation. You can't get "APEX at 32 kHz." The realistic options are:
- **A:** 32 kHz raw gyro/accel streaming for control/estimation, no APEX.
- **B:** APEX on one IMU at its supported rate, 32 kHz raw on the other.
- **C:** Mode switching. Use APEX in low-power/idle, then reconfigure to 32 kHz when active.

Check the current datasheet (DS-000347) and the APEX application note for which accel ODR / power-mode combinations are valid alongside APEX. The rest of this answer assumes Option A or B for the high-rate path.

---

## Bandwidth Budget

| Parameter | Value |
|---|---|
| ODR | 32 kHz → 31.25 µs period |
| SPI max clock (datasheet) | 24 MHz |
| FIFO packet 3 (accel+gyro+temp+timestamp) | 16 bytes |
| FIFO packet 4 (20-bit hi-res) | 20 bytes |
| Per IMU, packet 3 | 512 kB/s ≈ 4.1 Mbit/s |
| Both IMUs, packet 3 | ≈ 8.2 Mbit/s payload |

At 24 MHz, a 16-byte burst plus 1 address byte takes about 5.7 µs of clock time. That fits easily in 31.25 µs, but **don't take a CPU interrupt per sample per IMU** (64k IRQs/s). Batch with the FIFO watermark.

**Recommendation:** use **separate SPI peripherals per IMU**. This gives independent DMA streams, no CS arbitration, and deterministic latency. Sharing one bus works bandwidth-wise but couples jitter between the sensors.

---

## SPI Electrical/Timing Settings

- **Mode:** 0 (CPOL=0, CPHA=0) or 3. The device supports both; pick one and stay consistent.
- **Bit order:** MSB first, 8-bit frames.
- **Read:** first byte = `0x80 | reg_addr`, then the auto-incrementing burst.
- **Clock:** start at 12–16 MHz on a prototype. Go to 24 MHz only after scope-validating signal integrity, especially with any trace length or connectors.
- **CS:** use hardware NSS or a GPIO with a small guard delay before the first SCLK edge and after the last. Take the exact tCSS/tCSH minimums from the datasheet's SPI timing table; they're in the tens of ns, so a few cycles of margin is enough.
- **Interface lock:** set `INTF_CONFIG0`/`DEVICE_CONFIG` appropriately and disable I²C (`I3C/I2C` slew/disable bits) to avoid mode confusion.
- **Sync:** for coherent dual-IMU timestamps, drive both `FSYNC`/CLKIN pins from a common MCU timer (RTC/CLKIN mode, 32.768 kHz external clock option) so the ODRs don't drift relative to each other.

## Sensor Register Config (per IMU, high-rate path)

```
PWR_MGMT0      : GYRO_MODE=LN, ACCEL_MODE=LN
GYRO_CONFIG0   : ODR = 32 kHz, FS as required
ACCEL_CONFIG0  : ODR = 32 kHz, FS as required
FIFO_CONFIG    : Stream-to-FIFO mode
FIFO_CONFIG1   : ACCEL_EN, GYRO_EN, TEMP_EN, TMST_FSYNC_EN (packet 3)
                 (+HIRES_EN for packet 4)
FIFO_CONFIG2/3 : watermark = 16 packets (256 B) -> 2 kHz interrupt
INT_CONFIG     : INT1 push-pull, active high, pulsed
INT_SOURCE0    : FIFO_THS_INT1_EN
INTF_CONFIG0   : FIFO_COUNT in records, big-endian as preferred
```

Wait ≥ the datasheet gyro startup time (tens of ms) after enabling LN mode before trusting data. Verify the register field encodings against the datasheet; bank 0 addresses are standard, but bit positions are where bugs creep in.

## DMA Scheme (STM32H7-style example)

**Flow per IMU:**
1. INT1 (FIFO watermark) → EXTI ISR.
2. ISR assert CS, launch DMA: TX = `[0x80|FIFO_COUNTH]` + dummy bytes, RX into buffer. Better yet, read a fixed 16 packets directly from `FIFO_DATA` (one address byte + 256 bytes) to skip the count read.
3. DMA RX-complete IRQ → deassert CS, swap ping-pong buffer, signal the processing task.

```c
#define PKT_SZ      16
#define WM_PKTS     16
#define XFER_SZ     (1 + PKT_SZ*WM_PKTS)   // 257 bytes

// Place in DMA-accessible, non-cacheable RAM (or clean/invalidate D-cache)
__attribute__((section(".dma_buf"), aligned(32)))
static uint8_t rx[2][2][XFER_SZ];   // [imu][pingpong][bytes]
static uint8_t tx[XFER_SZ] = { 0x80 | REG_FIFO_DATA };  // rest = 0x00

void EXTI_IMUx_IRQHandler(int imu) {
    clear_exti(imu);
    CS_LOW(imu);
    HAL_SPI_TransmitReceive_DMA(&hspi[imu], tx, rx[imu][buf_idx[imu]], XFER_SZ);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h) {
    int imu = (h == &hspi[0]) ? 0 : 1;
    CS_HIGH(imu);
    SCB_InvalidateDCache_by_Addr(rx[imu][buf_idx[imu]], XFER_SZ); // if cached
    publish(imu, &rx[imu][buf_idx[imu]][1]);
    buf_idx[imu] ^= 1;
}
```

**DMA settings:**
- Normal mode (not circular), since the trigger is external.
- Byte width, memory increment on RX. TX can use a fixed zero source after the address byte.
- RX priority above TX to avoid overrun.
- FIFO/burst enabled.
- Separate streams per SPI.

**Timing at 24 MHz:** 257 bytes ≈ 86 µs per burst every 500 µs per IMU, or about 17% bus duty. That leaves room for an occasional `FIFO_COUNT` read to detect overflow.

## Robustness Checks

- Parse each packet header and discard any with `0x80` (empty/invalid) set.
- Monitor `INT_STATUS` FIFO_FULL. On overflow, flush the FIFO (`SIGNAL_PATH_RESET.FIFO_FLUSH`) and resync.
- Use the 16-bit FIFO timestamp to detect dropped samples and align the two IMUs.
- Check the `DEVICE_CONFIG`/`WHO_AM_I` (0x47) at boot on both devices.

If you clarify what "Cycle 10" refers to and which MCU you're using, I can tighten the DMA code and timing numbers to your exact hardware.