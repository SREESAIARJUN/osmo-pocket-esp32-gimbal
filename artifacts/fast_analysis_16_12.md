# ArduPilot Bi-Directional DShot600 on STM32H743 TIM1/TIM8 with RPM Telemetry Decoding

This describes how ArduPilot's ChibiOS `RCOutput` bdshot path is structured on STM32H743 TIM1 and TIM8.

I couldn't match "Cycle 16" to a named ArduPilot construct. I've read it as one 16-bit DShot frame-and-telemetry cycle per timer group. Tell me if you meant a specific scheduler slot or release.

---

## 1. Clock and Timing Basis

| Parameter | Value |
|---|---|
| SYSCLK / HCLK | 400 MHz / 200 MHz (ArduPilot H743 default) |
| APB2 | 100 MHz, so the timer clock is ×2 = **200 MHz** (TIM1 and TIM8 are both on APB2) |
| DShot600 bit time | 1 / 600 kHz = **1.667 µs** |
| Frame (16 bits) | 26.67 µs |
| Telemetry bit rate | 600 × 5/4 = **750 kbit/s**, which is 1.333 µs per bit |
| Telemetry length | 21 GCR bits, about 28 µs |
| ESC turnaround | about 30 µs after the frame ends |

**Output tick computation.** ArduPilot targets a bit width of N ticks, with N = 20 used for bdshot on fast timers. It then rounds the prescaler and recomputes the tick counts from the achieved rate:

```
target_clk = 600000 * DSHOT_BIT_WIDTH_TICKS        // 600k * 20 = 12 MHz
PSC        = (200e6 / target_clk) - 1   ≈ 15.67 → 16 - 1 = 15
tick_clk   = 200e6 / 16 = 12.5 MHz
ARR        = round(tick_clk / 600000) - 1 = 20 - 1     // bit width 20 ticks
T0H        = round(0.375 * 20) = 7  ticks  (~0.56 µs)
T1H        = round(0.75  * 20) = 15 ticks  (~1.20 µs)
```

**Bidirectional inversion.** The line idles high, so the output polarity is inverted. ArduPilot sets `CCxP` (or uses PWM mode 2), so the active portion of each bit is driven low.

---

## 2. Request Frame Construction

The frame is 11 bits of throttle, 1 telemetry-request bit, and a 4-bit CRC. For bidirectional mode the CRC is inverted:

```c
static inline uint16_t dshot_frame(uint16_t value, bool telem_req) {
    uint16_t pkt = (value << 1) | (telem_req ? 1 : 0);   // 12 bits
    uint16_t csum = pkt ^ (pkt >> 4) ^ (pkt >> 8);
    csum = ~csum;                                       // BDShot: inverted CRC
    return (pkt << 4) | (csum & 0x0F);
}
```

---

## 3. DMA Burst Output (UP request, DMAR)

All four channels of a timer are fed by a single update-event DMA stream through the burst register `TIMx_DMAR`.

```c
// DCR: base address = CCR1, burst length = 4 transfers
TIMx->DCR = (4 - 1) << TIM_DCR_DBL_Pos      // DBL = 3
          | (0x34 / 4) << TIM_DCR_DBA_Pos;  // DBA = 13 (CCR1 offset 0x34)
TIMx->DIER |= TIM_DIER_UDE;                 // DMA on update event
```

**Buffer layout.** The buffer has 16 data bits plus 2 trailing zero slots, so the line returns to idle cleanly. Each slot holds 4 channels as 32-bit words:

```c
// buf[(bit * 4) + chan] = (frame & (0x8000 >> bit)) ? T1H : T0H;
// last 2 slots = 0 → output stays at idle (high, due to inversion)
uint32_t dma_buffer[(16 + 2) * 4];
```

**DMAMUX1 request IDs (H743):**

| Request | TIM1 | TIM8 |
|---|---|---|
| UP | 15 | 51 |
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |

Configure the UP stream as follows:
- Memory to peripheral, with the peripheral address `&TIMx->DMAR`.
- Word-sized transfers, with memory increment enabled.
- Transfer count of `(16+2)*4`.

---

## 4. Switching to Input Capture

The sequence on the UP-DMA transfer-complete callback (ISR) is:

1. **Stop the output.** Clear `CEN` and `UDE`, then release the UP stream if it is shared.

2. **Reconfigure each bdshot channel to input:**
   ```c
   CCMRx: CCxS = 01            // IC mapped on TIx
   CCER : CCxP | CCxNP = 1     // capture on both edges
   ICxF = small filter (e.g. 0b0010)
   PSC  = 0 or small, so capture resolution is ≪ 1.333 µs
          (ArduPilot uses a higher-rate tick; e.g. 200 MHz / (PSC+1))
   ARR  = 0xFFFF
   ```

3. **Arm the CCx DMA stream.** Use a peripheral-to-memory, half-word transfer from `&TIMx->CCRx` into the capture buffer. Size the buffer at about 32 edges, since 21 bits produce at most about 22 transitions. Enable `CCxDE`.

4. **Arm a timeout.** ArduPilot uses a virtual-timer or ChibiOS `chVTSet` of about 30 + 28 µs plus margin. The capture completes on D