# ArduPilot Bi-Directional DShot600 on STM32H743: TIM1 and TIM8 DMA and RPM Telemetry

**Interpretation of "Cycle 2":** I've read this as the second telemetry capture slot in ArduPilot's round-robin, i.e. channel index 2 (CH3) on each timer. When no dedicated input-capture DMA stream exists for every channel, ArduPilot cycles telemetry capture across a timer's channels, one per DShot frame. If you meant something else, such as a design iteration, the timing, DMA and decode sections below still apply unchanged.

**Accuracy caveat:** The code below is a simplified, ArduPilot-style formulation, not verbatim ArduPilot source. It follows the logic of `RCOutput.cpp`, `RCOutput_bdshot.cpp` and `RCOutput_serial.cpp` in `AP_HAL_ChibiOS`. Before relying on specific values, check them against your ArduPilot version and the STM32H743 reference manual (RM0433). This applies especially to the DMAMUX request IDs, register offsets and the compile-time tick constants.

---

## 1. Clock and Timing Basis

| Parameter | Value |
|---|---|
| SYSCLK (ArduPilot H743 default) | 400 MHz |
| APB2 / TIM1, TIM8 kernel clock | 100 MHz / **200 MHz** (×2 multiplier) |
| DShot600 bit rate | 600 kbit/s, giving T_bit = 1.667 µs |
| Bit width (high-resolution mode) | 20 ticks: "0" = 7 ticks, "1" = 14 ticks |
| Telemetry bit rate (5/4 × DShot) | 750 kbit/s, giving T_gcr = 1.333 µs |

**Output prescaler.** ArduPilot computes the prescaler at runtime:

```
psc = round(f_tim / (rate_bps * bit_width_ticks)) - 1
    = round(200e6 / (600e3 * 20)) - 1 = 17 - 1 = 16
```

- The resulting bit rate is 200e6 / (17 × 20) = 588 kbit/s.
- That is about 2% slow, which is within ESC tolerance.
- ARR is set to `bit_width_ticks - 1 = 19`.

**Input-capture prescaler.** This is chosen for about 8 or more ticks per GCR bit, so that edge quantisation stays well below T_gcr/2.

- Using psc = 0 (5 ns resolution) with a 16-bit CNT gives 327 µs of range. This is ample for a roughly 30 µs reply.
- ArduPilot typically uses a lower capture rate to keep the buffer counts small. Either choice works if the decoder normalises by ticks-per-bit.

---

## 2. Frame Construction (Inverted DShot)

```c
static inline uint16_t create_dshot_packet(uint16_t value, bool telem_req, bool bidir)
{
    uint16_t pkt = (value << 1) | (telem_req ? 1 : 0);
    uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8)) & 0xF;
    if (bidir) {
        csum = ~csum & 0xF;        // inverted CRC signals bidir mode to the ESC
    }
    return (pkt << 4) | csum;
}

// Fill the DMA buffer: 16 bits plus 2 trailing zero slots (line returns to idle)
// The buffer is interleaved per timer channel for the DMAR burst
static void fill_dma_buffer(uint32_t *buf, uint8_t stride, uint16_t pkt, uint8_t chan)
{
    const uint32_t T0 = DSHOT_BIT_0_TICKS;   // 7
    const uint32_t T1 = DSHOT_BIT_1_TICKS;   // 14
    for (uint8_t i = 0; i < 16; i++) {
        buf[i*stride + chan] = (pkt & 0x8000) ? T1 : T0;
        pkt <<= 1;
    }
    buf[16*stride + chan] = 0;
    buf[17*stride + chan] = 0;
}
```

**Signal polarity.** For bidirectional DShot, the line idles **high** and the pulses are active-low. This is achieved with `CCER.CCxP = 1` (inverted polarity) in PWM mode 1, so that CCR=0 holds the line at idle-high.

---

## 3. DMA Request Topology (DMAMUX1)

| Timer | UP (burst output) | CH1 | CH2 | CH3 | CH4 |
|---|---|---|---|---|---|
| TIM1 | 15 | 11 | 12 | **13** | 14 |
| TIM8 | 51 | 47 | 48 | **49** | 50 |

**Output phase.** A single UP-triggered DMA stream per timer writes CCR1 to CCR4 in one burst through the DMAR register:

```c
// DCR: DBA = offset of CCR1 / 4, DBL = (channels - 1)
TIMx->DCR  = (STM32_TIM_DCR_DBA(0x34 >> 2)) | STM32_TIM_DCR_DBL(3);  // 4 transfers per UEV
TIMx->DIER = TIM_DIER_UDE;
dmaStreamSetPeripheral(stream_up, &TIMx->DMAR);
dmaStreamSetMemory0(stream_up, dma_buffer);
dmaStreamSetTransactionSize(stream_up, 18 * 4);       // 18 slots × 4 channels
dmaStreamSetMode(stream_up, STM32_DMA_CR_CHSEL(...) | STM32_DMA_CR_DIR_M2P |
                 STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
                 STM32_DMA_CR_MINC | STM32_DMA_CR_PL(3) | STM32_DMA_CR_TCIE);
```

**Input phase, Cycle 2.** The channel DMA request for CH3 is used: TIM1_CH3 is 13 and TIM8_CH3 is 49. It reads `CCR3` on every capture event.

In `hwdef.dat`:

```
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PC8  TIM8_CH3 TIM8 PWM(7) GPIO(56) BIDIR
DMA_PRIORITY TIM1* TIM8*
```

**DMA stream sharing.** If the UP stream and the CHn stream are the same physical stream, ArduPilot reallocates it between phases. In that case it uses `shared_dma` with lock and unlock around TX and RX.

---

## 4. Per-Frame State Machine (One Timer)

```
          ┌──────────── dshot_send() at loop rate (e.g. 1–8 kHz) ─────────────┐
          