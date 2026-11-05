# Dual ICM-42688-P at 32 kHz: SPI and DMA Configuration

## Issues With the Request

**"Cycle 15" is not a term I recognize.** It does not appear in TDK InvenSense documentation for the ICM-42688-P as far as I know. It may be an internal project name, a firmware or driver revision, or a mix-up with another part. Please tell me what it refers to so I can account for it.

**APEX and 32 kHz ODR may conflict.** APEX (pedometer, tilt, tap, raise-to-wake, SMD, WoM) runs at a DMP rate of only 25, 50, 100, or 500 Hz, set by `DMP_ODR` in `APEX_CONFIG0`. The datasheet ties APEX operation to specific accelerometer ODR and power-mode conditions, and I can't confirm APEX runs correctly with the accelerometer at 32 kHz in low-noise mode. **Check the current datasheet and app notes, or ask TDK, before designing around this.** If it is unsupported, the common workaround is a separate low-rate sensor for gestures, or time-multiplexing the modes.

Below is a solid baseline for the 32 kHz streaming path. **Verify all register values against your datasheet revision.**

---

## 1. Bandwidth Budget (per IMU)

| FIFO packet | Bytes | Rate at 32 kHz | Bit rate |
|---|---|---|---|
| Packet 3 (header + A + G + temp + timestamp) | 16 | 512 KB/s | ~4.1 Mbps |
| Packet 4 (20-bit hi-res) | 20 | 640 KB/s | ~5.1 Mbps |

- **Two IMUs, Packet 3:** about 8.2 Mbps of payload.
- **SPI clock:** run at the 24 MHz maximum (Mode 0 or 3) to leave margin for overhead and status reads.
- **Bus topology:** use **separate SPI peripherals** per IMU. A shared bus with two chip selects works at 24 MHz but roughly doubles worst-case latency, and the two DMA transactions must be serialized.

## 2. SPI Electrical and Timing

- Mode 0 or 3, MSB first, 8-bit frames.
- Read access: set bit 7 of the address byte (`0x80 | reg`).
- SCLK ≤ 24 MHz. Take CS setup/hold, SCLK high/low, and CS-high times from the datasheet's SPI timing table (they are tens of ns). Most MCU SPI peripherals at 24 MHz meet them, but check CS deassert time between bursts if you use GPIO CS.
- At 24 MHz, keep traces short, add series termination (22–33 Ω at the source), and route SCLK away from other signals.
- Set `INTF_CONFIG0` to big-endian data and FIFO count in **records** (`FIFO_COUNT_REC = 1`) to simplify the DMA length calculation.

## 3. Register Configuration (Bank 0 unless noted)

| Register | Addr | Value | Purpose |
|---|---|---|---|
| DEVICE_CONFIG | 0x11 | 0x01 | Soft reset (wait ≥1 ms) |
| INTF_CONFIG0 | 0x4C | FIFO_COUNT_REC=1, big-endian | Record-based count |
| GYRO_CONFIG0 | 0x4F | 0x01 | ±2000 dps, 32 kHz |
| ACCEL_CONFIG0 | 0x50 | 0x01 | ±16 g, 32 kHz |
| FIFO_CONFIG1 | 0x5F | 0x0F (or 0x2F with WM_GT_TH) | Accel + gyro + temp + timestamp |
| FIFO_CONFIG2/3 | 0x60/0x61 | watermark (e.g. 32 records) | ~1 ms batches |
| FIFO_CONFIG | 0x16 | 0x40 | Stream-to-FIFO |
| INT_CONFIG | 0x14 | push-pull, active high, pulsed | INT1 to MCU EXTI |
| INT_SOURCE0 | 0x65 | FIFO_THS_INT1_EN | Watermark interrupt |
| PWR_MGMT0 | 0x4E | 0x0F | Accel + gyro low-noise (wait ≥200 µs, no register writes during that time) |

**Synchronizing the two IMUs:** feed a common 32.768 kHz clock to pin 9 (CLKIN) on both devices. Enable it via `INTF_CONFIG1.RTC_MODE` and the Bank 1 `INTF_CONFIG5` pin-9 function. Both devices then share a timebase, and FIFO timestamps stay aligned.

## 4. DMA Strategy

**FIFO size:** 2 KB, which is about 128 Packet-3 records, or 4 ms of headroom at 32 kHz.

**Watermark-driven flow, per IMU:**
1. INT1 rising edge triggers the EXTI ISR.
2. Pull CS low and start a DMA transfer: TX `0x80|0x2E` plus 2 dummy bytes, RX the FIFO_COUNT.
3. On completion (or in the same transaction, if you always read a fixed watermark count), start the burst: TX `0x80|0x30` (FIFO_DATA) plus N×16 dummy bytes, RX into a ping-pong buffer.
4. On DMA transfer-complete, raise CS, swap buffers, and signal the processing task.

**Fixed-length optimization:** with a 32-record watermark, always read exactly 512 bytes (plus the address byte) in a single DMA transaction, without reading the count first. Validate each packet's header byte; `0x80` means empty/invalid. This takes about 175 µs per IMU at 24 MHz, so both buses stay below 20% utilization.

**Buffers:** use a double buffer of 2 × 513 bytes per IMU, placed in DMA-capable, non-cached RAM (or with cache maintenance on Cortex-M7).

**Priority:** give SPI RX DMA a higher priority than TX. Assign each IMU its own DMA stream so the two can run concurrently.

**Overflow guard:** watch the `FIFO_FULL` interrupt. If it fires, flush with `SIGNAL_PATH_RESET.FIFO_FLUSH` and resync timestamps.

## 5. If You Need APEX Concurrently

Validate on hardware with `DMP_ODR = 500 Hz`, and check `APEX_DATA` and `INT_STATUS3` behavior while the accelerometer runs at 32 kHz. If the datasheet or TDK says it is unsupported, keep the IMUs at 32 kHz for control and do gesture detection in software from the 32 kHz stream, decimated to roughly 500 Hz.

Tell me your MCU (STM32H7, RP2040, nRF, etc.) and what "Cycle 15" means, and I can give concrete DMA stream and channel assignments and driver code.