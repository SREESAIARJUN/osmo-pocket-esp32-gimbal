# ArduPilot Bi-Directional DShot600 on STM32H743 (TIM1 / TIM8)

This covers DMA request logic, the output/input turnaround, and RPM telemetry decoding, modeled on `AP_HAL_ChibiOS::RCOutput` (`RCOutput.cpp`, `RCOutput_bdshot.cpp`, `RCOutput_serial.cpp`).

I don't have context for what "Cycle 5" refers to. Below I treat it as one full output→capture→decode iteration, with channel rotation where the cycle index matters (§5). If you meant something else, such as a specific revision or test cycle, let me know and I'll adjust.

---

## 1. Timer Clocking and DShot600 Bit Timing

On the H743, ArduPilot runs SYSCLK at 400 MHz. TIM1 and TIM8 sit on APB2, giving a timer kernel clock of **f_TIM = 200 MHz**.

```
DShot600 bitrate      R  = 600 000 bit/s
Bit period            Tb = 1.6667 µs
Ticks per bit (AP)    N  = DSHOT_BIT_WIDTH_TICKS   (default 8)
Bit-0 high time       T0 = DSHOT_BIT_0_TICKS       (default 3  → 37.5 %)
Bit-1 high time       T1 = DSHOT_BIT_1_TICKS       (default 6  → 75 %)

Target counter clock  f_cnt = R · N = 4.8 MHz
Prescaler             PSC = round(f_TIM / f_cnt) − 1 = round(41.67) − 1 = 41
ARR                   = N − 1 = 7
Actual rate           200 MHz / 42 / 8 = 595.2 kbit/s   (−0.8 %, within ESC tolerance)
```

ArduPilot computes this at runtime in `set_group_mode()`. The prescaler is `(clock + target/2) / target − 1`, and the tick counts are scaled if the prescaler error is excessive.

---

## 2. Frame Construction

The 16-bit frame is `[11-bit throttle | 1-bit telemetry request | 4-bit CRC]`.

For bi-directional DShot, the CRC is **inverted**:

```c
uint16_t pkt = (value << 1) | telem_req;
uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8)) & 0x0F;
if (bdshot) csum = ~csum & 0x0F;          // bidir: inverted CRC
pkt = (pkt << 4) | csum;
```

**DMA buffer layout.** The buffer is interleaved for a 4-channel burst, plus 2 trailing zero slots so the line returns to idle:

```c
// dshot_buffer_length = (16 + 2) * 4 * sizeof(uint32_t) = 288 bytes
for (uint8_t b = 0; b < 16; b++) {
    buf[b*4 + chan] = (pkt & 0x8000) ? T1 : T0;
    pkt <<= 1;
}
buf[16*4 + chan] = buf[17*4 + chan] = 0;   // inter-frame gap
```

**Signal inversion.** Bidir DShot idles **high**. ArduPilot therefore uses PWM mode 2 (or sets the CCxP polarity) so that the "active" pulse is low. This is configured in `pwm_cfg.channels[i].mode = PWM_OUTPUT_ACTIVE_LOW`.

---

## 3. DMA Request Logic: Output Phase (TIM_UP Burst)

A single DMA stream per timer, triggered by the **update event**, writes all four CCRs through the DMA burst register.

| Parameter | TIM1 | TIM8 |
|---|---|---|
| DMAMUX1 request (UP) | **15** (`TIM1_UP`) | **51** (`TIM8_UP`) |
| Burst base (DCR.DBA) | CCR1 offset = 0x34/4 = **13** | 13 |
| Burst length (DCR.DBL) | 4 transfers → **3** | 3 |
| Peripheral address | `&TIM1->DMAR` | `&TIM8->DMAR` |
| Stream config | M→P, MSIZE/PSIZE = word, MINC, no PINC | same |

```c
// per send, inside RCOutput::send_pulses_DMAR()
TIMx->DCR  = (3U << 8) | 13U;            // DBL=4, DBA=CCR1
TIMx->DIER |= TIM_DIER_UDE;              // update → DMA request
dmaStreamSetPeripheral(group.dma, &TIMx->DMAR);
dmaStreamSetMemory0(group.dma, group.dma_buffer);
dmaStreamSetTransactionSize(group.dma, 18 * 4);
dmaStreamSetMode(group.dma, STM32_DMA_CR_CHSEL(...) | STM32_DMA_CR_DIR_M2P |
                 STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
                 STM32_DMA_CR_MINC | STM32_DMA_CR_PL(3) | STM32_DMA_CR_TCIE);
TIMx->EGR  = TIM_EGR_UG;                 // preload
TIMx->CR1 |= TIM_CR1_CEN;
dmaStreamEnable(group.dma);
```

**Advanced-timer requirement.** TIM1 and TIM8 need `BDTR.MOE = 1`, or the outputs stay disabled. ChibiOS PWM sets this, but bidir reconfiguration must preserve it.

**H7 cache coherency.** The DMA buffer must come from non-cacheable memory: `hal.util->malloc_type(len, AP_HAL::Util::MEM_DMA_SAFE)`, which uses SRAM1–3 or AXI with an MPU no-cache region. Otherwise call `SCB_CleanDCache_by_Addr()` before enabling the stream.

**DMA stream sharing.** If the UP stream is shared with another peripheral, `group.dma_handle->lock()` (Shared_DMA) serializes access. TIM1/TIM8 UP streams are normally given dedicated streams in `hwdef.dat` (`DMA_PRIORITY TIM1* TIM8*`).

---

## 4. Turnaround: Output → Input Capture

The DMA TC interrupt (`dma_up_irq_callback`) starts the turnaround:

1. **Stop the output.** Stop the timer and disable the UP DMA.
2. **Reconfigure the channel for input capture.**
   - `CCMRx.CCxS = 01` (IC mapped on TIy), filter `ICxF` = 0 or small.
   - Capture on **both edges**: `C