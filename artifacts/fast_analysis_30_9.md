# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Design

Before the configuration, there are two problems with the request:

1. **"APEX motion engine (Cycle 30)"** doesn't match anything in TDK InvenSense's ICM-42688-P documentation. There's no "Cycle 30" mode, revision, or setting that I know of. If it comes from an internal spec, a different part (e.g., ICM-45686), or a vendor app note, please share the source. I won't invent register values for it.
2. **APEX and 32 kHz ODR likely conflict.** APEX features (pedometer, tilt, tap, raise-to-wake, SMD) run on the on-chip DMP at its own `DMP_ODR` (25/50/100/500 Hz range), and the accelerometer ODR has to be set appropriately for each feature. Running the accelerometer at 32 kHz with APEX active at the same time is not a documented combination. Wake-on-Motion is the exception, since it's threshold-based and less ODR-constrained.

**Recommendation:** Run the IMUs at 32 kHz for raw data and do motion detection on the host, or verify the APEX/ODR combination with TDK before relying on it.

The rest covers the part that is well defined: dual IMUs at 32 kHz over SPI with DMA.

---

## 1. Timing Budget

| Parameter | Value |
|---|---|
| ODR | 32 kHz → **31.25 µs** per sample |
| SPI max clock (datasheet) | 24 MHz (modes 0 or 3) |
| Direct register burst: TEMP_DATA1 (0x1D) → GYRO_DATA_Z0 (0x2A) | 1 address byte + 14 data bytes = **15 bytes** |
| Transfer time @ 24 MHz | 15 × 8 / 24 MHz ≈ **5.0 µs** |
| Transfer time @ 12 MHz (safer) | ≈ **10.0 µs** |
| CS setup/hold + DMA turnaround | ~0.5–1 µs |

**Per-sample load:**

- **Separate SPI buses (recommended):** both reads run in parallel. About 5–10 µs per 31.25 µs window, so a 16–32% bus duty.
- **Shared bus with two CS lines:** reads are sequential. About 11–22 µs of the 31.25 µs window. This works at 24 MHz but leaves little margin at 12 MHz.

**Interrupt rate:** 64k data-ready IRQs per second (2 × 32k) is heavy. The FIFO-watermark approach in section 4 is strongly preferred.

## 2. SPI Electrical/Protocol Settings

- **Mode:** CPOL=1, CPHA=1 (mode 3) or mode 0. Pick one and keep it.
- **Bit order:** MSB first, 8-bit frames.
- **Read:** set bit 7 of the address (`0x80 | reg`). Multi-byte reads auto-increment.
- **CS:** hardware NSS or a GPIO driven by DMA/timer. Keep CS asserted for the whole burst.
- **Timing:** take tCSS, tCSH, tSHDW (CS high between transactions), and tDO exactly from the SPI timing table in the current datasheet revision. I'm not quoting nanosecond figures from memory.
- **Signal integrity:** at 24 MHz, keep traces short, add ~22–33 Ω series resistors on SCLK/MOSI near the MCU, and use a solid ground reference. If you see bit errors, drop to 12–16 MHz first.
- **Disable I²C:** set `INTF_CONFIG0`/`INTF_CONFIG6` (bank 0/1, check the datasheet) so the part is SPI-only. This avoids I²C-mode glitches.

## 3. Sensor Register Configuration (Bank 0)

Verify the addresses against your datasheet revision.

```c
// Per IMU
write(0x76, 0x00);        // REG_BANK_SEL = 0
write(0x11, 0x01);        // DEVICE_CONFIG: soft reset
delay_ms(2);
check(read(0x75) == 0x47);// WHO_AM_I

write(0x4F, 0x01);        // GYRO_CONFIG0:  FS=±2000 dps (0b000), ODR=32 kHz (0x1)
write(0x50, 0x01);        // ACCEL_CONFIG0: FS=±16 g (0b000),   ODR=32 kHz (0x1)
// Note: at 32 kHz, review the UI filter / AAF settings (GYRO_CONFIG1,
// ACCEL_CONFIG1, GYRO_ACCEL_CONFIG0, bank-1/2 AAF registers)

write(0x14, 0x03);        // INT_CONFIG: INT1 push-pull, active-high, pulsed
write(0x65, 0x08);        // INT_SOURCE0: route UI data-ready to INT1
                          // (or FIFO threshold, bit 2, if using the FIFO)
write(0x4E, 0x0F);        // PWR_MGMT0: gyro LN + accel LN
delay_ms(1);              // gyro needs ~30-45 ms to settle; discard early samples
```

**Clock synchronization (important for dual IMUs):** feed both IMUs a common external clock on pin 9 (CLKIN). Configure it via `INTF_CONFIG5` (bank 1, PIN9_FUNCTION = CLKIN) and `INTF_CONFIG1` (RTC_MODE). Without this, the two internal oscillators drift (±1–2%), and their 32 kHz streams slide relative to each other.

## 4. Two Acquisition Strategies

### A. Data-ready + DMA burst (lowest latency)
- INT1 from each IMU goes to an EXTI line.
- The EXTI ISR asserts CS and starts a 15-byte TX/RX DMA. The DMA-complete ISR deasserts CS and pushes the result into a ring buffer.
- Cost: 64k ISR pairs/s. Feasible on a Cortex-M7 at 400+ MHz, marginal on slower cores.

### B. FIFO watermark (recommended)
- `FIFO_CONFIG` (0x16) = stream mode. `FIFO_CONFIG1` (0x5F): enable accel + gyro + temp (+ timestamp).
- Packet 3 is **16 bytes** (header, accel, gyro, temp, timestamp). Packet 4 (high-res, 20-bit) is 20 bytes.
- Set the watermark (`FIFO_CONFIG2/3`) to around 16 packets = 256 bytes. That gives an interrupt every 500 µs per IMU.
- Read `FIFO_COUNTH/L` (0x2E/0x2F), then burst-read `FIFO_DATA` (0x30) via DMA.
- 257 bytes @ 24 MHz ≈ 86 µs per 500 µs. The IRQ rate drops 16×.
- The FIFO is 2 KB, so keep the watermark well below about 1 KB for overflow headroom.

## 5. DMA Configuration (STM32H7 example, one IMU per SPI)

```c
// SPI: master, mode 3, 8-bit, MSB first, prescaler -> ≤24 MHz
// DMA RX stream (higher priority than TX to avoid RX overrun)
hdma_rx.Init.Request             = DMA_REQUEST_SPI1_RX;
hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
hdma_rx.Init.