# Bi-Directional DShot600 on STM32H743 TIM1/TIM8 in ArduPilot

This is an engineering formulation of the logic ArduPilot's `RCOutput` / `RCOutput_bdshot.cpp` (ChibiOS HAL) uses. I don't have context for what "Cycle 15" refers to. The model below is self-contained, so tell me if it should build on a specific prior iteration. Verify constants against your hwdef and the current ArduPilot source, because tick widths and DMA mappings change between releases.

---

## 1. Clocking and Bit Timing

| Item | Value |
|---|---|
| SYSCLK | 400 MHz (typical ArduPilot H743) |
| APB2 timer clock (TIM1, TIM8) | 200 MHz |
| DShot600 bit period | 1/600 kHz = 1.667 µs |
| Ticks per bit (`DSHOT_BIT_WIDTH_TICKS`) | 20 (default) or 8 (low-res mode) |
| Bit-0 high time | 7/20 ticks ≈ 37.5 % |
| Bit-1 high time | 14/20 ticks ≈ 75 % |

The prescaler is chosen so that `timer_clk / (PSC+1) ≈ 600 kHz × 20 = 12 MHz`:

```
PSC = round(200e6 / 12e6) - 1 = 16 - 1 = 15   → 12.5 MHz actual tick
ARR = 20 - 1
```

The ~4 % rate error is within ESC tolerance. ArduPilot computes this in `calculate_bit_prescaler()` and records the actual achieved rate for the telemetry decode.

**Bidirectional mode inverts the line.** The idle level is high, and the frame is sent as active-low pulses. On TIM1/TIM8 you either:
- use PWM mode 2 (`OCxM = 0b111`), or
- set `CCxP` (output polarity inverted).

Because these are advanced timers, `BDTR.MOE` must be set, or the outputs stay disabled.

---

## 2. DShot Frame Construction

```c
static inline uint16_t dshot_frame(uint16_t value11, bool telem_req, bool bidir)
{
    uint16_t pkt = (value11 << 1) | (telem_req ? 1 : 0);
    uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8)) & 0x0F;
    if (bidir) {
        csum = (~csum) & 0x0F;     // inverted CRC signals bidirectional request
    }
    return (pkt << 4) | csum;
}
```

---

## 3. Output DMA: TIMx_UP Burst into CCR1..CCR4

The key structure is one DMA request per timer update, using the DMAR burst register to write all 4 CCRs at once.

### DMAMUX1 request IDs (RM0433)

| Request | TIM1 | TIM8 |
|---|---|---|
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |
| **UP** | **15** | **51** |

### Burst configuration

```c
// DBA = offset of CCR1 / 4 = 0x34/4 = 13 ; DBL = (4 transfers) - 1 = 3
TIMx->DCR  = (3U << TIM_DCR_DBL_Pos) | (13U << TIM_DCR_DBA_Pos);
TIMx->DIER |= TIM_DIER_UDE;              // DMA request on update event
// DMA: mem->periph, PAR = &TIMx->DMAR, MSIZE=PSIZE=32-bit, MINC=1, PINC=0
```

### Buffer layout

The buffer is interleaved per bit: 16 bits plus padding slots, multiplied by 4 channels.

```c
#define DSHOT_BUFFER_LEN  (16 + 2)          // 2 trailing zero slots => line returns idle
static uint32_t dshot_buf[DSHOT_BUFFER_LEN * 4] __attribute__((section(".dma"))); // non-cacheable / D-cache cleaned

void fill_buffer(uint32_t *buf, uint8_t ch, uint16_t frame, uint16_t clk_per_bit)
{
    const uint32_t b0 = clk_per_bit * 7 / 20;
    const uint32_t b1 = clk_per_bit * 14 / 20;
    for (int i = 0; i < 16; i++) {
        buf[i * 4 + ch] = (frame & 0x8000) ? b1 : b0;
        frame <<= 1;
    }
    buf[16 * 4 + ch] = 0;
    buf[17 * 4 + ch] = 0;
}
```

On the H7, the buffer must be in DMA-capable SRAM (AXI SRAM, or SRAM1–3 for DMA1/2). D-cache must be cleaned before the transfer, or the buffer placed in a non-cacheable MPU region. ArduPilot handles this with `stm32_cacheBufferFlush()`.

---

## 4. Bidirectional Turnaround: Output → Input Capture

The per-cycle state machine for each timer group is:

```
 ┌──────────────┐ DMA TC  ┌──────────────────┐ ~25-30µs  ┌──────────────────┐
 │ SEND (TIM_UP)│────────▶│ SWITCH TO INPUT  │──────────▶│ CAPTURE (TIM_CHx)│
 └──────────────┘         │ - stop timer     │  ESC resp │ DMA edges ->     │
        ▲                 │ - CCMR -> ICx    │           │ capture buffer   │
        │                 │ - realloc DMA    │           └────────┬─────────┘
        │                 └──────────────────┘                    │ timeout / TC
        │            ┌──────────────────────────┐                 │
        └────────────│ DECODE + restore output  │◀────────────────┘
                     └──────────────────────────┘
```

### Turnaround steps (ISR on output DMA complete)

1. Stop the timer (`CR1.CEN = 0`). Wait about one bit period so the last pulse completes.
2. Reconfigure the channel:
   - `CCMRx.CCxS = 01` (ICx mapped on TIx).
   - `CCER`: capture on both edges (`CCxP = 1, CCxNP = 1`).
   - Input filter `ICxF` small (for example 0–2).
3. Switch the GPIO from AF output to AF input with pull-up, since the line idles high.
4. Set `PSC = 0` (200 MHz capture resolution) or a modest prescaler, and `ARR = 0xFFFF`.
5. Point a DMA stream at DMAMUX request `TIMx_CHn` with `PAR = &TIMx->CCRn` and 16-bit transfers into `uint16_t capture_buf[]`.
6. Enable `DIER.CCnDE`, then `CEN = 1`.
7. Arm a timeout (a virtual timer or the next loop tick) of about 100 µs.

### DMA stream scarcity

TIM1 plus TIM8 means 8 channels. Dedicated UP and CH streams for all of them would need 10 streams, which is more than the H7's 16 streams allow once other peripherals take theirs. ArduPilot handles this as follows:
- It shares a stream between UP and CHx and re-allocates on each turnaround (`Shared_DMA` locking).
- If channels lack a dedicated capture stream, it **