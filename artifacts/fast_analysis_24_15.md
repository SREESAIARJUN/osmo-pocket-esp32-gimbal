# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Design

## Issues to Resolve First

**1. "Cycle 24" is not something I can identify.** It doesn't match any ICM-42688-P datasheet term, TDK eMD/SmartMotion release, or APEX feature revision I know of. If it refers to an internal project milestone, a vendor driver version, or a document from your organization, I don't have it. Please share it if it carries specific requirements. I won't invent parameters for it.

**2. APEX and 32 kHz ODR conflict.** The APEX motion functions (pedometer, tilt, tap, raise-to-wake, wake-on-motion) run on the DMP at low rates:

- `DMP_ODR` is 25, 50, 100, or 500 Hz.
- Tap detection expects accelerometer ODR of 200 Hz, 500 Hz, or 1 kHz.

They are not designed to run with the accelerometer at 32 kHz. Realistic options:

- **A.** Gyro at 32 kHz in LN mode, accel at an APEX-compatible ODR (e.g., 1 kHz), APEX enabled. You get asymmetric data.
- **B.** Both sensors at 32 kHz, APEX disabled. Compute motion features on the host.
- **C.** Both sensors at 32 kHz, with APEX/WoM used only during a separate low-power mode you switch into.

Check the current datasheet (DS-000347) and TDK's APEX application note for your silicon revision. The rest of this answer assumes **Option B or A** for the high-rate stream.

---

## Bandwidth Budget (per IMU)

| Item | Value |
|---|---|
| Sample period @ 32 kHz | 31.25 µs |
| FIFO packet 3 (accel + gyro + temp + 16-bit timestamp) | 16 bytes |
| FIFO packet 4 (high-res 20-bit) | 20 bytes |
| Throughput, packet 3 | 512 kB/s ≈ 4.1 Mbit/s |
| Throughput, packet 4 | 640 kB/s ≈ 5.1 Mbit/s |
| SPI max SCLK | 24 MHz |
| Bus utilization @ 24 MHz, batched FIFO reads | ~20–25 % |

Both IMUs on one bus at 24 MHz would use about 45–50% utilization. This is feasible but leaves little margin for jitter, retries, or register access. **Use separate SPI peripherals, one per IMU.** This gives you parallel DMA, independent timing, and fault isolation.

---

## SPI Electrical/Timing Configuration

These are typical datasheet values. Verify each against Table "SPI Timing Characteristics" in your datasheet revision.

| Parameter | Setting |
|---|---|
| Mode | 0 (CPOL=0, CPHA=0) or 3; use 3 if you share a bus with other devices that need it |
| Bit order | MSB first |
| SCLK | ≤ 24 MHz. Start at 12–16 MHz and increase after signal-integrity validation |
| Read bit | Address byte bit 7 = 1 for read |
| CS setup (tCSS) / hold (tCSH) | Tens of ns. Add ≥ 100 ns margin in firmware/hardware |
| CS high between transactions | ≥ ~100 ns; ensure the DMA end-of-transfer to CS-deassert path respects this |
| Auto-increment | Enabled by default on burst reads |
| Interface lock | Set `SPI_SLEW_RATE`/drive strength appropriately in `DRIVE_CONFIG` for your trace length. Disable I²C (`INTF_CONFIG0`/`INTF_CONFIG6` per datasheet) so the pin can't misinterpret glitches |

**Signal integrity at 24 MHz:** keep traces short (< 5–8 cm), add 22–33 Ω series resistors on SCLK/MOSI near the MCU, and use a solid ground reference.

---

## Sensor Register Configuration (per IMU)

```
DEVICE_CONFIG     : soft reset, wait ≥ 1 ms
INTF_CONFIG0      : FIFO count in records, big-endian, (disable I2C if applicable)
PWR_MGMT0         : GYRO_MODE = LN, ACCEL_MODE = LN  (wait ≥ 200 µs after enabling gyro;
                    do not write registers in the 200 µs after a mode change)
GYRO_CONFIG0      : FS as required, ODR = 32 kHz
ACCEL_CONFIG0     : FS as required, ODR = 32 kHz (Option B) or 1 kHz (Option A)
GYRO/ACCEL_CONFIG1, GYRO_ACCEL_CONFIG0 : UI filter order/bandwidth for your application
FIFO_CONFIG       : Stream-to-FIFO mode
FIFO_CONFIG1      : enable accel, gyro, temp, timestamp (+ HIRES for packet 4)
FIFO_CONFIG2/3    : watermark, e.g., 16 records (= 500 µs batch)
INT_CONFIG        : INT1 push-pull, active high, pulsed
INT_SOURCE0       : FIFO_THS_INT1_EN
INTF_CONFIG1 / Bank1 INTF_CONFIG5 : optional RTC_MODE + CLKIN on pin 9 (see sync below)
```

---

## Synchronizing the Two IMUs

Each IMU's internal oscillator drifts (~±1–2%), so the two 32 kHz streams will slip relative to each other. To fix this:

- **Best:** feed a common 32.768 kHz CLKIN to both IMUs (pin 9, `RTC_MODE=1`). The ODRs then derive from the same reference.
- **Plus:** enable FIFO timestamps and capture each FIFO-watermark interrupt edge with an MCU hardware timer input-capture. This lets you align the streams on the host.
- **Alternative:** use FSYNC from a common MCU timer output, tagged into the FIFO.

---

## DMA Architecture

**Don't DMA per sample.** Two IMUs at 32 kHz would generate 64 k interrupts/s. Batch via the FIFO watermark instead.

**Flow per IMU, at a watermark of N = 16 records (every 500 µs):**

1. INT1 rising edge triggers EXTI, which timestamps via input capture.
2. ISR (minimal):
   - Assert CS.
   - Start DMA: TX = `[0x80 | FIFO_DATA_REG]` + N×16 dummy bytes, RX into ping-pong buffer.
   - Alternatively, read `FIFO_COUNT` first in a short 3-byte transfer, then read the exact count. This is more robust to overruns but costs one extra transaction.
3. DMA RX-complete IRQ:
   - Deassert CS after the SPI busy flag clears. Don't deassert on DMA-complete alone; the last byte may still be shifting.
   - Swap ping-pong buffer.
   - Signal the processing task.

**Transfer size at N = 16:** 1 + 256 bytes (packet 3) ≈ 86 µs at 24 MHz. That fits easily in the 500 µs window.

**DMA settings (STM32-style; adapt to your MCU):**

| Setting | RX stream | TX stream |
|---|---|---|
| Direction | Periph → Mem | Mem → Periph |
| Data width | 8-bit | 8-bit |
| Mem increment | Yes | Yes (or No with fixed dummy byte) |
| Mode | Normal (re-armed per batch) or circular with double buffer | Normal |
| Priority | Very High | High |
| FIFO/burst | Enable, 4-beat if aligned | Same |

**Other DMA considerations:**

- **Cache coherence (Cortex-M7/H7):** put buffers in non-cacheable RAM (MPU region) or invalidate the D-cache by 32-byte-aligned lines after each RX.
- **Priorities:** the RX stream must outrank TX to avoid overrun. Give the two IMUs' DMA streams equal priority on separate controllers if possible.
- **Overflow handling:** check the FIFO header for invalid packets (`0x80` header / `-32768` values) and monitor `FIFO_FULL_INT`. On overflow, flush with `SIGNAL_PATH_RESET.FIFO_FLUSH`.

---

## Timing Summary (per IMU, N = 16)

```
t=0        INT1 (watermark)          ─┐
t≈1–2 µs   ISR → CS low → DMA start   │  latency budget
t≈88 µs    DMA complete → CS high     │
t≈90 µs    buffer handed to task     ─