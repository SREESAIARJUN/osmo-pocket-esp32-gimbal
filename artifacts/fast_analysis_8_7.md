# Bidirectional DShot600 on STM32H743 TIM1/TIM8: DMA Request Logic and eRPM Decoding

This design follows the structure of ArduPilot's `AP_HAL_ChibiOS/RCOutput_bdshot.cpp`. It is a reference implementation, not a verbatim copy of the ArduPilot source. "Cycle 8" is not a standard ArduPilot term, so this treats it as one design iteration.

---

## 1. Timing Budget (DShot600, Bidirectional)

| Parameter | Value |
|---|---|
| Bit rate | 600 kbit/s → bit period 1.667 µs |
| Frame | 16 bits → 26.7 µs, plus reset/idle gap |
| Signal polarity | Inverted: idle is **high**, so the ESC knows to reply |
| ESC turnaround | ~30 µs after the frame ends |
| Telemetry rate | 5/4 × 600k = **750 kbit/s** (1.333 µs per bit) |
| Telemetry length | 21 bits, GCR-encoded (~28 µs) |
| Minimum cycle | ~90–100 µs (send, then switch to input capture, then decode) |

**Timer clock:**
- H743 at 400 MHz: APB2 runs at 100 MHz, so TIM1/TIM8 run at **200 MHz**.
- On 480 MHz rev V parts, APB2 runs at 120 MHz, so the timer clock is 240 MHz.

---

## 2. Hardware Mapping (DMAMUX1 Request IDs, RM0433)

| Timer | UP (burst output) | CH1 | CH2 | CH3 | CH4 |
|---|---|---|---|---|---|
| TIM1 | **15** | 11 | 12 | 13 | 14 |
| TIM8 | **51** | 47 | 48 | 49 | 50 |

- **Output phase:** a single `TIMx_UP` DMA stream in **DMA burst mode** (via `TIMx_DCR`/`TIMx_DMAR`) writes CCR1–CCR4 on every update event. All four motors on a timer are therefore driven by one stream.
- **Input phase:** each channel needs its own `TIMx_CHy` DMA request to capture edge timestamps. If you run short of streams, ArduPilot captures one channel per cycle and round-robins through them.

Example hwdef entries:

```
PE9  TIM1_CH1 TIM1 PWM(1) GPIO(50) BIDIR
PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PE14 TIM1_CH4 TIM1 PWM(4) GPIO(53)
PC6  TIM8_CH1 TIM8 PWM(5) GPIO(54) BIDIR
...
```

In ArduPilot, `BIDIR` marks the channel pairs that get input-capture DMA.

**H7 DMA memory caveat:** DMA1/DMA2 cannot reach DTCM. All DShot buffers must live in AXI-SRAM or SRAM1–3. They must also be either non-cacheable (MPU region) or explicitly cleaned and invalidated.

---

## 3. Output Phase: Frame Construction and Burst DMA

```c
#define DSHOT_BIT_WIDTH_TICKS  8
#define DSHOT_BIT_0_TICKS      3   // ~37.5% duty
#define DSHOT_BIT_1_TICKS      6   // ~75% duty
#define DSHOT_BUFFER_LEN       (16 + 2)   // 16 bits + 2 zero slots for reset

// Prescaler: 200 MHz / (600k * 8) = 41.67 -> PSC = 41 (ArduPilot tolerates the
// ~1.6% error; ESCs accept roughly ±5%)
static inline uint16_t dshot_psc(uint32_t tclk, uint32_t bitrate) {
    return (uint16_t)((tclk + (bitrate*DSHOT_BIT_WIDTH_TICKS)/2) /
                      (bitrate*DSHOT_BIT_WIDTH_TICKS)) - 1;
}

// 11-bit throttle, 1-bit telemetry request, 4-bit CRC (inverted for bidir)
static uint16_t dshot_packet(uint16_t value, bool telem, bool bidir) {
    uint16_t pkt = (value << 1) | (telem ? 1 : 0);
    uint16_t csum = pkt ^ (pkt >> 4) ^ (pkt >> 8);
    if (bidir) csum = ~csum;
    return (pkt << 4) | (csum & 0x0F);
}

// Interleaved buffer: [bit][channel] so the UP burst writes CCR1..CCR4 per bit
__attribute__((section(".axisram_nocache")))
static uint32_t dma_buf_tim1[DSHOT_BUFFER_LEN * 4];

static void fill_frame(uint32_t *buf, uint8_t ch, uint16_t pkt) {
    for (int i = 0; i < 16; i++) {
        buf[i*4 + ch] = (pkt & 0x8000) ? DSHOT_BIT_1_TICKS : DSHOT_BIT_0_TICKS;
        pkt <<= 1;
    }
    buf[16*4 + ch] = 0;
    buf[17*4 + ch] = 0;
}
```

### Timer and DMA Setup for Burst Output

```c
static void tim_output_start(TIM_TypeDef *tim, DMA_Stream_TypeDef *s,
                             uint8_t dmamux_req, uint32_t *buf, uint16_t psc)
{
    tim->CR1  = 0;
    tim->PSC  = psc;
    tim->ARR  = DSHOT_BIT_WIDTH_TICKS - 1;

    // PWM mode 1 on CH1..4, preload enabled
    tim->CCMR1 = (6<<TIM_CCMR1_OC1M_Pos)|TIM_CCMR1_OC1PE |
                 (6<<TIM_CCMR1_OC2M_Pos)|TIM_CCMR1_OC2PE;
    tim->CCMR2 = (6<<TIM_CCMR2_OC3M_P