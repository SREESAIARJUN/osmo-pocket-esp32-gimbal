# Bi-directional DShot600 on STM32H743 TIM1/TIM8 in ArduPilot

Your request includes "(Cycle 14)". That isn't a term ArduPilot uses. If it refers to a specific board revision, firmware branch, or your own iteration numbering, tell me and I'll adapt this. The rest follows how ArduPilot's `RCOutput` and `RCOutput_bdshot` work on H7.

---

## 1. Hardware resources

**TIM1 and TIM8 on H743**
- Both are advanced-control timers on APB2.
- With the usual ArduPilot clock tree (SYSCLK 400 MHz, APB2 timer clock 200 MHz), the timer clock is `f_tim = 200 MHz`.
- Both have a DMAR/DCR burst interface, which ArduPilot uses for multi-channel output.
- Advanced timers require `BDTR.MOE = 1`. ChibiOS PWM handles this.

**DMAMUX1 request IDs (RM0433)**

| Request | TIM1 | TIM8 |
|---|---|---|
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |
| UP  | 15 | 51 |
| TRIG| 16 | 52 |
| COM | 17 | 53 |

Each timer group needs DMA streams for:
- **TX:** `TIMx_UP`, which drives a DMAR burst into CCR1..CCR4.
- **RX (telemetry):** `TIMx_CHy`, one input-capture DMA per channel. If streams are scarce, ArduPilot can reuse the UP stream for capture and rotate one channel per cycle.

**Example hwdef.dat**
```
# TIM1 group
PE9  TIM1_CH1 TIM1 PWM(1) GPIO(50) BIDIR
PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PE14 TIM1_CH4 TIM1 PWM(4) GPIO(53)
# TIM8 group
PC6  TIM8_CH1 TIM8 PWM(5) GPIO(54) BIDIR
PC7  TIM8_CH2 TIM8 PWM(6) GPIO(55)
PC8  TIM8_CH3 TIM8 PWM(7) GPIO(56) BIDIR
PC9  TIM8_CH4 TIM8 PWM(8) GPIO(57)

define HAL_WITH_BIDIR_DSHOT
DMA_PRIORITY TIM1* TIM8*
```

**`BIDIR` placement**
- Mark `BIDIR` on channel 1 or 3, or on 2 or 4.
- The paired channel provides the input-capture DMA.
- The DMA resolver (`dma_resolver.py`) must find a stream for each `TIMx_UP` and each `TIMx_CHy` used for capture.
- Check the generated `hwdef.h` for "DMA unavailable" warnings.

---

## 2. TX timing for DShot600

- **Bit rate:** 600 kbit/s, so the bit period is 1.6667 µs.
- **Bit encoding:**
  - "0" is high for 37.5% of the period (0.625 µs).
  - "1" is high for 75% of the period (1.25 µs).
- **Ticks per bit:** ArduPilot uses a fixed number per bit, `DSHOT_BIT_WIDTH_TICKS`. Recent code defaults to 8, with BIT_0 = 3 and BIT_1 = 6. Older code used 20/7/14.
- **Timer clock and prescaler:**

```
f_count  = 600 kHz * TICKS          (8 ticks → 4.8 MHz)
PSC      = f_tim / f_count - 1      (200 MHz / 4.8 MHz ≈ 41.67 → rounded; error well under DShot tolerance)
ARR      = TICKS - 1
```

**Frame layout**
- 16 bits: 11-bit throttle, 1 telemetry-request bit, and a 4-bit CRC.
- Followed by 2 or more zero-duty "reset" slots, so the line idles at the end.

**Bi-directional (inverted) CRC**
```
crc = (~(v ^ (v >> 4) ^ (v >> 8))) & 0x0F;   // inverted for bidir
```

**Inverted signalling**
- In bidir mode the line idles HIGH.
- Output uses inverted polarity: `CCxP = 1`, or PWM mode 2.
- This lets the ESC detect the telemetry request from the polarity.

**DMA burst buffer**
- Layout is `[bit][channel]`: 4 × u32 words per bit, for 16 + 2 bits.
- Settings:
  - `DCR.DBA` = offset of CCR1 (0x34 / 4 = 13).
  - `DCR.DBL` = 3, giving 4 transfers per burst.
  - `DIER.UDE = 1`.
- The DMA stream:
  - is memory→peripheral,
  - targets `&TIMx->DMAR`,
  - uses word size,
  - transfers `(16 + 2) * 4` items.

---

## 3. Bidir request/response sequence (per timer group)

```
 t=0          ~26.7 µs       +~30 µs (ESC turnaround)       ~+28 µs
 |--DShot TX--|---switch-----|----GCR telemetry 21 bits-----|--next TX
   UP DMA        pins→IC        CHy DMA captures edges
```

### Step A: TX
1. `dshot_send_groups()` fills the buffer and arms the UP DMA stream.
2. It starts the timer in PWM mode.
3. On DMA TC (transfer complete), `dma_up_irq_callback()` runs.

### Step B: Switch to capture
- Stop the timer.
- Release the UP stream, or keep it if it is shared for capture.
- Reconfigure the bidir channel(s):
  - `CCMRx.CCxS = 01`, input on TIx.
  - Both-edge capture: `CCxP = 1`, `CCxNP = 1`.
  - Input filter as appropriate.
- Set PSC/ARR for capture resolution:
  - ArduPilot uses a capture clock that gives several ticks per telemetry bit.
  - The telemetry bit rate is 5/4 × 600 kHz = 750 kbit/s, so 1.333 µs per bit.
  - `ARR` is set to 0xFFFF so it doesn't overflow during the frame.
- Arm the CHy DMA:
  - peripheral→memory from `&TIMx->CCRy`,
  - capture buffer of about 32 halfwords,
  - `DIER.CCyDE = 1`.
- Start the timer.

### Step C: Capture timeout
- A virtual timer (~ 45–60 µ