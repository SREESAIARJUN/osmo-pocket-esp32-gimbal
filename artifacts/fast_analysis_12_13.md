# Bi-directional DShot600 on STM32H743 TIM1/TIM8: DMA Request Logic and RPM Telemetry Decoding

This follows the structure of ArduPilot's `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`. Constants and register sequences are written out explicitly. Treat it as a formulation of the logic, not a verbatim copy of the source.

"Cycle 12" isn't a standard ArduPilot term. Below it is treated as one complete output-then-capture period for both timers. If it refers to something in your own series, map the phases in section 3 accordingly.

---

## 1. Clock and Timing Base

| Item | Value |
|---|---|
| SYSCLK (ArduPilot H743 default) | 400 MHz |
| APB2 | 100 MHz → TIM1/TIM8 kernel clock = **200 MHz** |
| DShot600 bit rate | 600 kbit/s (T_bit = 1.667 µs) |
| Ticks per bit (`DSHOT_BIT_WIDTH_TICKS`) | 8 |
| Bit-0 / Bit-1 high time | 3 / 6 ticks (37.5 % / 75 %) |
| Telemetry rate (5/4 × DShot) | 750 kbit/s (T = 1.333 µs) |
| ESC turnaround | ≈ 30 µs after frame end |
| Telemetry frame | 21 bits GCR ≈ 28 µs |

**Output prescaler**

```
PSC = round(200 MHz / (600 kHz × 8)) - 1 = 41 - 1 = 40   → 4.878 MHz, 609.8 kbit/s (+1.6 %, within ESC tolerance)
ARR = 8 - 1 = 7
```

**Input-capture prescaler**

Capture needs enough resolution to resolve 1.333 µs runs. Running at PSC = 0 (200 MHz) gives about 267 ticks per telemetry bit. ArduPilot uses a coarser rate, but any rate with 3 or more ticks per bit, combined with run-length rounding, works.

---

## 2. DMAMUX Request Map (RM0433, DMAMUX1)

| Request | ID | Use |
|---|---|---|
| TIM1_CH1..CH4 | 11..14 | bdshot input capture |
| **TIM1_UP** | **15** | DShot burst output |
| TIM8_CH1..CH4 | 47..50 | bdshot input capture |
| **TIM8_UP** | **51** | DShot burst output |

The H7 DMAMUX lets any DMA1/DMA2 stream take any request. In `hwdef.dat` this looks like:

```
PE9  TIM1_CH1 TIM1 PWM(1) GPIO(50) BIDIR
PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PE14 TIM1_CH4 TIM1 PWM(4) GPIO(53)
PC6  TIM8_CH1 TIM8 PWM(5) GPIO(54) BIDIR
...
DMA_PRIORITY TIM1_UP* TIM8_UP* TIM1_CH* TIM8_CH*
```

`BIDIR` marks the channels that get a dedicated capture stream. With fewer streams available, ArduPilot round-robins the capture channel each cycle. In that case, each motor's RPM updates at loop_rate / 4.

---

## 3. Per-Cycle State Machine (per timer group)

```
 ┌──────────────┐  TIMx_UP DMA burst   ┌──────────────┐ DMA TC ISR  ┌───────────────┐
 │ IDLE (high)  │ ───────────────────► │ SEND 16+pad  │ ──────────► │ RECONFIG→IC   │
 └──────────────┘                      └──────────────┘             └──────┬────────┘
        ▲                                                                   │ CHx DMA, both edges
        │      decode in thread ctx       ┌──────────────┐   timeout/TC     ▼
        └──────────────────────────────── │ DECODE GCR   │ ◄──────── ┌───────────────┐
                                          └──────────────┘           │ CAPTURE ~45µs │
                                                                     └───────────────┘
```

Both TIM1 and TIM8 groups run this sequence. ArduPilot serialises them through `Shared_DMA` locks, so they may share streams. Group 1 sends, captures, and releases, then group 2 does the same. With dedicated streams, they run in parallel.

### 3a. Output phase: DMAR burst on update event

```c
// frame build (bidirectional => inverted checksum)
uint16_t pkt = (throttle << 1) | telem_req;
uint16_t csum = (~(pkt ^ (pkt >> 4) ^ (pkt >> 8))) & 0x0F;
pkt = (pkt << 4) | csum;

// interleaved buffer: [bit][ch] for 4 CCRs, + 2 trailing zero slots
for (uint8_t b = 0; b < 16; b++) {
    buf[b*4 + ch] = (pkt & 0x8000) ? BIT1_TICKS /*6*/ : BIT0_TICKS /*3*/;
    pkt <<= 1;
}
buf[16*4 .. 17*4+3] = 0;           // pad -> line returns to idle

// timer setup
TIMx->CR1  = 0;
TIMx->PSC  = 40;  TIMx->ARR = 7;
TIMx->CCMRx: OCxM = PWM mode 1, OCxPE = 1
TIMx->CCER: CCxE = 1, CCxP = 1     // INVERTED: idle high, pulses low
TIMx->BDTR |= TIM_BDTR_MOE;        // required on advanced timers TIM1/