# Bi-directional DShot600 on STM32H743 TIM1/TIM8: DMA and RPM Telemetry Logic

This follows the structure ArduPilot uses in `AP_HAL_ChibiOS/RCOutput.cpp`, `RCOutput_bdshot.cpp` and `RCOutput.h`. Function and field names are simplified for clarity. I treated "Cycle 31" as an iteration label; it does not change the logic below.

---

## 1. Clock and Timing Basis

On a typical ArduPilot H743 build, SYSCLK is 400 MHz and APB2 is 100 MHz. TIM1 and TIM8 sit on APB2, so their kernel clock is ×2, giving **200 MHz**.

DShot600 requirements:

- Bit rate: 600 kbit/s, so T_bit = 1.667 µs.
- ArduPilot uses 8 timer ticks per bit:
  - `DSHOT_BIT_WIDTH_TICKS = 8`
  - `BIT_0_TICKS = 3` (37.5% duty)
  - `BIT_1_TICKS = 6` (75% duty)
- Required tick clock: 600 kHz × 8 = 4.8 MHz.

Prescaler calculation:

- PSC = round(200 MHz / 4.8 MHz) − 1 = **41**.
- Actual tick clock = 200 MHz / 42 = 4.762 MHz.
- Actual bit rate ≈ 595 kbit/s, a −0.8% error. DShot tolerates this well (within ±5%).
- ARR = 8 − 1 = **7**.

```c
uint32_t target = bitrate * DSHOT_BIT_WIDTH_TICKS;          // 4.8e6
uint16_t psc    = (timer_clk + target/2) / target - 1;      // 41
timer->PSC = psc;
timer->ARR = DSHOT_BIT_WIDTH_TICKS - 1;                     // 7
```

---

## 2. Outbound Frame Construction (Bi-directional = Inverted)

Frame layout: `[11-bit throttle][1-bit telem req][4-bit CRC]`.

For bi-directional DShot, the CRC is **inverted**. The line idles **high**, so the output polarity is inverted (CCxP = 1, or PWM mode 2).

```c
uint16_t pkt = (value << 1) | telem_req;
uint16_t crc = (pkt ^ (pkt >> 4) ^ (pkt >> 8));
if (bdshot) crc = ~crc;
pkt = (pkt << 4) | (crc & 0xF);
```

### DMA buffer layout

The buffer is interleaved for burst writes:

- 18 slots: 16 data bits plus 2 trailing zero slots so the line returns to idle.
- Each slot holds 4 words, one per channel (CCR1..CCR4).

```c
// buffer[(bit * 4) + chan]
for (uint8_t b = 0; b < 16; b++) {
    buf[b*4 + chan] = (pkt & 0x8000) ? BIT_1_TICKS : BIT_0_TICKS;
    pkt <<= 1;
}
buf[16*4 + chan] = buf[17*4 + chan] = 0;   // reset/guard slots
```

---

## 3. DMA Burst Request Logic (Output Phase)

One DMA stream per timer, triggered by **TIMx_UP**. It writes through `DMAR` in burst mode to all four CCRs per update event.

### DMAMUX1 request IDs (RM0433)

| Timer | UP  | CH1 | CH2 | CH3 | CH4 |
|-------|-----|-----|-----|-----|-----|
| TIM1  | 15  | 11  | 12  | 13  | 14  |
| TIM8  | 51  | 47  | 48  | 49  | 50  |

### Register setup

```c
// DCR: DBA = offset of CCR1 / 4 = 0x34/4 = 13, DBL = 4 transfers - 1 = 3
timer->DCR  = (3 << TIM_DCR_DBL_Pos) | (13 << TIM_DCR_DBA_Pos);
timer->DIER = TIM_DIER_UDE;                       // update DMA request
timer->BDTR |= TIM_BDTR_MOE;                      // REQUIRED on TIM1/TIM8 (advanced)

dmaStreamSetPeripheral(stream, &timer->DMAR);
dmaStreamSetMemory0(stream, dma_buffer);          // must be in DMA-capable SRAM (AXI/SRAM1-3, not DTCM)
dmaStreamSetTransactionSize(stream, 18 * 4);
dmaStreamSetMode(stream,
    STM32_DMA_CR_CHSEL(dmamux_req)  |             // 15 for TIM1, 51 for TIM8
    STM32_DMA_CR_DIR_M2P | STM32_DMA_CR_MINC |
    STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
    STM32_DMA_CR_PL(3) | STM32_DMA_CR_TCIE);

cacheBufferFlush(dma_buffer, len);                // H7 D-cache coherency
timer->EGR = TIM_EGR_UG;                          // preload first values
timer->CNT = 0;
dmaStreamEnable(stream);
timer->CR1 |= TIM_CR1_CEN;
```

### Notes

- **Buffer placement:** The buffer must not be in DTCM, because the DMA1/2 controllers cannot reach it on the H7.
- **Shared stream allocation:** ArduPilot uses `Shared_DMA` to allocate the UP stream. TIM1 and TIM8 should get **separate** UP streams so both groups can fire in the same loop without serialisation.

---

## 4. Turnaround: Output to Input Capture

The DMA transfer-complete ISR (`dma_up_irq_callback`) does the following:

1. Stop the timer and disable UDE.
2. Reconfigure each bdshot channel to **input capture on both edges**: CCxS = 01, CCxP = 1, CCxNP = 1.
3. Set a faster capture timebase. ArduPilot uses PSC = 0 or a small value with ARR = 0xFFFF, so edges are timestamped with fine resolution.
4. Arm a **per-channel CCx DMA request** (TIM1_CH1 = 11, and so on) into a 16-bit capture buffer, size `GCR_TELEMETRY_BIT_LEN` ≈ 21 + margin.

