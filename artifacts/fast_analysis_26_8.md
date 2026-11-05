# ArduPilot Bi-Directional DShot600 on STM32H743: TIM1 and TIM8

This describes how ArduPilot's ChibiOS HAL (`RCOutput.cpp` and `RCOutput_bdshot.cpp`) drives bi-directional DShot600 on the H743 advanced timers. Exact constants and function names vary between ArduPilot releases, so check them against the source tree you are building.

I don't know of any ArduPilot convention called "Cycle 26." The sections below cover one complete output-and-telemetry cycle. If "Cycle 26" refers to something specific in your project, point me to it and I'll align the answer.

---

## 1. Clock and Bit Timing

TIM1 and TIM8 sit on APB2. With a 400 MHz SYSCLK and the APB2 prescaler set to 2, the timer clock is:

- **f_TIM = 200 MHz**

**DShot600 output**

- Bit period: T_bit = 1 / 600 kHz = 1.6667 µs
- ArduPilot uses a fixed tick grid per bit. Typically:
  - 20 ticks per bit
  - "0" bit = 7 ticks high (35%)
  - "1" bit = 14 ticks high (70%)
- Required count rate: f_cnt = 600 kHz × 20 = 12 MHz
- Prescaler: PSC = f_TIM / f_cnt − 1 = 200 MHz / 12 MHz − 1 ≈ 15.67, which rounds to **15**
  - Actual count rate is 12.5 MHz.
  - Actual bit rate is about 625 kbit/s.
  - ESCs tolerate this error. ArduPilot computes the prescaler at runtime from the measured timer clock.
- ARR = 20 − 1 = **19**

**Telemetry return (GCR)**

- Bit rate: 600 × 5/4 = **750 kbit/s**, so T_gcr = 1.333 µs
- Frame length: 21 bits, about 28 µs

**Cycle budget per timer group**

| Phase | Duration |
|---|---|
| Command frame (16 bits) | 26.7 µs |
| Reset/pad slots | ~3 µs |
| ESC turnaround | ~30 µs |
| GCR reply | ~28 µs |
| Capture margin | ~10–15 µs |
| **Total** | **≈ 100 µs** |

TIM1 and TIM8 are serviced as separate groups. They can overlap only if they do not share a DMA stream.

---

## 2. DMA Request Mapping (H743 DMAMUX1)

| Timer | UP (burst out) | CH1 | CH2 | CH3 | CH4 |
|---|---|---|---|---|---|
| TIM1 | 15 | 11 | 12 | 13 | 14 |
| TIM8 | 51 | 47 | 48 | 49 | 50 |

- **Output** uses one stream per timer, triggered by **TIMx_UP**. It runs in DMA burst mode to `TIMx_DMAR`.
- **Input capture** uses **TIMx_CHn** requests, one stream per captured channel.
  - If streams are scarce, ArduPilot captures telemetry from one channel per cycle in round-robin order.
  - The `BIDIR` hwdef flag tells the DMA resolver which channels need a capture stream.

**Example hwdef**

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
DMA_NOSHARE  TIM1* TIM8*
```

ArduPilot marks alternating channels as `BIDIR`, as above. During capture, the paired channel (CH1/CH2 or CH3/CH4) can route its input through the partner's capture register (indirect TI mapping, CCxS = 10), which saves streams.

---

## 3. Output DMA Burst Logic

**Register setup**

```
TIMx->DCR  = (TIM_DMABURSTLENGTH_4TRANSFERS) | (offsetof(CCR1)/4)   // DBL=3, DBA=CCR1
TIMx->DIER |= TIM_DIER_UDE;           // update event -> DMA request
TIMx->CCMRx: OCxM = PWM mode 1, OCxPE = 1 (preload)
TIMx->CCER : CCxP = 1                 // INVERTED polarity for bidir (idle high)
TIMx->BDTR |= TIM_BDTR_MOE;           // required on advanced timers TIM1/TIM8
```

**Buffer layout**

The buffer is interleaved per bit. Each update event moves one bit for all four channels:

```c
// buffer[(bit * 4) + chan], 16 data bits + 2 zero pad slots
for (bit = 0; bit < 16; bit++) {
    uint16_t mask = 0x8000 >> bit;
    for (ch = 0; ch < 4; ch++)
        buf[bit*4 + ch] = (pkt[ch] & mask) ? BIT_1_TICKS : BIT_0_TICKS;
}
buf[64..71] = 0;   // trailing idle so the line returns to idle level
```

**DMA stream configuration**

- Memory-to-peripheral, 32-bit peripheral width, 16- or 32-bit memory width
- Memory increment on, peripheral increment off
- Peripheral address = `&TIMx->DMAR`
- On the H7, the buffer must sit in DMA-capable SRAM (AXI SRAM or SRAM1–3, not DTCM). It must also be cache-cleaned before the transfer (`cacheBufferFlush`).

**Packet construction (bidir)**

```c
pkt = (throttle << 1) | telem_req;          // 11-bit value + telemetry bit
csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8));
csum = ~csum & 0xF;                          // inverted CRC signals bidirectional mode
pkt = (pkt << 4) | csum;
```

---

## 4. Turnaround: Output to Input Capture

This happens in the DMA transfer-complete ISR, or in a chained callback.

1. Stop the UP DMA and set `TIMx->DIER &