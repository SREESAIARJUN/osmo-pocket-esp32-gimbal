# Bidirectional DShot600 on STM32H743 TIM1/TIM8: DMA Request Logic and RPM Telemetry Decoding

This follows the structure of ArduPilot's `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`. The code is a reconstruction of the logic, not a verbatim copy of the ArduPilot source. I don't know what "Cycle 24" refers to. If it is a step in a specific course or design document, say which constraints it adds and I'll adapt this.

---

## 1. Protocol Constants (DShot600, bidirectional)

| Parameter | Value |
|---|---|
| Outbound bit rate | 600 kbit/s, bit period ≈ 1.667 µs |
| Frame | 16 bits: 11-bit throttle, 1 telemetry-request bit, 4-bit CRC |
| Line polarity (bidir) | Inverted: idle **high**, pulses low |
| Bidir CRC | `crc = (~(v ^ (v>>4) ^ (v>>8))) & 0x0F` |
| ESC reply | Starts about 30 µs after the frame ends; 21 bits GCR at 5/4 × bit rate = **750 kbit/s** (≈1.333 µs/bit) |
| Reply payload | 16 bits: `eee mmmmmmmmm cccc` (exponent, mantissa, CRC) |

---

## 2. Timer Clocking on H743

TIM1 and TIM8 sit on APB2. With the usual ArduPilot H743 configuration (SYSCLK 400 MHz, APB2 100 MHz, timer ×2), the timer kernel clock is **200 MHz**.

ArduPilot encodes each bit in 20 ticks:

```c
#define DSHOT_BIT_WIDTH_TICKS 20
#define DSHOT_BIT_0_TICKS      7   // ~35% duty
#define DSHOT_BIT_1_TICKS     14   // ~70% duty
```

The prescaler and resulting bit rate follow from these:

```c
target_freq = 600000 * DSHOT_BIT_WIDTH_TICKS;          // 12 MHz
psc         = (timer_clock + target_freq/2) / target_freq - 1;  // 200M/12M → 16
// actual tick = 200 MHz / 17 = 11.76 MHz → 588 kbit/s (-2%, within ESC tolerance)
TIMx->PSC = psc;
TIMx->ARR = DSHOT_BIT_WIDTH_TICKS - 1;
```

The output channels are configured in PWM mode 1. Polarity is inverted for bidirectional operation (`CCxP = 1`), so the line idles high.

---

## 3. Outbound DMA: TIMx_UP Burst into CCR1..CCR4

All four channels are driven from **one** DMA stream, triggered by the update event. The DMA writes through `DMAR` using the timer's DMA burst mode.

### DMAMUX1 request IDs (H743)

| Request | ID | Request | ID |
|---|---|---|---|
| TIM1_CH1 | 11 | TIM8_CH1 | 47 |
| TIM1_CH2 | 12 | TIM8_CH2 | 48 |
| TIM1_CH3 | 13 | TIM8_CH3 | 49 |
| TIM1_CH4 | 14 | TIM8_CH4 | 50 |
| **TIM1_UP** | **15** | **TIM8_UP** | **51** |

### Burst configuration

```c
// DBA = offset of CCR1 in 32-bit words = 0x34/4 = 13
// DBL = (number of channels - 1) = 3 → 4 transfers per update event
TIMx->DCR  = (3 << TIM_DCR_DBL_Pos) | (13 << TIM_DCR_DBA_Pos);
TIMx->DIER = TIM_DIER_UDE;
```

### DMA stream setup

```c
dmaStreamSetPeripheral(stream, &TIMx->DMAR);
dmaStreamSetMemory0(stream, dma_buffer);
dmaStreamSetTransactionSize(stream, (16 + 2) * 4);   // bits + 2 idle slots, x4 channels
dmaStreamSetMode(stream,
    STM32_DMA_CR_CHSEL(0) | STM32_DMA_CR_DIR_M2P |
    STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
    STM32_DMA_CR_MINC | STM32_DMA_CR_PL(3) |
    STM32_DMA_CR_TEIE | STM32_DMA_CR_TCIE);
dmaSetRequestSource(stream, is_tim1 ? 15 : 51);      // DMAMUX
```

The buffer must be in DMA-capable SRAM (AXI SRAM or SRAM1–3), not DTCM. On the H7 it also needs cache maintenance before the transfer, using `stm32_cacheBufferFlush()`.

### Buffer packing (interleaved)

```c
void fill_dshot(uint32_t *buf, uint8_t chan, uint16_t value, bool telem) {
    uint16_t pkt = (value << 1) | telem;
    uint16_t crc = (~(pkt ^ (pkt >> 4) ^ (pkt >> 8))) & 0x0F;   // bidir: inverted
    pkt = (pkt << 4) | crc;
    for (uint8_t i = 0; i < 16; i++) {
        buf[i*4 + chan] = (pkt & 0x8000) ? DSHOT_BIT_1_TICKS : DSHOT_BIT_0_TICKS;
        pkt <<= 1;
    }
    buf[16*4 + chan] = buf[17*4 + chan] = 0;   // trailing idle
}
```

### Kick-off

```c
TIMx->CNT = 0;
TIMx->EGR = TIM_EGR_UG;        // preload
dmaStreamEnable(stream);
TIMx->CR1 |= TIM_CR1_CEN;
```

---

## 4. Inbound: Switching to Input Capture

When the outbound DMA completes (TC ISR), ArduPilot runs the following sequence