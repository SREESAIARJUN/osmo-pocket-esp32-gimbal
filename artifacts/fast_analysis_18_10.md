# Bi-directional DShot600 on STM32H743 TIM1/TIM8 in ArduPilot

This covers DMA request mapping, timer configuration, the TX→RX turnaround, and RPM telemetry decoding. It follows the structure of `AP_HAL_ChibiOS/RCOutput.cpp` and `RCOutput_bdshot.cpp`. I don't know what "Cycle 18" refers to, so I've treated it as a label. If it refers to a specific iteration or constraint, say so and I'll adapt this.

---

## 1. Hardware Mapping (hwdef)

```
# TIM1 – motors 1-4
PE9  TIM1_CH1 TIM1 PWM(1) GPIO(50) BIDIR
PE11 TIM1_CH2 TIM1 PWM(2) GPIO(51)
PE13 TIM1_CH3 TIM1 PWM(3) GPIO(52) BIDIR
PE14 TIM1_CH4 TIM1 PWM(4) GPIO(53)

# TIM8 – motors 5-8
PC6  TIM8_CH1 TIM8 PWM(5) GPIO(54) BIDIR
PC7  TIM8_CH2 TIM8 PWM(6) GPIO(55)
PC8  TIM8_CH3 TIM8 PWM(7) GPIO(56) BIDIR
PC9  TIM8_CH4 TIM8 PWM(8) GPIO(57)

DMA_PRIORITY TIM1* TIM8*
DMA_NOSHARE  TIM1_UP TIM8_UP
```

`BIDIR` marks the channels that get an input-capture DMA stream. ArduPilot conventionally marks CH1/CH3 or CH2/CH4 and pairs them. When streams are scarce, it rotates the capture across channels (see section 5).

### DMAMUX1 Request IDs (RM0433)

| Signal | Req | Purpose |
|---|---|---|
| TIM1_CH1..CH4 | 11..14 | Input-capture DMA (RX) |
| TIM1_UP | 15 | Burst TX via DMAR |
| TIM8_CH1..CH4 | 47..50 | Input-capture DMA (RX) |
| TIM8_UP | 51 | Burst TX via DMAR |

Because of DMAMUX, any DMA1/DMA2 stream can serve these requests. The hwdef DMA resolver assigns the streams, and `STM32_DMA_REQUIRED` and the shared-DMA locks are generated automatically.

---

## 2. Timer Configuration for DShot600

On H743 at 400 MHz, APB2 timer clock = 200 MHz (TIM1/TIM8 sit on APB2).

```
DShot600 bit time   = 1 / 600 kHz = 1.6667 µs
bit_width_ticks     = 20            // ArduPilot DSHOT_BIT_WIDTH_TICKS
timer_freq          = 600k * 20 = 12 MHz
PSC                 = (200 MHz / 12 MHz) - 1  → 15.67

ArduPilot calculates PSC and accepts the rounding (≈ +4% bitrate tolerance is fine for ESCs).
ARR                 = bit_width_ticks - 1 = 19
BIT_1 duty          = 14 ticks  (~75%)
BIT_0 duty          =  7 ticks  (~37%)
```

**Bi-directional specifics:**
- **Inverted polarity.** Set `CCxP = 1` in CCER so the line idles high. This tells the ESC to respond with telemetry.
- **Advanced timer.** Set `BDTR.MOE = 1`, or TIM1/TIM8 outputs stay disabled.
- **Preload.** Set `OCxPE = 1` and `ARPE = 1` so CCR updates from DMA latch on the update event.

### Burst DMA Into CCR1–CCR4

```c
// DCR: DBA = offset of CCR1 in 32-bit words (0x34/4 = 13), DBL = 4 transfers (encoded 3)
TIMx->DCR  = (3U << TIM_DCR_DBL_Pos) | 13U;
TIMx->DIER |= TIM_DIER_UDE;          // UP event drives DMA
// DMA: mem→periph, PSIZE/MSIZE = 32-bit, MINC=1, PINC=0, periph addr = &TIMx->DMAR
```

### Buffer Layout

Each frame is 16 bits plus 2 trailing zero slots, so the line returns to idle and the output is quiet before turnaround. The layout is interleaved per bit:

```c
// dma_buffer[(bit * 4) + chan]
static uint32_t dshot_buf[DSHOT_BUFFER_LENGTH]  // 18*4 words
    __attribute__((section(".axisram"), aligned(32)));
```

The buffer must be in a DMA-reachable RAM. On H7, DTCM is not reachable by DMA1/2; ArduPilot uses AXI SRAM or SRAM1–3. Flush the D-cache before TX (`stm32_cacheBufferFlush`).

### Frame Construction (bidir)

```c
uint16_t pkt = (value << 1) | telem_req;              // 11-bit throttle + T bit
uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8));
csum = (~csum) & 0xF;                                  // INVERTED checksum for bidir
pkt = (pkt << 4) | csum;

for (int i = 0; i < 16; i++)
    buf[i*4 + chan] = (pkt & (0x8000 >> i)) ? BIT_1_TICKS : BIT_0_TICKS;
buf[16*4 + chan] = buf[17*4 + chan] = 0;
```

---

## 3. TX→RX Turnaround Sequence

The ESC replies about 30 µs after the frame ends, at 5/4 the bit rate (750 kbit/s for DShot600, ≈1.333 µs per bit). The reply is 21 bits, roughly 28 µs.

```
dshot_send(group):
  1. lock UP-DMA stream (shared-DMA lock)
  2. cacheFlush(dshot_buf)
  3. TIMx: PWM mode, CCxP inverted, UDE on, start DMA (72 words)
  4. On DMA TC ISR (dma_up_irq_callback):
        - stop timer, UDE off
        - chVTSetI(timeout ≈ 40 µs) → bdshot_receive_pulses_DMAR
        or (preferred) immediately reconfigure:

bdshot_prepare_for_next_pulse / bdshot_config_icu_dshot:
  5. CCMRx: CCxS = 01 (input on TIx), ICxF filter = small (e.g. 0b0010)
  6. CCER : CCxP=1, CCxNP=1 (both edges), CCxE=1
  7. PSC  : set so ticks resolve the 750 kbit/s cell
            (ArduPilot uses telem_rate*... ≈ 8–12 ticks per bit), ARR = 0xFFFF
  8.