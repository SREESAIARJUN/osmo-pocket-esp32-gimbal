# Bi-directional DShot600 on STM32H743 TIM1/TIM8 (ArduPilot-style)

This design follows the structure of ArduPilot's `RCOutput.cpp` and `RCOutput_bdshot.cpp`. "Cycle 6" is treated here as one complete output → turnaround → capture → decode pass, which runs every scheduler loop. Register values target the H743 at 400 MHz SYSCLK, where the APB2 timer clock (TIM1/TIM8) is 200 MHz.

---

## 1. Timing Budget (DShot600 Bidirectional)

| Phase | Value |
|---|---|
| Output bit period | 1/600 kHz = **1.667 µs** |
| Frame (16 bits + 2 reset slots) | 18 × 1.667 = **30.0 µs** |
| ESC turnaround | ~**30 µs** (nominal, ±5 µs) |
| Telemetry bitrate | 5/4 × 600 k = **750 kbit/s** (1.333 µs/bit) |
| Telemetry frame | 21 GCR bits ≈ **28 µs** |
| Total cycle | ≈ **95–100 µs** per timer group |

Bidirectional mode **inverts** the line. It idles high and pulses active-low. The frame checksum is also inverted.

---

## 2. Timer Configuration – Output Phase

```c
// TIM1 / TIM8, kernel clock 200 MHz
PSC  = 0;                 // 200 MHz tick (5 ns)
ARR  = 332;               // 333 ticks -> 600.6 kHz (0.1% error)
BIT0 = 125;               // 37.5% duty  (0.625 µs)
BIT1 = 250;               // 75%   duty  (1.25 µs)

CCMRx: OCxM = 0b0110 (PWM mode 1), OCxPE = 1
CCER : CCxE = 1, CCxP = 1   // ACTIVE-LOW -> inverted bidir signalling
BDTR : MOE = 1              // mandatory on advanced timers TIM1/TIM8
DIER : UDE = 1              // update event triggers DMA
DCR  : DBA = 13 (CCR1 offset 0x34/4), DBL = 3  // 4-transfer burst CCR1..CCR4
```

**DMA burst write:** each update event pushes 4 half-words to `TIMx->DMAR`. The hardware scatters them into CCR1–CCR4. The buffer is interleaved as `[bit][channel]`:

```c
// 18 slots x 4 channels, must live in DMA-reachable non-cacheable RAM
__attribute__((section(".sram_d2"), aligned(32)))
static uint32_t dshot_buf[18 * 4];   // last 2 slots = 0 (line held idle)
```

### DMAMUX1 request IDs (H743)

| Request | ID | Request | ID |
|---|---|---|---|
| TIM1_CH1 | 11 | TIM8_CH1 | 47 |
| TIM1_CH2 | 12 | TIM8_CH2 | 48 |
| TIM1_CH3 | 13 | TIM8_CH3 | 49 |
| TIM1_CH4 | 14 | TIM8_CH4 | 50 |
| **TIM1_UP** | **15** | **TIM8_UP** | **51** |

The output phase uses `TIMx_UP`. The capture phase uses `TIMx_CHn`.

---

## 3. Frame Encoding

```c
static inline uint16_t dshot_pack_bidir(uint16_t value, bool telem_req)
{
    uint16_t pkt = (value << 1) | (telem_req ? 1 : 0);
    uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8)) & 0x0F;
    csum = ~csum & 0x0F;                       // inverted for bidirectional
    return (pkt << 4) | csum;
}

static void fill_dshot_buffer(uint32_t *buf, uint8_t chan, uint16_t pkt)
{
    for (uint8_t i = 0; i < 16; i++) {
        buf[i * 4 + chan] = (pkt & 0x8000) ? BIT1 : BIT0;
        pkt <<= 1;
    }
    buf[16 * 4 + chan] = 0;
    buf[17 * 4 + chan] = 0;
}
```

---

## 4. Turnaround: Output → Input Capture

After the UP DMA transfer completes (TC ISR, or a chibios virtual timer in ArduPilot), the group switches to capture:

```c
void bdshot_switch_to_capture(pwm_group &g)
{
    TIM_TypeDef *t = g.timer;
    t->CR1  &= ~TIM_CR1_CEN;
    t->DIER  = 0;
    t->CCER  = 0;                               // disable all CCx before CCMR edit

    // Capture timebase: PSC=9 -> 20 MHz (50 ns), 1 telem bit ≈ 26.7 ticks
    t->PSC   = 9;
    t->ARR   = 0xFFFF;

    // CHn as input, mapped to TIn, filter 2 to reject ringing
    set_ccmr_input(t, g.bdshot.curr_chan, /*CCxS=*/1, /*ICxF=*/2);

    // Capture BOTH edges: CCxP=1, CCxNP=1
    t->CCER  = (TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1N