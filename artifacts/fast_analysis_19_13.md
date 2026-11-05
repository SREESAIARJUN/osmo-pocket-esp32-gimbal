# Bidirectional DShot600 on STM32H743 TIM1/TIM8: DMA Timer Logic and RPM Decoding

**Scope notes**

- "Cycle 19" does not match any ArduPilot term or release I know. I've treated it as an iteration label. If it refers to a specific branch or document, share the details and I'll adapt this.
- The code below is an engineering sketch that mirrors the structure of ArduPilot's `AP_HAL_ChibiOS/RCOutput*.cpp`. It is not verbatim source.

---

## 1. Clock and Timer Base

On the H743 at 400 MHz SYSCLK, APB2 runs at 100 MHz. TIM1 and TIM8 sit on APB2, so with the ×2 timer multiplier **f_TIM = 200 MHz**.

ArduPilot encodes each DShot bit as 8 timer ticks: a "0" is high for 3 ticks and a "1" is high for 6.

| Quantity | Value |
|---|---|
| Bit rate | 600 kbit/s → T_bit = 1.667 µs |
| Target tick clock | 600k × 8 = 4.8 MHz |
| PSC | round(200 MHz / 4.8 MHz) − 1 = **41** → 4.762 MHz |
| Actual bit rate | 595.2 kbit/s (−0.8%, within ESC tolerance) |
| ARR | 8 − 1 = **7** |
| CCR for "0" / "1" | **3 / 6** |

## 2. Frame Construction (Bidirectional Variant)

The bidirectional variant differs from normal DShot in two ways: the checksum is inverted, and the line idles high.

```c
static inline uint16_t bdshot_frame(uint16_t value, bool telem_req)
{
    uint16_t pkt = (value << 1) | (telem_req ? 1 : 0);
    uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8));
    csum = (~csum) & 0x0F;                 // inverted CRC => bidir request
    return (pkt << 4) | csum;
}
```

Output polarity is inverted (idle high). Use PWM mode 1 with `CCxP=1`, or PWM mode 2. The ESC detects bidirectional mode from the inverted CRC together with the inverted line.

## 3. DMA Output: TIMx_UP Burst via DMAR

All four channels of one timer are updated per bit with a single **UP-request burst** into `TIMx_DMAR`.

```c
// DCR: base = CCR1, burst length = 4 transfers
TIMx->DCR = ((offsetof(TIM_TypeDef, CCR1) / 4) << TIM_DCR_DBA_Pos)
          | ((4 - 1) << TIM_DCR_DBL_Pos);
TIMx->DIER |= TIM_DIER_UDE;
```

**Buffer layout:** 16 bits plus 2 trailing zero slots (to guarantee a clean low/idle), interleaved per channel. That gives 18 × 4 words.

```c
#define DSHOT_BUF_LEN (18 * 4)
static uint32_t dshot_buf[DSHOT_BUF_LEN] __attribute__((aligned(32)));  // in AXI/SRAM1-3, not DTCM

void fill_buffer(uint32_t *buf, uint16_t frame, uint8_t chan)
{
    for (int i = 0; i < 16; i++)
        buf[i*4 + chan] = (frame & (0x8000 >> i)) ? 6 : 3;
    buf[16*4 + chan] = buf[17*4 + chan] = 0;
}
```

**DMAMUX1 request IDs (RM0433):**

| Request | TIM1 | TIM8 |
|---|---|---|
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |
| **UP** | **15** | **51** |

**H7 DMA rules:**
- DMA1 and DMA2 cannot reach DTCM.
- Either `cacheBufferFlush()` before TX and `cacheBufferInvalidate()` after RX, or place the buffers in a non-cacheable MPU region.
- ArduPilot uses the non-cacheable approach.

## 4. Turnaround: Output → Input Capture

When the UP DMA completes (TC ISR), the timer is reconfigured into input capture:

1. Stop the timer and disable UDE.
2. Configure channel n as TI-mapped input with **both-edge capture** (`CCxP=1, CCxNP=1`), so every transition is timestamped.
3. Reconfigure the time base for capture:
   - PSC = 0 or a small value, so tick resolution is much finer than the 1.333 µs GCR bit.
   - ARR = 0xFFFF.
4. Switch the GPIO to input with a pull-up (the line idles high).
5. Arm DMA on the **CHx request** (e.g. TIM1_CH1 = 11), peripheral `CCRx` → memory, half-word.
6. Arm a one-shot timeout (~ 30 µs turnaround + 28 µs frame + margin ≈ 100 µs). On expiry, abort DMA and restore output mode.

**DMA stream limits:** each capturing channel needs its own CHx DMA stream. When `hwdef.dat` lacks enough streams, ArduPilot captures **one channel per output cycle** and rotates through the channels. Each motor's RPM then updates at f_loop / N. Declaring per-channel DMA (the `BIDIR` markers in hwdef) enables simultaneous capture.

## 5. Telemetry Frame Timing

The ESC replies about 30 µs after the frame ends:

- Format: 21-bit GCR at 5/4 × DShot rate = **750 kbit/s**,