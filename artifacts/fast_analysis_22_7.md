# ArduPilot Bi-Directional DShot600 on STM32H743: TIM1 and TIM8 DMA Logic with RPM Telemetry Decoding

> **Note on "Cycle 22":** I don't know what your project, review, or test series means by "Cycle 22". This document uses a generic per-update-cycle model. If Cycle 22 refers to a specific constraint, such as a particular loop rate, channel rotation index, or telemetry slot, tell me and I'll specialise the timing tables.

This follows the architecture of `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`. Register-level details are simplified where ArduPilot wraps them in ChibiOS calls.

---

## 1. Hardware Mapping (STM32H743)

### Timer clock and pins

- TIM1 and TIM8 are advanced timers on APB2.
- With the typical ArduPilot H743 clock tree (sysclk 400 MHz, D2PPRE1/2 = /2), the timer kernel clock is **200 MHz**.

| Timer | Ch | Typical pin | DMAMUX1 request ID |
|---|---|---|---|
| TIM1 | CH1 | PE9 | 11 |
| TIM1 | CH2 | PE11 | 12 |
| TIM1 | CH3 | PE13 | 13 |
| TIM1 | CH4 | PE14 | 14 |
| TIM1 | UP | – | 15 |
| TIM8 | CH1 | PC6 | 47 |
| TIM8 | CH2 | PC7 | 48 |
| TIM8 | CH3 | PC8 | 49 |
| TIM8 | CH4 | PC9 | 50 |
| TIM8 | UP | – | 51 |

Because the H7 uses DMAMUX, any DMA1/DMA2 stream can be routed to any request. The only constraint is how many streams are free. ArduPilot's `dma_resolver.py` allocates them at build time from the hwdef.

### hwdef.dat fragment

```
# TIM1 – motors 1-4
PE9  TIM1_CH1 TIM1 PWM(1) GPIO(50) BIDIR
PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PE14 TIM1_CH4 TIM1 PWM(4) GPIO(53)

# TIM8 – motors 5-8
PC6  TIM8_CH1 TIM8 PWM(5) GPIO(54) BIDIR
PC7  TIM8_CH2 TIM8 PWM(6) GPIO(55)
PC8  TIM8_CH3 TIM8 PWM(7) GPIO(56) BIDIR
PC9  TIM8_CH4 TIM8 PWM(8) GPIO(57)

DMA_PRIORITY TIM1* TIM8*
DMA_NOSHARE  TIM1_UP TIM8_UP TIM1_CH* TIM8_CH*
define HAL_WITH_BIDIR_DSHOT 1
```

- `BIDIR` marks channels that get a dedicated input-capture DMA stream.
- On H7 you can mark all four channels if streams allow. Otherwise ArduPilot pairs them (CH1 serves CH1/CH2, CH3 serves CH3/CH4) and round-robins telemetry.

---

## 2. DShot600 Output Timing

| Parameter | Value |
|---|---|
| Bit rate | 600 kbit/s |
| Bit period | 1.667 µs |
| T1H (75 %) | 1.250 µs |
| T0H (37.5 %) | 0.625 µs |
| Frame (16 bits) | 26.67 µs |

ArduPilot selects a prescaler so that one bit spans a fixed number of ticks:

```
bit_width_ticks = 20                       // high-resolution mode on H7
f_tick          = 600e3 * 20 = 12 MHz
PSC             = 200 MHz / 12 MHz - 1     // non-integer (16.67), so
                                           // ArduPilot picks the nearest
                                           // and adjusts bit_width_ticks
ARR             = bit_width_ticks - 1
CCR(bit=1)      = 14   (≈ 70–75 %)
CCR(bit=0)      =  7   (≈ 35–37.5 %)
```

ArduPilot computes this in `RCOutput::set_group_mode()` via `protocol_bitrate()`, then calls `pwmStart()` with `period = bit_width_ticks`.

### Bi-directional inversion

In bidir mode the line idles **high**:

- Output polarity is inverted: `CCxP = 1`, i.e. `PWM_OUTPUT_ACTIVE_LOW` in ChibiOS.
- The CRC is the inverted variant: `crc = (~(v ^ v>>4 ^ v>>8)) & 0x0F`.

---

## 3. DMA Request Logic: Output Phase (Burst via TIMx_UP)

All four channels of one timer are written in a single burst on each update event using the DMA burst registers.

```c
// TIMx->DCR: DBA = offset of CCR1 / 4, DBL = 4 transfers (CCR1..CCR4)
tim->DCR  = ((offsetof(stm32_tim_t, CCR[0]) >> 2) << TIM_DCR_DBA_Pos)
          | ((4 - 1) << TIM_DCR_DBL_Pos);
tim->DIER = TIM_DIER_UDE;        // update event -> DMA request (TIM1_UP=15 / TIM8_UP=51)

// DMA stream: memory -> TIMx->DMAR, 32-bit words, 18 bursts
//   16 data bits + 2 trailing zero slots so the line returns to idle
dmaStreamSetPeripheral(group.dma, &tim->DMAR);
dmaStreamSetMemory0  (group.dma, group.dma_buffer);   // uint32_t[18*4]
dmaStreamSetTransactionSize(group.dma, 18 * 4);
dmaStreamSetMode(group.dma,
      STM32_DMA_CR_CHSEL(dmamux_id) | STM32_DMA_CR_DIR_M2P
    | STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD
    | STM32_DMA_CR_MINC | STM32_DMA_CR_PL(3)
    | STM32_DMA_CR_TEIE | STM32_DMA_CR_TCIE);

// advanced-timer