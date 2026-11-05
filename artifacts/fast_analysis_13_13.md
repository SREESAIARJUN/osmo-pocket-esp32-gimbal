# Bidirectional DShot600 on STM32H743 TIM1/TIM8 with RPM Telemetry Decoding

I don't recognize "Cycle 13" as an ArduPilot term, so I have treated it as your own iteration label. If it refers to a specific branch, issue, or design-review step, tell me and I'll align the answer to it. The structure below follows ArduPilot's ChibiOS HAL (`RCOutput.cpp`, `RCOutput_bdshot.cpp`). Constants are derived from the H743 clock tree and DShot specs. Confirm DMAMUX IDs and timer clocks against RM0433 and your board's `hwdef.dat`.

---

## 1. Clock and timing derivation

| Parameter | Value |
|---|---|
| SYSCLK | 400 MHz (ArduPilot H7 default; 480 MHz on rev V if enabled) |
| APB2 | 100 MHz, so TIM1/TIM8 kernel clock = 200 MHz (×2 rule) |
| DShot600 bit rate | 600 kbit/s, giving a bit period of 1.667 µs |
| ArduPilot bit width | `DSHOT_BIT_WIDTH_TICKS = 20` |
| Bit "0" high time | 7 ticks (~37 %) |
| Bit "1" high time | 14 ticks (~75 %) |
| Target counter clock | 600 kHz × 20 = 12 MHz |
| Prescaler | 200 MHz / 12 MHz = 16.67, which is not an integer |

ArduPilot handles the non-integer ratio by rounding the prescaler and accepting the small rate error:

```
psc        = (timer_clock + target/2) / target - 1;   // = 16
actual_clk = timer_clock / (psc + 1);                 // 11.76 MHz
bitrate    = actual_clk / 20;                         // ~588 kbit/s (-2%, within ESC tolerance)
ARR        = 20 - 1;
```

Telemetry return path:
- The ESC replies at 5/4 of the command rate: 750 kbit/s, or 1.333 µs per bit.
- At the 11.76 MHz capture clock, that is about 15.7 ticks per telemetry bit.
- The reply starts about 30 µs after the command frame ends.
- The reply lasts about 21 × 1.333 µs ≈ 28 µs.

## 2. Frame construction (bidirectional = inverted)

```c
static uint16_t create_dshot_packet(uint16_t value, bool telem_req, bool bidir)
{
    uint16_t packet = (value << 1) | (telem_req ? 1 : 0);
    uint16_t csum = 0, csum_data = packet;
    for (uint8_t i = 0; i < 3; i++) { csum ^= csum_data; csum_data >>= 4; }
    if (bidir) csum = ~csum;              // inverted CRC signals bidir mode to the ESC
    return (packet << 4) | (csum & 0xF);
}
```

The line idles **high** in bidirectional mode. ArduPilot implements this by setting `CCxP` (active-low polarity) on the output channel instead of inverting the duty buffer.

## 3. DMA buffer layout (burst via DMAR)

TIM1 and TIM8 each use one **UP-request** DMA stream to update all 4 CCRs per bit:

```
buffer[bit * 4 + ch] = bit_set ? 14 : 7;    // 16 data bits
buffer[16..17][*]    = 0;                   // 2 trailing zero slots → line returns to idle
length               = (16 + 2) * 4 words
```

Timer DMA burst configuration:

```c
TIMx->DCR  = (4-1) << TIM_DCR_DBL_Pos      // DBL = 4 transfers
           | ((offsetof(TIM_TypeDef, CCR1) / 4) << TIM_DCR_DBA_Pos);
TIMx->DIER |= TIM_DIER_UDE;                 // UP request drives the burst
// DMA: periph addr = &TIMx->DMAR, mem→periph, 32-bit (or 16-bit) words, no circular
```

Advanced-timer specifics for TIM1/TIM8:
- Set `BDTR.MOE = 1`, or the outputs stay dead.
- Keep `RCR = 0`.
- Disable complementary outputs (`CCxNE = 0`) unless a pin is mapped to CHxN. If it is, use `CCxNP` for the inversion instead.

## 4. DMAMUX1 request IDs (H743, verify against RM0433 Table 121)

| Request | TIM1 | TIM8 |
|---|---|---|
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |
| UP | 15 | 51 |

In `hwdef.dat`, mark the pins as bidirectional so the generator allocates capture DMA:

```
PE9  TIM1_CH1 TIM1 PWM(1) GPIO(50) BIDIR
PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PE14 TIM1_CH4 TIM1 PWM(4) GPIO(53)
PC