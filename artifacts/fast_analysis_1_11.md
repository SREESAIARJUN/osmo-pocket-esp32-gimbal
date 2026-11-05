# Bi-Directional DShot600 on STM32H743 TIM1/TIM8 in ArduPilot: Cycle 1

This first cycle covers the DMA request plumbing, the output-to-capture turnaround, and GCR/eRPM decoding. It follows the structure of `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`. Constants are shown as ArduPilot uses them or as derived from the H743 clock tree. The DMAMUX request IDs and the CCR1 register offset come from the H743 reference manual, so check them against RM0433 for your silicon revision. Treat the code as a formulation, not a drop-in patch.

---

## 1. Clock and Timer Base

| Item | Value |
|---|---|
| SYSCLK | 400 MHz (ArduPilot H743 default) |
| APB2 (TIM1, TIM8) | 100 MHz, giving 200 MHz timer clock (×2 rule) |
| DShot600 bit rate | 600 kbit/s, giving 1.6667 µs per bit |
| Ticks per bit (`DSHOT_BIT_WIDTH_TICKS_DEFAULT`) | 8 |
| Target timer frequency | 600k × 8 = 4.8 MHz |
| Prescaler | round(200 MHz / 4.8 MHz) − 1 = 41, giving 4.878 MHz (+1.6%, well within ESC tolerance) |
| ARR | 8 − 1 = 7 |
| Bit-0 high time | 3 ticks (≈ 615 ns) |
| Bit-1 high time | 6 ticks (≈ 1.23 µs) |

In bi-directional mode the output is **inverted**: idle is high, and data bits are low pulses. ArduPilot sets this with `CCxP = 1` in CCER, so the duty values above stay the same.

---

## 2. Frame Encoding (Inverted CRC)

```c
static inline uint16_t create_bdshot_packet(uint16_t value /*0..2047*/, bool telem_req)
{
    uint16_t pkt = (value << 1) | (telem_req ? 1 : 0);
    uint16_t csum = 0, t = pkt;
    for (uint8_t i = 0; i < 3; i++) { csum ^= t; t >>= 4; }
    csum = ~csum & 0xF;          // bidirectional => inverted CRC
    return (pkt << 4) | csum;
}
```

**DMA buffer layout:** 16 data bits plus 2 trailing zero slots, interleaved per channel for burst mode:

```c
#define DSHOT_BIT_LEN   18                       // 16 + 2 pad (line returns idle)
#define NCH             4
uint32_t dma_buffer[DSHOT_BIT_LEN * NCH];        // word-wide for DMAR

for (uint8_t b = 0; b < 16; b++)
    for (uint8_t c = 0; c < NCH; c++)
        dma_buffer[b*NCH + c] = (pkt[c] & (0x8000 >> b)) ? 6 : 3;
// last 2*NCH entries = 0
```

---

## 3. DMA Request Logic: Output Phase (Burst via DMAR)

All four channels of each timer are driven from one **UP** request stream, using the DMA burst register.

| Timer | UP request (DMAMUX1) | CH1 | CH2 | CH3 | CH4 |
|---|---|---|---|---|---|
| TIM1 | 15 | 11 | 12 | 13 | 14 |
| TIM8 | 51 | 47 | 48 | 49 | 50 |

```c
// DCR: DBA = CCR1 offset/4 = 0x34/4 = 13, DBL = NCH-1
TIMx->DCR  = ((NCH - 1) << TIM_DCR_DBL_Pos) | 13;
TIMx->DIER = TIM_DIER_UDE;                 // update -> DMA request
TIMx->PSC  = 41;  TIMx->ARR = 7;
TIMx->CCMRx: OCxM = PWM mode 1, OCxPE = 1
TIMx->CCER: CCxE = 1, CCxP = 1             // inverted for bdshot
TIMx->BDTR |= TIM_BDTR_MOE;                // advanced timers TIM1/TIM8 only!

dmaStreamSetPeripheral(dma_up, &TIMx->DMAR);
dmaStreamSetMemory0  (dma_up, dma_buffer);
dmaStreamSetTransactionSize(dma_up, DSHOT_BIT_LEN * NCH);
dmaStreamSetMode(dma_up, STM32_DMA_CR_CHSEL(req_up) |
                 STM32_DMA_CR_DIR_M2P | STM32_DMA_CR_PSIZE_WORD |
                 STM32_DMA_CR_MSIZE_WORD | STM32_DMA_CR_MINC |
                 STM32_DMA_CR_PL(3) | STM32_DMA_CR_TCIE);
TIMx->EGR = TIM_EGR_UG;                    // preload first value
dmaStreamEnable(dma_up);
TIMx->CR1 |= TIM_CR1_CEN;
```

**H7 cache constraint:** `dma_buffer` must be in non-cacheable SRAM (ArduPilot uses `malloc_dma`/AXI SRAM with an MPU region). Otherwise you must call `cacheBufferFlush()` before enabling the stream.

**Frame duration:** 18 × 1.64 µs ≈ 29.5 µs.

---

## 4. Turnaround: Output to Input Capture (Cycle 1 Channel)

TIM1 and TIM8 each have only enough spare DMA streams to capture **one channel per frame**. ArduPilot therefore rotates through the channels: `_bdshot.curr_telem_chan`, advanced each cycle. In Cycle 1, the channel captured is index 1 of the 0..3 rotation, i.e. CH2 (request 12 on TIM1, 48 on TIM8).

On the DMA transfer-complete ISR for the UP stream:

```c
void dma_up_irq(group &g) {
    TIMx->CR1 &= ~TIM_CR1_CEN;
    dmaStreamDisable(dma_up);
    // UP stream may now be lent to another timer (ArduPilot shares via Shared_DMA)
    bdshot_prepare_for_next_pulse / bdshot_config_icu_dshot(g, chan = curr_telem_chan);
}
```

```c
void bdshot_config_icu_dshot(TIM_Typ