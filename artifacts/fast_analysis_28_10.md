# ArduPilot Bi-Directional DShot600 on STM32H743 (TIM1 / TIM8)

> **Note on "Cycle 28":** This isn't an ArduPilot term I recognize. If it refers to a specific release, design iteration, or internal document, tell me what it changes. The logic below follows the upstream `AP_HAL_ChibiOS` implementation (`RCOutput.cpp`, `RCOutput_bdshot.cpp`, `bdshot` decode).

---

## 1. Timing Budget

| Parameter | DShot600 |
|---|---|
| Bit rate (TX) | 600 kbit/s → 1.667 µs/bit |
| TX frame | 16 bits ≈ 26.7 µs (+ 2 padding slots) |
| ESC turnaround | ~30 µs (line released, pulled high) |
| Reply bit rate | 5/4 × 600k = **750 kbit/s** → 1.333 µs/bit |
| Reply length | 21 bits (GCR) ≈ 28 µs |
| **Total per cycle** | **≈ 90–100 µs** (sets the upper bound on output rate) |

### Timer clock on H743

- TIM1 and TIM8 sit on APB2. With D2PPRE2 = /2 the timer kernel clock is 200 MHz.
- ArduPilot targets `DSHOT_BIT_WIDTH_TICKS = 20` ticks per bit, so the desired counter frequency is:
  - f_cnt = 600 k × 20 = 12 MHz
  - PSC = round(200 MHz / 12 MHz) − 1 = 16 − 1 = **15** → actual f_cnt = 12.5 MHz
- ArduPilot recalculates the actual bit rate from the rounded prescaler. With ARR = 19, the result is ≈ 625 kbit/s. ESCs tolerate roughly ±5–10 %, but check the rounding on your clock tree.
- Duty values: `BIT_0 = 7` ticks, `BIT_1 = 14` ticks (≈ 37 % / 75 %).

---

## 2. DMA Request Mapping (DMAMUX1)

| Timer | TX request (burst) | RX capture requests |
|---|---|---|
| TIM1 | `TIM1_UP` = 15 | `TIM1_CH1..CH4` = 11, 12, 13, 14 |
| TIM8 | `TIM8_UP` = 51 | `TIM8_CH1..CH4` = 47, 48, 49, 50 |

**TX:** one UP-triggered DMA stream per timer uses DMA burst mode into `TIMx_DMAR`. This updates CCR1–CCR4 on every update event.

**RX:** each channel needs its own CCx DMA stream, because input capture cannot ride on the UP burst. The H7 has 16 streams across DMA1/DMA2 via DMAMUX, so ArduPilot can usually allocate them. If streams run short, it falls back to **round-robin telemetry**, capturing one channel per timer per cycle (`bdshot.curr_telem_chan`).

### hwdef example

```
# Motor outputs, BIDIR enables input-capture DMA allocation
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

Mark `BIDIR` on alternate channels (1/3). ArduPilot pairs CH1/CH2 and CH3/CH4 so that one CCx DMA can serve the pair, since they share capture inputs via TI1/TI2 cross-mapping.

---

## 3. TX Path: Frame Build and DMA Burst

### Frame encoding (inverted CRC for bidirectional)

```cpp
uint16_t create_dshot_packet(uint16_t value, bool telem_req, bool bidir)
{
    uint16_t pkt = (value << 1) | (telem_req ? 1 : 0);   // 11b throttle + 1b T
    uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8));
    if (bidir) csum = ~csum;                              // inverted CRC signals BDShot
    return (pkt << 4) | (csum & 0x0F);
}
```

### Interleaved burst buffer

The buffer holds 18 slots × 4 channels. Slots 16 and 17 are zeros, which leaves the line idle.

```cpp
void fill_DMA_buffer_dshot(uint32_t *buf, uint8_t stride, uint16_t pkt, uint16_t clockmul)
{
    const uint32_t b0 = BIT_0_TICKS * clockmul, b1 = BIT_1_TICKS * clockmul;
    for (int i = 0; i < 16; i++) {
        buf[i * stride] = (pkt & 0x8000) ? b1 : b0;
        pkt <<= 1;
    }
    buf[16 * stride] = buf[17 * stride] = 0;
}
// call once per channel with buf = &dma_buffer[chan_index], stride = 4
```

### Timer and DMA configuration for send

```cpp
void dshot_send_groups(pwm_group &g)
{
    TIM_TypeDef *T = g.pwm_drv->tim;

    // Output, PWM mode, inverted polarity (idle HIGH for bidirectional)
    for (ch : 0..3) set CCxP = 1;        // or use PWM mode 2
    T->PSC  = prescaler;                 // 15 → 12.5 MHz
    T->ARR  = DSHOT_BIT_WIDTH_TICKS - 1;
    T->DCR  = STM32_TIM_DCR_DBA(0x0D)    // offset of CCR1 (0x34/4)
            | STM32_TIM_DCR_DBL(3);      // 4 transfers per burst
    T->DIER = STM32_TIM_DIER_UDE;        // DMA on update

    dmaStreamSetPeripheral(g.dma, &T->DMAR);
    dma