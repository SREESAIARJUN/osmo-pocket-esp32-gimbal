# Bi-Directional DShot600 on STM32H743 TIM1/TIM8: DMA Request Logic and RPM Telemetry Decoding (ArduPilot ChibiOS HAL)

This covers TIM1 and TIM8 (APB2 advanced-control timers) driving bi-directional DShot600, as implemented in `libraries/AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`. It is organised as one full output→capture→decode cycle. I've read "Cycle 9" as the ninth frame of the round-robin capture schedule. Treat the code as an accurate model of the logic rather than a verbatim copy of the source.

---

## 1. Clock and Timing Budget

| Parameter | Value |
|---|---|
| SYSCLK (ArduPilot H743 default) | 400 MHz |
| APB2 timer clock (TIM1/TIM8) | 200 MHz |
| DShot600 bit period | 1.667 µs (600 kbit/s) |
| `DSHOT_BIT_WIDTH_TICKS` | 8 (ARR = 7) |
| `DSHOT_BIT_0_TICKS` / `DSHOT_BIT_1_TICKS` | 3 / 6 |
| Target counter clock | 600 k × 8 = 4.8 MHz → PSC computed by `calculate_bitrate_prescaler()`, rounded |
| Frame | 16 bits + 2 trailing zero slots ≈ 30 µs |
| ESC turnaround | ~30 µs (line released, pull-up holds it high) |
| Telemetry bit rate | 5/4 × 600 k = 750 kbit/s (1.333 µs/bit) |
| Telemetry frame | 21 bits ≈ 28 µs |
| Capture oversampling | 16 timer ticks per telemetry bit (hence `(diff + 8) / 16`) |

Total cycle ≈ 30 + 30 + 28 + guard ≈ 100–110 µs per group.

---

## 2. DMAMUX1 Request Mapping (H743)

The H7 DMAMUX lets any DMA1/DMA2 stream service any request, so allocation is flexible. Stream count is still finite: 16 streams are shared with UARTs, SPI and ADC.

| Request | DMAMUX ID | Role |
|---|---|---|
| TIM1_UP | 15 | Output burst (DMAR → CCR1..4) |
| TIM1_CH1 / CH2 / CH3 / CH4 | 11 / 12 / 13 / 14 | Input capture of eRPM edges |
| TIM8_UP | 51 | Output burst |
| TIM8_CH1 / CH2 / CH3 / CH4 | 47 / 48 / 49 / 50 | Input capture |

Example `hwdef.dat`:

```
PE9  TIM1_CH1 TIM1 PWM(1) GPIO(50) BIDIR
PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PE14 TIM1_CH4 TIM1 PWM(4) GPIO(53)
PC6  TIM8_CH1 TIM8 PWM(5) GPIO(54) BIDIR
PC7  TIM8_CH2 TIM8 PWM(6) GPIO(55)
PC8  TIM8_CH3 TIM8 PWM(7) GPIO(56) BIDIR
PC9  TIM8_CH4 TIM8 PWM(8) GPIO(57)
DMA_PRIORITY TIM1* TIM8*
```

`BIDIR` marks the channel whose capture DMA stream is shared with its partner (CH1↔CH2, CH3↔CH4). The `BIDIR` tags and the existence of TIMx_CHy DMA defines enable `HAL_WITH_BIDIR_DSHOT`.

---

## 3. Output Phase: TIMx_UP Burst DMA

### 3.1 Timer configuration

The signal is inverted for bi-directional DShot: the line idles high and pulses are active-low.

```c
// Advanced timer: main output enable is mandatory
TIMx->BDTR |= TIM_BDTR_MOE;

// PWM mode 1, inverted polarity (idle high on the wire)
TIMx->CCMR1 = (6 << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE |
              (6 << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
TIMx->CCMR2 = (6 << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
              (6 << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
TIMx->CCER  = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC2E | TIM_CCER_CC2P |
              TIM_CCER_CC3E | TIM_CCER_CC3P | TIM_CCER_CC4E | TIM_CCER_CC4P;

TIMx->PSC = prescaler;               // -> 4.8 MHz
TIMx->ARR = DSHOT_BIT_WIDTH_TICKS - 1;

// DMA burst: base = CCR1, length = 4 transfers per UEV
TIMx->DCR  = ((offsetof(TIM_TypeDef, CCR1) >> 2) << TIM_DCR_DBA_Pos)
           | ((4 - 1) << TIM_DCR_DBL_Pos);
TIMx->DIER = TIM_DIER_UDE;           // UP DMA request
```

### 3.2 Buffer layout (interleaved, 4 channels × 18 slots)

```c
// dma_buffer[bit * 4 