# ArduPilot Bi-Directional DShot600 on STM32H743 (TIM1 / TIM8)

This is a reconstruction of the logic in `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`, written as a formal specification with C-style pseudocode. It is not a verbatim copy of the source. Constants that depend on board clock configuration are marked.

I've interpreted **"Cycle 4"** as the **4-channel telemetry capture rotation**. When a timer has only one capture DMA stream, telemetry is read from one channel per output cycle, so a full RPM refresh of a 4-channel group takes 4 cycles. If you meant something else, let me know.

---

## 1. Clock and Bit Timing

On an H743 at 400 MHz, TIM1 and TIM8 sit on APB2 (100 MHz), so the timer kernel clock is ×2 = **200 MHz**. A 480 MHz configuration gives 240 MHz.

| Parameter | DShot600 TX | Telemetry RX (GCR, 5/4 rate) |
|---|---|---|
| Bitrate | 600 kbit/s | 750 kbit/s |
| Bit period | 1.667 µs | 1.333 µs |
| Frame length | 16 bits + 2 reset slots | 21 bits (GCR-encoded 20 bits + start) |

ArduPilot's bit encoding (`DSHOT_BIT_WIDTH_TICKS`, with bit-0 and bit-1 duty values) uses two schemes:
- **Default:** 8 ticks per bit, bit-0 = 3 ticks, bit-1 = 6 ticks.
- **High-resolution:** 20 ticks per bit, bit-0 = 7 ticks, bit-1 = 14 ticks.

The prescaler is chosen so that the timer runs at `rate × ticks_per_bit`:

```c
// Example: high-resolution scheme on a 200 MHz timer clock
target_hz  = 600000 * 20;                       // 12 MHz
psc        = (200000000 + target_hz/2) / target_hz - 1;   // = 16 (rounded)
actual_hz  = 200000000 / (psc + 1);             // 11.76 MHz, about 2% slow
ARR        = 20 - 1;
```

ArduPilot tolerates this rounding error. ESCs accept roughly ±5% bit-time error.

**Inversion for bi-directional mode.** The line idles high and the data pulses are active-low. This is set with `CCxP = 1` (output compare polarity inverted), or equivalently PWM mode 2. The GPIO uses a pull-up, because the ESC drives the reply on the same wire.

---

## 2. DMA Burst Output (DMAR / DCR)

All four channels of a timer are updated from a single DMA request on the **UPDATE** event, using burst mode.

```c
// Point the burst at CCR1..CCR4
TIMx->DCR = ((4 - 1) << TIM_DCR_DBL_Pos)          // DBL = 3 → 4 transfers per update
          | ((offsetof(TIM_TypeDef, CCR1) >> 2)); // DBA = 0x34/4 = 13
TIMx->DIER |= TIM_DIER_UDE;                        // DMA request on update

// Interleaved buffer: [bit][channel]
uint32_t dma_buffer[(16 + 2) * 4];   // 18 slots × 4 channels, uncached / D2 SRAM
```

### DMAMUX1 request IDs (RM0433)

| Timer | CH1 | CH2 | CH3 | CH4 | UP |
|---|---|---|---|---|---|
| TIM1 | 11 | 12 | 13 | 14 | **15** |
| TIM8 | 47 | 48 | 49 | 50 | **51** |

### TX DMA stream configuration

```c
dmaStreamSetPeripheral(tx_stream, &TIMx->DMAR);
dmaStreamSetMemory0  (tx_stream, dma_buffer);
dmaStreamSetTransactionSize(tx_stream, 18 * 4);
dmaStreamSetMode(tx_stream,
    STM32_DMA_CR_CHSEL(UP_req) | STM32_DMA_CR_DIR_M2P |
    STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
    STM32_DMA_CR_MINC | STM32_DMA_CR_PL(3) | STM32_DMA_CR_TCIE);
```

### Packing the frame

```c
uint16_t pkt = (throttle << 1) | telem_req;          // 11-bit value + telemetry bit
uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8));
csum = (~csum) & 0xF;                                // INVERTED CRC for bidirectional
pkt = (pkt << 4) | csum;

for (int b = 0; b < 16; b++)
    dma_buffer[b*4 + ch] = (pkt & (0x8000 >> b)) ? BIT_1_TICKS : BIT_0_TICKS;
dma_buffer[16*4 + ch] = dma_buffer[17*4 + ch] = 0;   // reset / idle slots
```

The inverted checksum is what tells the ESC to answer with eRPM telemetry.

---

## 3. TX → RX Turnaround (per timer group)

```
[TX DMA TC IRQ]
   ├─ stop the timer, disable UDE
   ├─ release the UP DMA stream (if it is shared)
   ├─ select capture channel  c = telem_chan[cycle % 4]   ← "Cycle 4" rotation
   ├─ reconfigure channel c:
   │     CCMRx: CCxS = 01 (input on TIx), ICxF = small filter
   │     CCER : CCxP = 1, CCxNP = 1   (capture BOTH edges)
   │     PSC  : tick ≈ (fine resolution, e.g. 1 tick per ~60–100 ns)
   │     ARR  : 0xFFFF
   │     DIER : CCxDE = 1
   ├─ arm the RX DMA on CHc request (TIM1: 11+c, TIM8: 47+c)
   │     P2M, PSIZE=HALF/WORD, MINC, len = GCR_BUF_LEN (~32 edges)
   ├─ start the timer
   └─ arm a virtual timer timeout ≈ 30 µs reply delay + 21×1.333 µs + margin (~80 µs)

[timeout or RX DMA TC]
   ├─ stop the capture DMA; count = len - NDTR
   ├─ restore channel c to PWM output (CCxS = 00, OCxM = PWM, polarity inverted)
   ├─ queue decode(capture_buf, count, c)
   └─ group->cycle++   // next frame captures channel (