# Bi-directional DShot600 on STM32H743 TIM1/TIM8 in ArduPilot

This covers the DMA timer request logic and RPM telemetry decoding, modeled on `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`. I don't know what "Cycle 23" refers to. It isn't an ArduPilot or STM32 term I recognize, so I've treated it as your iteration label. If it means a specific branch, PR, or hardware revision, say so and I'll adapt this.

---

## 1. Clock and Bit Timing

| Item | Value (H743 @ 400 MHz SYSCLK) |
|---|---|
| APB2 clock (TIM1/TIM8 bus) | 100 MHz |
| Timer kernel clock (APB2 ×2) | **200 MHz** |
| DShot600 bit period | 1/600 kHz = **1.6667 µs** |
| Ticks per bit (ArduPilot default) | `DSHOT_BIT_WIDTH_TICKS = 8` |
| Required timer tick rate | 600 kHz × 8 = 4.8 MHz |
| Prescaler | round(200 MHz / 4.8 MHz) − 1 = **41 − 1 = 40** |
| Actual bit rate | 200 MHz / 41 / 8 = 609.8 kHz (+1.6%, inside ESC tolerance) |
| T0H / T1H duty (CCR) | 3 ticks (37.5%) / 6 ticks (75%) |

The prescaler is computed at runtime rather than hardcoded:

```cpp
// RCOutput::set_group_mode(), simplified
const uint32_t target_freq = bitrate * group.bit_width_mul * DSHOT_BIT_WIDTH_TICKS;
uint32_t prescaler = (group.pwm_drv->clock + target_freq/2) / target_freq;   // rounded
group.pwm_cfg.frequency = group.pwm_drv->clock / prescaler;
group.pwm_cfg.period    = DSHOT_BIT_WIDTH_TICKS;   // ARR+1
```

**Bidirectional mode inverts the line.** Idle is high and bits are low pulses. Set the polarity so a "1" is a long low pulse:

```cpp
group.pwm_cfg.channels[i].mode = PWM_OUTPUT_ACTIVE_LOW;   // CCxP = 1
```

---

## 2. Output Phase: DMA Burst via `TIMx_UP`

All four channels of a timer are updated from a single DMA stream. The stream uses the timer's DMA burst feature (`DCR`/`DMAR`), triggered by the **update event**.

### DMAMUX1 request IDs (RM0433 Table 121)

| Request | ID | Request | ID |
|---|---|---|---|
| TIM1_CH1 | 11 | TIM8_CH1 | 47 |
| TIM1_CH2 | 12 | TIM8_CH2 | 48 |
| TIM1_CH3 | 13 | TIM8_CH3 | 49 |
| TIM1_CH4 | 14 | TIM8_CH4 | 50 |
| **TIM1_UP** | **15** | **TIM8_UP** | **51** |

### Burst configuration

```cpp
// DBA = offset of CCR1 / 4 = 0x34/4 = 13,  DBL = (4 transfers) - 1 = 3
timer->DCR  = (13U << TIM_DCR_DBA_Pos) | (3U << TIM_DCR_DBL_Pos);
timer->DIER |= TIM_DIER_UDE;          // update event -> DMA request

// DMA buffer layout: interleaved, one uint32 per channel per bit
//   [bit0:CH1,CH2,CH3,CH4][bit1:CH1..CH4] ... [bit17:CH1..CH4]
// 16 data bits + 2 trailing zero slots (line returns to idle, resets ESC decoder)
#define DSHOT_BUFFER_LENGTH  ((16 + 2) * 4 * sizeof(uint32_t))   // 288 bytes

dmaStreamSetPeripheral(dma, &timer->DMAR);
dmaStreamSetMemory0(dma, group.dma_buffer);       // must be in AXI/SRAM1-3, NOT DTCM
dmaStreamSetTransactionSize(dma, 18 * 4);
dmaStreamSetMode(dma,
    STM32_DMA_CR_CHSEL(group.dma_up_channel) |
    STM32_DMA_CR_DIR_M2P | STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
    STM32_DMA_CR_MINC | STM32_DMA_CR_PL(3) |
    STM32_DMA_CR_TEIE | STM32_DMA_CR_TCIE);
```

### Packet encoding (inverted CRC for bidir)

```cpp
uint16_t create_dshot_packet(uint16_t value, bool telem_req, bool bidir) {
    uint16_t pkt = (value << 1) | (telem_req ? 1 : 0);
    uint16_t csum = pkt ^ (pkt >> 4) ^ (pkt >> 8);
    if (bidir) csum = ~csum;                   // signals ESC to reply on same wire
    return (pkt << 4) | (csum & 0xF);
}

void fill_DMA_buffer_dshot(uint32_t *buf, uint8_t stride, uint16_t packet, uint16_t clockmul) {
    const uint32_t b0 = 3 * clockmul, b1 = 6 * clockmul;
    for (int i = 0; i < 16; i++)
        buf[i * stride] = (packet & (0x8000 >> i)) ? b1 : b0;
    buf[16 * stride] = buf[17 * stride] = 0;   // trailing idle
}
```

Before starting