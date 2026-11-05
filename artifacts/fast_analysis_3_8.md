# ArduPilot Bi-Directional DShot600 on STM32H743: TIM1/TIM8 DMA Logic and RPM Decoding

I've read "Cycle 3" as the third refinement pass: full send, capture and decode logic consolidated for the two advanced timers. If it refers to a specific document revision in your project, map the sections below onto it.

---

## 1. Clock and Timing Basis

| Parameter | Value |
|---|---|
| SYSCLK (ArduPilot H743 default) | 400 MHz |
| APB2 | 100 MHz, so the TIM1/TIM8 kernel clock is **200 MHz** (×2 multiplier) |
| DShot600 bit rate | 600 kbit/s, giving 1.667 µs per bit |
| ArduPilot bit width | `DSHOT_BIT_WIDTH_TICKS = 8` (default) |
| Timer tick target | 600 k × 8 = 4.8 MHz |
| PSC | `round(200 MHz / 4.8 MHz) − 1 = 41` (≈4.76 MHz actual, which is within DShot tolerance) |
| ARR | 7 (8 ticks per bit) |
| Bit 0 / bit 1 CCR | 3 / 6 ticks (37.5 % / 75 % duty) |
| Bidir polarity | **Inverted** (idle high, active low). Set `CCxP = 1` / PWM mode 2 equivalent. |

**Telemetry return:**
- Speed: 5/4 × 600 k = **750 kbit/s**, which is 1.333 µs per GCR bit.
- Start time: about 30 µs after the end of the command frame.
- Length: 21 bits, giving 20 data bits after the start bit.

---

## 2. Output Phase: Burst DMA on the Update Event

All four channels of one timer are driven by one DMA stream. It writes `CCR1…CCR4` per bit through the DMA burst register.

### DMAMUX1 request IDs (RM0433)

| Signal | TIM1 | TIM8 |
|---|---|---|
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |
| **UP** (burst output) | **15** | **51** |
| TRIG | 16 | 52 |
| COM | 17 | 53 |

### Output configuration

```c
// Buffer: 16 data bits + 2 zero pads (forces line idle and resets compare at frame end)
#define DSHOT_BIT_LEN        18
#define DSHOT_BUFFER_LENGTH  (DSHOT_BIT_LEN * 4 * sizeof(uint32_t))  // 4 channels interleaved

void dshot_send_prepare(TIM_TypeDef *tim, DMA_Stream_TypeDef *s, uint8_t up_req,
                        uint32_t *buf)
{
    tim->CR1   = 0;
    tim->PSC   = 41;
    tim->ARR   = 7;
    tim->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE |
                 (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    tim->CCMR2 = (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
                 (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    tim->CCER  = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC2E | TIM_CCER_CC2P |
                 TIM_CCER_CC3E | TIM_CCER_CC3P | TIM_CCER_CC4E | TIM_CCER_CC4P; // inverted
    tim->BDTR  = TIM_BDTR_MOE;           // required on advanced timers TIM1/TIM8

    // DMA burst: base = CCR1 (offset 13 words from CR1), length = 4 transfers
    tim->DCR   = (3u << TIM_DCR_DBL_Pos) | (13u << TIM_DCR_DBA_Pos);
    tim->DIER  = TIM_DIER_UDE;           // DMA request on update

    DMAMUX1_Channel[stream_idx(s)]->CCR = up_req;  // 15 (TIM1_UP) or 51 (TIM8_UP)
    s->PAR  = (uint32_t)&tim->DMAR;
    s->M0AR = (uint32_t)buf;             // must reside in DMA-capable RAM (AXI SRAM / SRAM1-3, not DTCM)
    s->NDTR = DSHOT_BIT_LEN * 4;
    s->CR   = DMA_SxCR_DIR_0 | DMA_SxCR_MINC |
              (2u << DMA_SxCR_PSIZE_Pos) | (2u << DMA_SxCR_MSIZE_Pos) |
              DMA_SxCR_TCIE;
    SCB_CleanDCache_by_Addr(buf, DSHOT_BUFFER_LENGTH);  // H7 cache coherency

    tim->EGR = TIM_EGR_UG;
    s->CR   |= DMA_SxCR_EN;
    tim->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}
```

### Frame encoding (bidir checksum is inverted)

```c
uint16_t dshot_frame(uint16_t value11, bool telem_req)
{
    uint16_t pkt = (value11 << 1) | telem_req;
    uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8)) & 0xF;
    csum = ~csum & 0xF;                 // bidirectional: inverted CRC
    return (pkt << 4) | csum;
}

void fill_buffer(uint32_t *buf, uint16_t frame, uint8_t chan)
{
    for (int i = 0; i < 16; i++)
        buf[i*4 + chan] = (frame & (0x8000 >> i)) ? 6 : 3;
    buf[16*4 + chan] = buf[17*4 + chan] = 0;
}
```

---

## 3. Turnaround: Switching Output to Input Capture

The switch happens on DMA TC of the send stream. ArduPilot does this in `dma_up_irq_callback` / `bdshot_receive_pulses_DMAR`.

1. Stop the timer and disable the UP DMA (`UDE = 0`).
2. Reconfigure each channel pin as **input capture on both edges**:
   - Set `CCxS = 01` (TIx).
   - Set `CCxP | CCxNP = 1` (both edges).
   - Set the filter `ICxF` small (0–2).
3. Re-time the capture clock for resolution. ArduPilot runs the capture timer fast, with `PSC = 0` or 1 and `ARR = 0xFFFF`, so one 750 kbit/s bit spans enough ticks for rob