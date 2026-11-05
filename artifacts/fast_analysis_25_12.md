# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Clarifications First

**"Cycle 25"**: I don't recognize this as a TDK/InvenSense term for the ICM-42688-P or its APEX engine. It could be an internal project name, a silicon/firmware revision, or a term from another vendor. If you can tell me where it comes from, I can adjust. I haven't guessed at its meaning.

**APEX and 32 kHz don't run as one pipeline.** APEX (pedometer, tilt, tap, raise-to-wake, WoM, SMD) runs on the on-chip DMP at its own low rate, set by `DMP_ODR` (25/50/100/500 Hz class rates). It's designed for low-power, accel-driven gesture detection, not 32 kHz streaming. Running full-rate gyro/accel streaming *and* APEX together may be restricted or may degrade APEX behavior. Check the current datasheet and TDK's APEX application note (and the eMD/SmartMotion driver source) for supported ODR/power-mode combinations before committing. The config below treats them as separate concerns: high-rate data via SPI/DMA, and APEX events via INT2.

## 1. Data-Rate Budget

| Parameter | Value |
|---|---|
| ODR | 32 kHz, period 31.25 µs |
| Sensor modes | Gyro + accel in Low-Noise (LN) mode (required for 32 kHz) |
| Direct burst read | 1 addr + 14 data bytes (TEMP + ACCEL XYZ + GYRO XYZ, regs 0x1D–0x2A) = 15 bytes |
| FIFO packet 3 | 16 bytes (header, accel, gyro, temp, 2-byte timestamp) |
| FIFO packet 4 (20-bit) | 20 bytes |
| SPI clock | Up to 24 MHz per datasheet (verify for your silicon rev and board) |

**Per-device transfer time at 24 MHz:**
- Burst read: 15 B × 8 / 24 MHz ≈ 5.0 µs, plus CS overhead (~0.3 µs) ≈ **5.3 µs**
- Two devices on a shared bus: ≈ **10.6 µs per 31.25 µs period**, about 34% bus utilization

This works, but it leaves little room for jitter. Separate SPI peripherals per IMU are strongly preferred.

## 2. SPI Electrical/Protocol Settings

| Setting | Value |
|---|---|
| Mode | CPOL=1, CPHA=1 (Mode 3); Mode 0 is also supported |
| Bit order | MSB first |
| Read flag | Address bit 7 = 1 |
| Word size | 8-bit |
| CS | Active-low, software or hardware NSS; one CS per device |
| Init clock | ≤1 MHz during configuration writes (conservative), then raise for data reads |
| Interface lock | Set `INTF_CONFIG0`/`DRIVE_CONFIG` slew and SPI-only mode (disable I²C) to avoid mode ambiguity |

**Signal integrity at 24 MHz:**
- Keep traces short (<5 cm ideal)
- Use series termination (22–33 Ω) on SCLK/MOSI near the MCU
- Set `DRIVE_CONFIG` SPI slew rate appropriately; faster is not always better on long traces

## 3. Recommended Architecture

```
          SPI1 (24 MHz) ── IMU_A   INT1_A ──> EXTI ─> DMA trigger
MCU                                 INT2_A ──> EXTI (APEX events, low prio)
          SPI2 (24 MHz) ── IMU_B   INT1_B ──> EXTI ─> DMA trigger
                                    INT2_B ──> EXTI (APEX events)
          Timer/PWM 32 kHz ──> CLKIN (pin 9) of BOTH IMUs  (time sync)
```

**Synchronization:** Two free-running IMUs drift relative to each other because their internal oscillators have roughly ±1% class tolerance. Feed a common external clock to both via the **RTC/CLKIN pin**, enabled in `INTF_CONFIG1` / `INTF_CONFIG5` pin function. That locks both ODRs to your MCU timebase. Check the datasheet for the allowed CLKIN frequency range and the resulting ODR scaling.

## 4. Two Data-Acquisition Strategies

### Option A: DRDY-triggered direct reads (lowest latency)
- `INT_SOURCE0`: UI_DRDY → INT1, pulse mode, push-pull, active high
- On each DRDY edge, the EXTI ISR asserts CS and starts a 15-byte TX/RX DMA
- DMA TC ISR deasserts CS and flags the buffer
- Cost: 64,000 interrupts/s for two devices, so the MCU needs headroom (e.g., Cortex-M7 class)

### Option B: FIFO watermark batching (recommended)
- FIFO mode: stream; packet 3 (16 B) or packet 4 (20 B, high-res)
- Watermark example: 16 packets = 256 B, giving an interrupt every 0.5 ms
- Read `FIFO_COUNTH/L`, then DMA-burst `FIFO_DATA` (0x30)
- This cuts interrupt load by about 16× and gives per-sample timestamps
- At 32 kHz × 16 B = 512 kB/s per device, a 2 KB FIFO gives about 4 ms of margin before overflow

## 5. Example DMA Configuration (STM32H7-style, HAL/LL pseudocode)

```c
// SPI1 for IMU_A (repeat for SPI2 / IMU_B)
hspi1.Init.Mode              = SPI_MODE_MASTER;
hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
hspi1.Init.CLKPolarity       = SPI_POLARITY_HIGH;   // Mode 3
hspi1.Init.CLKPhase          = SPI_PHASE_2EDGE;
hspi1.Init.NSS               = SPI_NSS_SOFT;
hspi1.Init.BaudRatePrescaler = /* select for <=24 MHz from kernel clk */;
hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
hspi1.Init.FifoThreshold     = SPI_FIFO_THRESHOLD_01DATA;

// DMA streams
// RX: Periph->Mem, byte, mem increment, priority VERY_HIGH
// TX: Mem->Periph, byte, mem increment, priority HIGH
// RX priority > TX so RX never overruns
// Use DTCM/non-cacheable region for buffers OR clean/invalidate D-cache

static uint8_t txA[16] __attribute__((aligned(32))) = { 0x1D | 0x80 }; // burst from TEMP_DATA1
static uint8_t rxA[2][32] __attribute__((aligned(32)));               // double buffer

void EXTI_IMU_A_IRQHandler(void) {
    CS_A_LOW();
    HAL_SPI_TransmitReceive_DMA(&hspi1, txA, rxA[bufIdxA], 15);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h) {
    if (h == &hspi1) {
        CS_A_HIGH();
        SCB_InvalidateDCache_by_Addr((uint32_t*)rxA[bufIdxA], 32);
        publish(rxA[bufIdxA]); bufIdxA ^= 1;
    }
}
```

**Interrupt priorities:**
1. DMA TC (highest)
2. DRDY EXTI
3. APEX INT2 (lowest)

**Latency-sensitive builds:** Replace HAL with LL/register-level calls. HAL overhead (~1–3 µs) is significant against a 31.25 µs period.

## 6. Key Register Setup (verify against current datasheet)

| Register (Bank 0) | Purpose |
|---|---|
| `DEVICE_CONFIG` | Soft reset first, wait ≥1 ms |
| `PWR_MGMT0` | Gyro + accel LN mode; wait ≥200 µs after enabling gyro |
| `GYRO_CONFIG0` | FS + ODR = 32 kHz |
| `ACCEL_CONFIG0` | FS + ODR = 32 kHz |
| `GYRO_CONFIG_STATIC2..5` (Bank 1) | Anti-alias filter: set AAF bandwidth appropriate for 32 kHz |
| `ACCEL_CONFIG_STATIC2..4` (Bank 2) | Accel AAF |
| `INT_CONFIG` / `INT_CONFIG1` | INT1 pulse, push-pull; adjust `INT_ASYNC_RESET` per datasheet note |
| `INT_SOURCE0` | DRDY or FIFO_THS → INT1 |
| `FIFO_CONFIG` / `FI