# Bi-directional DShot600 on STM32H743 TIM1/TIM8 (ArduPilot `RCOutput`) with RPM Telemetry Decoding

"Cycle 20" isn't a convention I recognize, so I've treated this as a self-contained specification. Where something depends on your board's `hwdef` or ArduPilot version, it is marked **(verify)**.

---

## 1. Clock and Timing Basis

| Parameter | Value |
|---|---|
| SYSCLK (H743 rev V) | 400 MHz (480 MHz on some boards) |
| APB2 | 100 MHz (TIM1 and TIM8 are on APB2) |
| Timer kernel clock | 200 MHz (×2 rule, since the APB2 prescaler ≠ 1) |
| DShot600 bit time | 1 / 600 kHz = 1.6667 µs |
| Frame (16 bits) | 26.67 µs, plus a reset/pad slot |
| Telemetry bitrate | 5/4 × 600 kHz = 750 kbit/s, so 1.3333 µs per bit |
| Telemetry frame | 21 GCR bits ≈ 28 µs |
| ESC turnaround | about 25–30 µs after the end of the command frame |

### Output timer setup

ArduPilot uses `DSHOT_BIT_WIDTH_TICKS = 8`, with a 0-bit of about 3 ticks and a 1-bit of about 6 ticks (exact values depend on version) **(verify)**.

- Timer tick rate: f_tick = 600 kHz × 8 = 4.8 MHz.
- Prescaler: PSC = 200 MHz / 4.8 MHz − 1 ≈ 40.67. This is not an integer, so ArduPilot rounds PSC to 41 (giving 4.878 MHz, about 1.6% fast). That is within the DShot tolerance of roughly ±5%.
- ARR = 7 (8 ticks per bit). CCR values are 3 for a 0-bit and 6 for a 1-bit.

### Bidirectional (inverted) signalling

In bidirectional mode the line idles **high**, and data is sent as low-going pulses. To achieve this:

- Use output-compare PWM mode 1 with the **CCxP=1** polarity inversion (or equivalently PWM mode 2).
- Configure the GPIO with a pull-up so the line idles high while the pin is an input.

---

## 2. DMA Request Map (DMAMUX1, STM32H743)

| Request | ID | Role |
|---|---|---|
| TIM1_CH1 | 11 | IC capture, motor on CH1 |
| TIM1_CH2 | 12 | IC capture, CH2 |
| TIM1_CH3 | 13 | IC capture, CH3 |
| TIM1_CH4 | 14 | IC capture, CH4 |
| **TIM1_UP** | **15** | DShot output burst (DMAR) |
| TIM8_CH1 | 47 | IC capture |
| TIM8_CH2 | 48 | IC capture |
| TIM8_CH3 | 49 | IC capture |
| TIM8_CH4 | 50 | IC capture |
| **TIM8_UP** | **51** | DShot output burst |

Check these IDs against RM0433, Table "DMAMUX1 request mux inputs" **(verify)**.

### Output phase: TIM_UP burst

- `TIMx_DCR`:
  - DBA = offset of CCR1 = (0x34 / 4) = 13.
  - DBL = (nchannels − 1), i.e. 3 for 4 channels.
- The DMA stream writes to `&TIMx->DMAR`, memory-to-peripheral, 32-bit words.
- The buffer is interleaved: `buf[bit*4 + ch]`. It holds 16 bits plus 2 trailing zero slots, which forces the line back to idle.
- Each update event sends one burst of 4 words to CCR1..CCR4.

### Input phase: TIM_CHx capture

- There is **one DMA stream per channel**. Each stream:
  - reads `CCRx` into a `uint32_t` buffer,
  - runs peripheral-to-memory,
  - is triggered by CCxDE.
- ArduPilot can **share** one stream across channels and capture them one channel per cycle, round-robin. You trade telemetry rate for fewer streams.

### `hwdef.dat` example

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

How `BIDIR` placement works:

- A `BIDIR` flag on CH1/CH3 makes that stream serve the pair (CH1+CH2, CH3+CH4).
- If DMA streams are plentiful, put `BIDIR` on every channel.
- The `dma_resolver` assigns streams. Build output will warn if a CHx stream cannot be allocated.

---

## 3. Per-Timer State Machine (Executed for TIM1 and TIM8 Independently)

```
 ┌────────────┐ dshot_send_groups()
 │   IDLE     │──────────────────────────────┐
 └────────────┘                              ▼
                                     ┌───────────────┐
                                     │  SEND         │ UP-DMA burst, OC mode,
                                     │  (≈30 µs)     │ CCxP inverted, MOE=1
                                     └──────┬────────┘
                                 DMA TC ISR │ (dma_up_irq_callback)
                                            ▼
                                     ┌───────────────┐
                                     │ RECONFIG→IC   │ stop UP DMA, release stream
                                     │  (<~5 µs)     │ CCMRx: CCxS=01 (TI), IC filter
                                     └──────┬────────┘ CCER: CCxP|CCxNP (both edges)
                                            │          PSC→IC rate, ARR=0xFFFF, CNT=0
                                            │          start CHx DMA, N= GCR_EDGES
                                            ▼
                                     ┌───────────────┐
                                     │  CAPTURE      │ ESC reply ≈30 µs + 28 µs
                                     │ (timeout VT)  │ virtual timer ≈ 80–100 µs
                                     └──────┬────────┘
                   timeout / DMA TC         │
                                            ▼
                                     ┌───────────────┐
                                     │ RESTORE OC    │ stop CHx DMA, cache invalidate,
                                     │ + DECODE      │ decode_telemetry_packet()
                                     └──────┬────────┘ CCMR back to PWM, re-arm UP DMA
                                            ▼
                                          IDLE
```

### Key register operations, in order

```c
// --- SEND ---
TIMx->BDTR |= TIM_BDTR_MOE;                  // advanced timers TIM1/TIM8 need MOE
TIMx->CCER  = CCxE | CCxP;                   // inverted output (bidir)
TIMx