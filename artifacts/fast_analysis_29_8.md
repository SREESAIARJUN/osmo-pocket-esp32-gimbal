# Bi-Directional DShot600 on STM32H743 TIM1/TIM8 in ArduPilot

This design follows the structure of ArduPilot's `RCOutput` / `RCOutput_bdshot` implementation in `AP_HAL_ChibiOS`. Register names and DMAMUX IDs come from the STM32H743 reference manual (RM0433). Exact tick counts in ArduPilot depend on version and hwdef, so verify them against your tree.

"Cycle 29" has no standard meaning in ArduPilot. Below it is treated as one iteration of the output→capture→decode loop. If it refers to something specific in your project, such as a test case, tell me and I'll adapt this.

---

## 1. Clocking and DShot600 Timing

| Parameter | Value |
|---|---|
| SYSCLK | 400 MHz (480 MHz on rev V parts) |
| TIM1/TIM8 bus | APB2, timer kernel clock = 200 MHz (at 400 MHz SYSCLK) |
| DShot600 bit rate | 600 kbit/s, so 1.667 µs per bit |
| Frame | 16 bits = 11-bit throttle, 1 telemetry-request bit, 4-bit CRC |
| Bidir CRC | **inverted**: `crc = ~(v ^ v>>4 ^ v>>8) & 0xF` |
| Signal polarity (bidir) | **inverted**: line idles high, bits are low pulses |
| Telemetry reply rate | 5/4 × 600k = 750 kbit/s, 21 GCR bits |
| Reply turnaround | about 30 µs after frame end |

### Output timebase

ArduPilot targets `DSHOT_BIT_WIDTH_TICKS` (default 20) ticks per bit:

```
target_clk = 600 kHz × 20 = 12 MHz
PSC        = round(200 MHz / 12 MHz) − 1 = 16   → actual tick = 11.76 MHz
ARR        = 20 − 1                              → bit = 1.70 µs (+2%, within ESC tolerance)
BIT_0_CCR  = 7   (~37.5% duty)
BIT_1_CCR  = 14  (~75% duty)
```

ArduPilot's `calculate_bitrate_prescaler()` performs this rounding. ESCs tolerate a few percent of error.

With bidir, active-low output is achieved via `CCxP = 1` (polarity inversion) rather than by inverting the buffer values.

---

## 2. Output Phase: DMA Burst via TIMx_UP

One update-event DMA request feeds all 4 CCRs per bit using the DMA burst mechanism.

### DMAMUX1 request IDs (H743)

| Request | TIM1 | TIM8 |
|---|---|---|
| UP | 15 | 51 |
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |

### Buffer layout

The buffer holds 18 slots per channel: 16 data bits plus 2 trailing zeros, which force the line to idle after the frame.

```c
#define DSHOT_BIT_WIDTH_TICKS 20
#define DSHOT_BIT_0_TICKS     7
#define DSHOT_BIT_1_TICKS     14
#define DSHOT_BUFFER_LENGTH   (18 * 4)   // words, 4 channels interleaved

// buf[bit*4 + chan] = CCR value for that channel at that bit
static void fill_dshot_buffer(uint32_t *buf, uint8_t chan, uint16_t packet)
{
    for (uint8_t i = 0; i < 16; i++) {
        buf[i*4 + chan] = (packet & 0x8000) ? DSHOT_BIT_1_TICKS : DSHOT_BIT_0_TICKS;
        packet <<= 1;
    }
    buf[16*4 + chan] = 0;
    buf[17*4 + chan] = 0;
}

static uint16_t make_bdshot_packet(uint16_t value, bool telem_req)
{
    uint16_t p = (value << 1) | (telem_req ? 1 : 0);
    uint16_t crc = (~(p ^ (p >> 4) ^ (p >> 8))) & 0x0F;   // inverted for bidir
    return (p << 4) | crc;
}
```

### Timer and DMA setup (TIM1; TIM8 is identical with request 51)

```c
void dshot_send_start(stm32_tim_t *TIMx, const stm32_dma_stream_t *dma, uint32_t *buf)
{
    TIMx->CR1  = 0;
    TIMx->PSC  = 16;
    TIMx->ARR  = DSHOT_BIT_WIDTH_TICKS - 1;

    // PWM mode 1 + preload on CH1..CH4
    TIMx->CCMR1 = (6 << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE |
                  (6 << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    TIMx->CCMR2 = (6 << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
                  (6 << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;

    // Enable outputs, inverted polarity for bidir
    TIMx->CCER = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC2E | TIM_CCER_CC2P |
                 TIM_CCER_CC3E | TIM_CCER_CC3P | TIM_CCER_CC4E | TIM_CCER_CC4P;
    TIMx->BDTR = TIM_BDTR_MOE;                          // advanced timers need MOE

    // DMA burst: base = CCR1 (offset 0x34/4 = 13), length = 4 transfers (DBL=3)
    TIMx->DCR  = (3 << TIM_DCR_DBL_Pos) | (13 << TIM_DCR_DBA_Pos);

    dmaStreamSetPeripheral(dma, &TIMx->DMAR);
    dmaStreamSetMemory0(dma, buf);                       // must be in DMA-capable RAM (AXI/SRAM1-3, not DTCM)
    dmaStreamSetTransactionSize(dma, DSHOT_BUFFER_LENGTH);
    dmaStreamSetMode(dma, STM32_DMA_CR_CHSEL(0) | STM32_DMA_CR_DIR_M2P |
                     STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
                     STM32_DMA