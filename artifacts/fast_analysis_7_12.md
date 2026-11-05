# Bi-directional DShot600 on STM32H743 TIM1/TIM8 (ArduPilot-style)

This describes how ArduPilot's ChibiOS `RCOutput` + `RCOutput_bdshot` path drives bi-directional DShot600 on the H743 advanced timers and decodes eRPM telemetry. It covers clocking, DMAMUX request mapping, the transmit DMA burst, the switch to input capture, and GCR decoding.

I have taken "Cycle 7" to mean one pass of the per-loop output/telemetry state machine, including the telemetry channel rotation. If it refers to something else in your curriculum, the stage logic below still applies.

---

## 1. Clock and timing basis

| Item | Value |
|---|---|
| SYSCLK / HCLK | 400 MHz / 200 MHz |
| APB2 (TIM1, TIM8 bus) | 100 MHz, so timer kernel clock f_TIM = 200 MHz |
| DShot600 bit rate | 600 kbit/s, T_bit = 1.667 µs |
| Bit encoding (ArduPilot default) | width = 8 ticks, '0' = 3 ticks, '1' = 6 ticks |
| Telemetry bit rate | 5/4 × 600k = 750 kbit/s, T_tbit = 1.333 µs |

**Prescaler for TX:**

```
f_target = 600e3 * 8 = 4.8 MHz
PSC      = round(200e6 / 4.8e6) - 1 = 41 - 1 = 40   → f_cnt = 4.878 MHz
ARR      = 8 - 1 = 7                                → 609.8 kbit/s (+1.6%, within ESC tolerance)
```

ArduPilot computes this at runtime from `_timer_clock` and logs the real rate.

**One frame budget:**

```
TX frame      : 16 bits × 1.667 µs        ≈ 26.7 µs (+ 2 padding zero slots)
ESC turnaround: ~30 µs (line released, pulled high)
RX frame      : 21 bits × 1.333 µs        ≈ 28 µs
Total         : ≈ 90–100 µs  → leaves headroom even at 2 kHz loop / 8 kHz-capable output
```

---

## 2. DMAMUX1 request IDs (STM32H743)

| Timer | CH1 | CH2 | CH3 | CH4 | UP |
|---|---|---|---|---|---|
| TIM1 | 11 | 12 | 13 | 14 | **15** |
| TIM8 | 47 | 48 | 49 | 50 | **51** |

- **TX** uses one stream per timer, triggered by **TIMx_UP**, writing a burst via `DMAR`.
- **RX** needs a **TIMx_CHy** capture request per channel.

There are only 16 DMA streams across DMA1 and DMA2. ArduPilot therefore either:
- (a) assigns dedicated CH streams when the hwdef provides them (`BDSHOT` with per-channel DMA), or
- (b) **re-points the UP stream's DMAMUX request** to the CHy request after TX and rotates which channel is captured each cycle. This is the "one channel telemetry per cycle" mode.

---

## 3. TX: DShot packet and DMA burst

### Packet (bi-directional = inverted CRC)

```c
uint16_t dshot_pkt(uint16_t value, bool telem_req) {
    uint16_t p   = (value << 1) | (telem_req ? 1 : 0);   // 11-bit throttle + T
    uint16_t csum = (p ^ (p >> 4) ^ (p >> 8));
    csum = ~csum;                                         // bidir: inverted checksum
    return (p << 4) | (csum & 0xF);
}
```

### Buffer layout (4 channels interleaved for DMA burst)

```c
#define BIT_0  3
#define BIT_1  6
#define NBITS  (16 + 2)          // 2 trailing zero slots → line returns idle
uint32_t dma_buf[NBITS * 4];     // word per CCR, 32-bit for DMAR

for (b = 0; b < 16; b++)
  for (ch = 0; ch < 4; ch++)
     dma_buf[b*4 + ch] = (pkt[ch] & (0x8000 >> b)) ? BIT_1 : BIT_0;
// tail slots = 0 (no pulse)
```

### Timer config (TX phase)

```c
TIMx->PSC  = 40;
TIMx->ARR  = 7;
TIMx->CCMR1/2 : OCxM = PWM mode 1, OCxPE = 1
TIMx->CCER : CCxE = 1, CCxP = 1          // INVERTED output: idle HIGH for bidir
TIMx->BDTR |= TIM_BDTR_MOE;              // required on TIM1/TIM8
TIMx->DCR  = (3 << 8) | (0x34 >> 2);     // DBL=3 (4 xfers), DBA=CCR1 offset (13)
TIMx->DIER = TIM_DIER_UDE;               // UP event → DMA burst
```

### DMA stream (TX)

```c
DMAMUX1_Channel[s]->CCR = 15 /*TIM1_UP*/  // or 51 for TIM8
stream->PAR  = (uint32_t)&TIMx->DMAR;
stream->M0AR = (uint32_t)dma_buf;        // must be in DMA-capable SRAM (AXI/SRAM1-3, not DTCM)
stream->NDTR = NBITS * 4;
stream->CR   = MEM2PERIPH | MINC | PSIZE_32 | MSIZE_32 | TCIE | PL_VHIGH;
// clean D-cache over dma_buf before enable (SCB_CleanDCache_by_Addr)
```

