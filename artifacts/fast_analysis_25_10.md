# ArduPilot Bi-Directional DShot600 on STM32H743 (TIM1 / TIM8)

This covers DMA request routing, timer reconfiguration, the output and input-capture phases, and RPM telemetry decoding. It follows the structure of `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`, simplified for clarity. "Cycle 25" is treated as an iteration label. It does not change the logic.

---

## 1. Clock and Timing Basis

| Parameter | Value |
|---|---|
| SYSCLK / HCLK | 400 MHz (ArduPilot H743 default) |
| APB2 timer clock (TIM1, TIM8) | 200 MHz |
| DShot600 bit period | 1.6667 µs |
| Telemetry bitrate (5/4 × DShot rate) | 750 kbit/s, so 1.333 µs per bit |
| ESC response turnaround | about 30 µs after frame end |
| Response length | 21 bits (start bit plus 20 GCR bits) |

ArduPilot picks a prescaler so that one DShot bit equals `DSHOT_BIT_WIDTH_TICKS` timer ticks:

```cpp
// RCOutput::set_group_mode() — DShot600
const uint32_t target_rate = 600000 * DSHOT_BIT_WIDTH_TICKS;      // e.g. 20 ticks/bit
uint32_t psc = (timer_clock + target_rate/2) / target_rate - 1;   // rounded
// Actual bitrate error of a few % is acceptable to ESCs
group.bit_width_mul = ...;                // scaled for actual clock
// Duty values (inverted output for bidir):
//   BIT_0 ≈ 37.5% of bit width, BIT_1 ≈ 75% of bit width
```

---

## 2. DMAMUX Request IDs (RM0433, DMAMUX1)

| Signal | TIM1 | TIM8 |
|---|---|---|
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |
| **UP** (DMAR burst, output) | **15** | **51** |

The streams have two roles:

- **Output phase:** one DMA stream bound to `TIMx_UP`. It does a burst write through `TIMx->DMAR` into CCR1..CCR4.
- **Input phase:** a stream bound to `TIMx_CHn`. It reads `TIMx->CCRn` on each captured edge.

In hwdef this looks like:

```
PE9  TIM1_CH1 TIM1 PWM(1) GPIO(50) BIDIR
PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PE14 TIM1_CH4 TIM1 PWM(4) GPIO(53)
PC6  TIM8_CH1 TIM8 PWM(5) GPIO(54) BIDIR
...
DMA_PRIORITY TIM1_UP* TIM8_UP*
```

On a timer, `BIDIR` marks the channel(s) that get a dedicated CH DMA stream. Without one per channel, ArduPilot round-robins input capture across channels. In that case each channel's RPM is refreshed every N frames.

---

## 3. Output Phase: Burst DMA via DMAR

### Buffer layout (interleaved, 4 channels × 16 bits + reset padding)

```cpp
// dma_buffer_len = (16 + DSHOT_POST_PAD) * 4 * sizeof(uint32_t)
// buffer[bit*4 + ch] = CCR value for channel ch at bit index
void RCOutput::fill_DMA_buffer_dshot(uint32_t *buffer, uint8_t stride,
                                     uint16_t packet, uint16_t clockmul)
{
    const uint32_t b0 = DSHOT_MOTOR_BIT_0 * clockmul;
    const uint32_t b1 = DSHOT_MOTOR_BIT_1 * clockmul;
    for (uint8_t i = 0; i < 16; i++) {
        buffer[i * stride] = (packet & 0x8000) ? b1 : b0;
        packet <<= 1;
    }
    // trailing zeros already present → line returns to idle (high, inverted)
}
```

### Packet construction (bidirectional uses inverted CRC)

```cpp
uint16_t RCOutput::create_dshot_packet(uint16_t value, bool telem_request, bool bidir)
{
    uint16_t packet = (value << 1) | (telem_request ? 1 : 0);
    uint16_t csum = 0, v = packet;
    for (uint8_t i = 0; i < 3; i++) { csum ^= v; v >>= 4; }
    if (bidir) csum = ~csum;          // ESC recognises bidir mode by inverted CRC
    return (packet << 4) | (csum & 0xF);
}
```

### Timer and DMA setup per frame

```cpp
void RCOutput::send_pulses_DMAR(pwm_group &group, uint32_t buffer_length)
{
    stm32_tim_t *TIMx = group.pwm_drv->tim;

    // H7: D-cache coherency — buffer lives in DMA-capable SRAM (not DTCM)
    stm32_cacheBufferFlush(group.dma_buffer, buffer_length);

    dmaStreamSetPeripheral(group.dma, &TIMx->DMAR);
    dmaStreamSetMemory0(group.dma, group.dma_buffer);
    dmaStreamSetTransactionSize(group.dma, buffer_length / sizeof(uint32_t));
    dmaStreamSetFIFO(group.dma, STM32_DMA_FCR_DMDIS | STM32_DMA_FCR_FTH_FULL);
    dmaStreamSetMode(group.dma,
        STM32_DMA_CR_CHSEL(group.dma_up_channel) |   // DMAMUX req 15 / 51
        STM32_DMA_CR_DIR_M2P | STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
        STM32_DMA_CR_MINC | STM32_DMA_CR_PL(3) |
        STM32_DMA_CR_TEIE | STM32_DMA_CR_TCIE);

    // DCR: base = CCR1 offset (0x34/4 = 13), burst length = 4 transfers
    TIMx->DCR  = STM32_TIM_DCR_DBA(0x0D) | STM32_TIM_DCR_DBL(3);
    TIMx->CCER |= bidir_inverted_polarity_mask;   // CCx