# Bi-directional DShot600 on STM32H743 TIM1/TIM8: DMA and RPM Telemetry Design

This describes the design pattern ArduPilot uses (`RCOutput.cpp` and `RCOutput_bdshot.cpp`). The code is a sketch: verify identifiers, tick constants and DMAMUX IDs against your ChibiOS and ArduPilot tree before using it.

---

## 1. Clock and Timing Base

TIM1 and TIM8 are APB2 timers on the H743.

| Item | Value |
|---|---|
| SYSCLK | 400 MHz (480 MHz on rev V parts) |
| Timer kernel clock (TIM1/TIM8) | 200 MHz (240 MHz) |
| DShot600 bit rate | 600 kbit/s, so 1.667 µs per bit |
| Ticks per bit | 20 |
| Required tick rate | 600 k × 20 = 12 MHz |
| Prescaler (PSC) | 200 / 12 ≈ 16.67, so PSC = 16 (−0.6 % at 200 MHz). At 240 MHz, PSC = 19 exactly |
| ARR | 19 (20 ticks) |
| Bit "1" high time | 15 ticks (75 %) |
| Bit "0" high time | 7 ticks (37.5 %) |

The prescaler is computed at runtime:

```c
psc = (timer_clock + (rate * width)/2) / (rate * width) - 1;
```

The ±1–2 % timing error this produces is well inside ESC tolerance.

**Polarity:** bi-directional DShot is inverted (the line idles high). Use PWM mode 1 with `CCxP = 1`, or PWM mode 2.

---

## 2. DMAMUX Request Mapping (H743, DMAMUX1)

| Request | ID | Request | ID |
|---|---|---|---|
| TIM1_CH1 | 11 | TIM8_CH1 | 47 |
| TIM1_CH2 | 12 | TIM8_CH2 | 48 |
| TIM1_CH3 | 13 | TIM8_CH3 | 49 |
| TIM1_CH4 | 14 | TIM8_CH4 | 50 |
| **TIM1_UP** | **15** | **TIM8_UP** | **51** |

The DMAMUX lets any stream on DMA1 or DMA2 serve any request. ArduPilot's `hwdef` allocator assigns these automatically.

### Request roles

1. **Transmit.** `TIMx_UP` drives a burst DMA through `DMAR`. One stream updates all four CCRs per bit.
2. **Receive.** `TIMx_CHn` captures edges on the channel being read back. Each channel that needs telemetry needs its own capture request. ArduPilot often time-shares one stream and reads one channel per cycle in rotation.

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

Here `BIDIR` on CH1 and CH3 allocates capture streams, and CH2/CH4 are read through the paired input (see §4).

---

## 3. Transmit Path: UP-DMA Burst

### Register setup per timer

```c
TIMx->CR1  = 0;
TIMx->PSC  = psc;
TIMx->ARR  = 19;                         // 20 ticks/bit
TIMx->CCMR1 = OC1M_PWM1 | OC1PE | OC2M_PWM1 | OC2PE;
TIMx->CCMR2 = OC3M_PWM1 | OC3PE | OC4M_PWM1 | OC4PE;
TIMx->CCER  = CC1E|CC1P | CC2E|CC2P | CC3E|CC3P | CC4E|CC4P; // inverted
TIMx->BDTR  = MOE;                        // TIM1/TIM8 advanced: main output enable
TIMx->DCR   = (3 << 8) | 13;              // DBL=3 (4 transfers), DBA=13 (CCR1 offset 0x34/4)
TIMx->DIER  = UDE;                        // update DMA request
```

TIM1 and TIM8 are advanced-control timers. Without `BDTR.MOE` they output nothing, which is a common bring-up bug.

### Frame build

```c
uint16_t pkt = (throttle << 1) | telem_req;          // 11-bit value + telemetry bit
uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8));
csum = (~csum) & 0xF;                                  // inverted CRC => bidir mode
pkt = (pkt << 4) | csum;

// dma_buffer layout: [bit][channel], 16 bits + 2 trailing zero slots
for (int b = 0; b < 16; b++)
    buf[b*4 + ch] = (pkt & (0x8000 >> b)) ? BIT1_TICKS : BIT0_TICKS;
buf[16*4 + ch] = buf[17*4 + ch] = 0;                  // force line idle at end
```

### DMA stream configuration

| Field | Setting |
|---|---|
| Direction | memory → peripheral |
| Peripheral address | `&TIMx->DMAR` |
| Data width | 32-bit peripheral, 32-bit memory (`CCRx` on H7 is 32-bit wide) |
| Transfers | 18 bits × 4 channels = 72 |
| Mode | normal (not circular) |
| Buffer location | must be in AXI/SRAM1-3, not DTCM. Use `NOCACHE` or clean the D-cache before starting |

On transfer complete, the ISR schedules receive-mode switchover. The ISR does **not** touch the pins yet.

---

## 4. Turnaround: Output → Input Capture

The ESC replies about 30 µs after the frame ends (the GCR frame starts ~25–30 µs later). Switch within roughly 10 µ