# Bi-directional DShot600 on STM32H743 TIM1/TIM8 in ArduPilot

This covers the DMA request routing, the timer TX/RX turnaround, and the RPM telemetry decode, following ArduPilot's `RCOutput.cpp` and `RCOutput_bdshot.cpp`. I've treated "Cycle 30" as an iteration label. If it refers to something specific, such as a frame index or loop rate, tell me and I'll adapt the design.

---

## 1. Clock and Bit Timing

On the H743 at 400 MHz, TIM1 and TIM8 sit on APB2, so the timer kernel clock is **200 MHz**.

| Parameter | DShot600 value |
|---|---|
| Bit rate (outgoing) | 600 kbit/s, which is 1.667 µs per bit |
| Frame | 16 bits plus 2 reset slots, about 30 µs |
| Telemetry bit rate (GCR) | 600k × 5/4 = **750 kbit/s**, which is 1.333 µs per bit |
| Telemetry frame | 21 bits, starting about 30 µs after the end of TX |

**Prescaler choice.** ArduPilot picks a fixed number of ticks per bit (`DSHOT_BIT_WIDTH_TICKS`, typically 8) and solves for the prescaler:

```
psc  = round(f_tim / (bitrate * ticks_per_bit)) - 1
     = round(200e6 / (600e3 * 8)) - 1 = 42 - 1 = 41
ARR  = ticks_per_bit - 1 = 7
T0H  = 3 ticks (~37.5 %)   T1H = 6 ticks (~75 %)
```

The resulting bitrate error is about 0.8%. That is well inside ESC tolerance (BLHeli_32, AM32 and Bluejay all accept a few percent).

---

## 2. DMAMUX1 Request IDs (RM0433)

The H7 DMAMUX decouples requests from streams. Any DMA1 or DMA2 stream can serve any request.

| Request | ID | Request | ID |
|---|---|---|---|
| TIM1_CH1 | 11 | TIM8_CH1 | 47 |
| TIM1_CH2 | 12 | TIM8_CH2 | 48 |
| TIM1_CH3 | 13 | TIM8_CH3 | 49 |
| TIM1_CH4 | 14 | TIM8_CH4 | 50 |
| **TIM1_UP** | **15** | **TIM8_UP** | **51** |

### Stream allocation per timer

- **TX uses `TIMx_UP` with a DMA burst through `DMAR`/`DCR`.** One stream drives all 4 channels at once:
  - `DCR.DBA` = offset of CCR1 = (0x34 / 4) = 13
  - `DCR.DBL` = 3, giving 4 transfers per update
  - The buffer is interleaved as `[CCR1,CCR2,CCR3,CCR4] × 18 slots`, with 32-bit words.
- **RX uses `TIMx_CHy` in input-capture mode.** ArduPilot normally has **one** capture stream per timer and **rotates** which channel it listens to each frame. This is the `bdshot.curr_telem_chan` logic. Each motor therefore reports RPM every 4th frame on a 4-channel timer.
  - If the hwdef provides a separate stream per CH request, all channels are captured every frame.
- On the H7, ArduPilot can also **reuse the UP stream** for capture by re-pointing the DMAMUX request ID. This is the shared-DMA path, used when streams are scarce.

### Example hwdef (Matek H743-style)

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

`BIDIR` marks the channel whose CH DMA request is allocated for capture. ArduPilot pairs channels (CH1 with CH2, CH3 with CH4), so the capture stream services its pair through rotation.

---

## 3. Per-Frame State Machine (one timer group)

```
          ┌──────────────── 1 kHz+ output loop ────────────────┐
          ▼                                                    │
 [PREP] fill dma_buffer (inverted DShot, telemetry req bit=1)  │
          │                                                    │
 [TX]  pins → AF, CCMRx = PWM mode 2 (inverted idle-high),     │
       BDTR.MOE=1, DIER.UDE=1, DMAMUX req = TIMx_UP,           │
       stream: mem→DCR burst → TIMx->DMAR, 18×4 words          │
          │  DMA TC ISR                                        │
          ▼                                                    │
 [TURNAROUND] UDE=0, CEN=0, CCxE=0                             │
       reconfigure channel ch=curr_telem_chan:                 │
         CCMR CCxS=01 (IC on TIx), ICxF=filter, CCxP|CCxNP     │
         (both edges), PSC for ~0.1 µs resolution,             │
         ARR=0xFFFF, DMAMUX req = TIMx_CHy, CCxDE=1            │
       arm DMA: TIMx->CCRy → bdshot_buf[], N=~32 halfwords      │
       start chVT timeout (~40-50 µs + frame)                  │
          ▼                                                    │
 [RX]  ESC replies ~30 µs later, edges DMA'd                   │
          │ DMA TC or timeout                                  │
          ▼                                                    │
 [FINISH] stop stream, CCxE=0, restore PWM config,             │
          decode capture → erpm[ch], rotate curr_telem_chan ───┘
```

**Turnaround constraints.**

- The reconfiguration must complete in well under 30 µs. ArduPilot does it in the TX-complete ISR at high priority.
- Advanced timers need **`BDTR.MOE`** set for any output. It is harmless during capture.
- Pins must stay pulled up during RX (`PAL_STM32_PUPDR_PULLUP`), because the idle state is high.
- Run the **D-cache maintenance** before each DMA:
  - `cacheBufferFlush` on the TX buffer.
  - `cacheBufferInvalidate` on the capture buffer.
  - Alternatively, place both buffers in SRA