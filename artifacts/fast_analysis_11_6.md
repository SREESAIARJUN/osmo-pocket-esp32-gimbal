# Bi-Directional DShot600 on STM32H743 (TIM1 / TIM8): DMA Request Logic and RPM Telemetry Decoding

I couldn't find a defined meaning for "Cycle 11" in ArduPilot. The design below follows the logic in ArduPilot's `RCOutput` / `RCOutput_bdshot` (AP_HAL_ChibiOS) for TIM1 and TIM8. If "Cycle 11" refers to a specific iteration or constraint, tell me and I'll adapt it.

---

## 1. Timing Basis

| Parameter | Value (DShot600) |
|---|---|
| Bit rate | 600 kbit/s, so 1.667 µs per bit |
| Bit-1 high time | 75%, about 1.25 µs |
| Bit-0 high time | 37.5%, about 0.625 µs |
| Frame length | 16 bits plus 2 trailing zero (reset) slots |
| Telemetry reply | 21-bit GCR at 5/4 × 600k = 750 kbit/s, starting about 30 µs after the frame |
| H743 TIM1/TIM8 clock | APB2 timer clock: 200 MHz at a 400 MHz core, 240 MHz at 480 MHz |

ArduPilot uses 8 ticks per bit (ticks `DSHOT_BIT_WIDTH_TICKS = 8`, bit-0 = 3, bit-1 = 6). The timer is set up as follows:

```c
// ArduPilot-style timing setup
const uint32_t bitrate   = 600000;
const uint32_t tick_freq = bitrate * DSHOT_BIT_WIDTH_TICKS;      // 4.8 MHz
const uint16_t psc       = (timer_clock / tick_freq) - 1;        // 200MHz -> 40.67, rounded
// ArduPilot rounds psc, then adjusts the ARR / bit widths so that the error stays < ~2%
TIMx->PSC = psc;
TIMx->ARR = DSHOT_BIT_WIDTH_TICKS - 1;
```

For **bi-directional** operation the signal is **inverted**: the line idles high and pulses go low. This is done with PWM mode 2, or with mode 1 plus `CCxP = 1`.

---

## 2. DMAMUX Request IDs (RM0433, DMAMUX1)

| Timer | CH1 | CH2 | CH3 | CH4 | UP |
|---|---|---|---|---|---|
| TIM1 | 11 | 12 | 13 | 14 | **15** |
| TIM8 | 47 | 48 | 49 | 50 | **51** |

The requests are used in two phases:

- **Transmit phase:** the `TIMx_UP` request drives a DMA burst through `DMAR`/`DCR`. Each update event writes CCR1..CCR4, which outputs one bit on all 4 channels at once.
- **Receive phase:** a `TIMx_CHn` request moves input-capture timestamps from `CCRn` into a buffer for the channel being listened to.

---

## 3. Transmit: Burst DMA via TIMx_UP

```c
// Buffer layout: [bit][channel] packed, 18 slots x 4 channels x uint32_t
// Must live in DMA-reachable, cache-safe SRAM (AXI SRAM / SRAM1-3, NOT DTCM)
static uint32_t dshot_buf[ (16+2) * 4 ] __attribute__((section(".dma_buf"), aligned(32)));

static void dshot_fill(uint32_t *buf, uint8_t ch, uint16_t throttle, bool telem_req)
{
    uint16_t pkt = (throttle << 1) | (telem_req ? 1 : 0);
    // Bidirectional DShot: inverted CRC
    uint16_t csum = (~(pkt ^ (pkt >> 4) ^ (pkt >> 8))) & 0x0F;
    pkt = (pkt << 4) | csum;

    for (uint8_t i = 0; i < 16; i++) {
        buf[i*4 + ch] = (pkt & 0x8000) ? DSHOT_BIT_1_TICKS : DSHOT_BIT_0_TICKS;
        pkt <<= 1;
    }
    buf[16*4 + ch] = 0;   // reset slots (line held idle)
    buf[17*4 + ch] = 0;
}

static void dshot_tx_start(TIM_TypeDef *TIMx, DMA_Stream_TypeDef *s,
                           DMAMUX_Channel_TypeDef *mux, uint8_t up_req)
{
    SCB_CleanDCache_by_Addr(dshot_buf, sizeof(dshot_buf));   // H7 D-cache coherency

    // Outputs: PWM mode 2 (inverted for bidir), preload on
    TIMx->CCMR1 = (7<<4)|TIM_CCMR1_OC1PE | (7<<12)|TIM_CCMR1_OC2PE;
    TIMx->CCMR2 = (7<<4)|TIM_CCMR2_OC3PE | (7<<12)|TIM_CCMR2_OC4PE;
    TIMx->CCER  = TIM_CCER_CC1E|TIM_CCER_CC2E|TIM_CCER_CC3E|TIM_CCER_CC4E;
    TIMx->BDTR |= TIM_BDTR_MOE;                 // REQUIRED on advanced timers TIM1/TIM8

    // DMA burst: base = CCR1 offset (0x34/4 = 13), length 4 transfers
    TIMx->DCR  = (3 << TIM_DCR_DBL_Pos) | (13 << TIM_DCR_DBA_Pos);

    mux->CCR   = up_req;                        // 15 (TIM1_UP) or 51 (TIM8_UP)
    s->CR      = 0;
    while (s->CR & DMA_SxCR_EN) {}
    s->PAR     = (uint32_t)&TIMx->DMAR;
    s->M0AR    = (uint32_t)dshot_buf;
    s->NDTR    = 18 * 4;
    s->CR      = DMA_SxCR_DIR_0 | DMA_SxCR_MINC |
                 DMA_SxCR_PSIZE_1 | DMA_SxCR_MSIZE_1 |   // 32-bit
                 DMA_SxCR_TCIE | DMA_SxCR_PL_1;
    s->CR     |= DMA_SxCR_EN;

    TIMx->DIER = TIM_DIER_UDE;
    TIMx->EGR  = TIM_EGR_UG;
    TIMx->CR1 |= TIM_CR1_CEN;
}
```

---

## 4. Turnaround: Switch the Channel to Input Capture

In the transmit-complete ISR, the timer is reconfigured to listen for the ESC's reply:

```c
static void dshot_tx_done_isr(TIM_TypeDef *TIMx, uint8_t telem_ch /*0..3*/)
{
    TIMx->CR1  &= ~TIM_CR1_CEN;
    TIMx->DIER  = 0;

    // Free-running coun