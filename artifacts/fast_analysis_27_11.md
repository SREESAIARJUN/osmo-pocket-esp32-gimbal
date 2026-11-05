# ArduPilot Bi-Directional DShot600 on STM32H743 (TIM1 / TIM8): DMA Request Logic and RPM Telemetry Decoding

I'm treating "Cycle 27" as your revision label. This follows the structure of ArduPilot's `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`. Constant names match upstream where possible, but verify them against your branch.

---

## 1. Clock and Timing Basis

| Parameter | Value |
|---|---|
| SYSCLK | 400 MHz (480 MHz on rev V parts) |
| APB2 | 100 MHz |
| TIM1/TIM8 kernel clock | 200 MHz (APB2 × 2) |
| DShot600 bit rate | 600 kbit/s, so T_bit = 1.667 µs |
| Bit width (ticks) | `DSHOT_BIT_WIDTH_TICKS = 8` |
| Bit 0 high time | 3 ticks (37.5 %) |
| Bit 1 high time | 6 ticks (75 %) |

**Output prescaler:**

```
f_tick = 600 kHz × 8 = 4.8 MHz
PSC    = round(200 MHz / 4.8 MHz) − 1 = 41
f_real = 200 MHz / 42 = 4.762 MHz
bitrate_real = 595.2 kHz   (−0.8 %, well inside ESC tolerance)
ARR    = 8 − 1 = 7
```

ArduPilot computes this at runtime:

```c
prescaler = (timer_clock / (rate * DSHOT_BIT_WIDTH_TICKS)) - 1;
```

**Telemetry return** runs at 5/4 × 600 = 750 kbit/s, so a return bit is 1.333 µs. The response:

- starts about 30 µs after the end of the command frame;
- is 21 GCR bits long, so it lasts about 28 µs.

The full bidirectional cycle per frame is roughly 27 µs (TX) + 30 µs (gap) + 28 µs (RX) + margin, about 100 µs. This caps the loop at about 8 kHz in practice. ArduPilot normally runs it at the main loop rate.

---

## 2. DShot Frame Construction (Bidirectional = Inverted CRC)

```c
static inline uint16_t create_dshot_packet(uint16_t value, bool telem_req, bool bidir)
{
    uint16_t packet = (value << 1) | (telem_req ? 1 : 0);   // 11-bit throttle + T bit
    uint16_t csum = (packet ^ (packet >> 4) ^ (packet >> 8));
    if (bidir) {
        csum = ~csum;                                        // bidir: inverted CRC
    }
    return (packet << 4) | (csum & 0x0F);
}
```

Line polarity is inverted: the line idles high and pulses go low. You get this with PWM mode 1 plus `CCxP = 1` (active low), set via `PWM_OUTPUT_ACTIVE_LOW` in the ChibiOS PWM config.

---

## 3. Output DMA: Timer Burst via DMAR (TIM_UP Request)

One UP-event DMA stream per timer writes all four CCRs each bit period, using the DCR/DMAR burst mechanism.

### DMAMUX1 request IDs (RM0433)

| Signal | DMAMUX ID |
|---|---|
| TIM1_CH1 to TIM1_CH4 | 11, 12, 13, 14 |
| TIM1_UP | 15 |
| TIM8_CH1 to TIM8_CH4 | 47, 48, 49, 50 |
| TIM8_UP | 51 |

### Register setup (per timer)

```c
// Burst: base = CCR1, length = 4 transfers
TIMx->DCR  = (STM32_TIM_DCR_DBL(3)) |                 // 4 transfers
             (STM32_TIM_DCR_DBA(offsetof(stm32_tim_t, CCR) / 4)); // CCR1 = 0x34/4 = 13
TIMx->DIER |= STM32_TIM_DIER_UDE;                      // DMA on update
TIMx->BDTR |= STM32_TIM_BDTR_MOE;                      // REQUIRED on TIM1/TIM8
TIMx->RCR   = 0;
TIMx->PSC   = 41;
TIMx->ARR   = 7;
TIMx->EGR   = STM32_TIM_EGR_UG;                        // latch PSC
```

Two points are easy to miss on TIM1 and TIM8:

- **`MOE` must be set.** Without it, the advanced-timer outputs stay disabled and you see a flat line.
- **Complementary (N) outputs need care.** If a motor pin is a CHxN output, the logic inverts. ArduPilot handles this with `CCxNP` and `PWM_COMPLEMENTARY_OUTPUT_ACTIVE_LOW`.

### Buffer layout (interleaved, 4 channels)

```c
#define DSHOT_BUFFER_LENGTH  ((16 + 2) * 4 * sizeof(uint32_t))   // 16 bits + 2 reset slots

static void fill_DMA_buffer_dshot(uint32_t *buf, uint8_t stride, uint16_t packet,
                                  uint16_t clockmul /*=1*/)
{
    const uint32_t b0 = 3 * clockmul, b1 = 6 * clockmul;
    for (uint8_t i = 0; i < 16; i++) {
        buf[i * stride] = (packet & 0x8000) ? b1 : b0;
        packet <<= 1;
    }
    buf[16 * stride] = 0;      // trailing zero slots -> line returns to idle
    buf[17 * stride] = 0;
}
// channel c of timer group: fill_DMA_buffer_dshot(dma_buffer + c, 4, pkt, 1);
```

### DMA stream configuration (ChibiOS `dmaStreamSetMode`)

```c
dmaStreamSetPeripheral(stream, &TIMx->DMAR);
dmaStreamSetMemory0