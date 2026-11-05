# Bi-directional DShot600 on STM32H743 TIM1 and TIM8 in ArduPilot

This follows the structure of ArduPilot's ChibiOS `RCOutput` / `RCOutput_bdshot.cpp`. I'm not sure what "Cycle 21" refers to. If it is your own iteration label, the logic below applies unchanged. If it means something specific, such as a scheduler slot or test cycle, let me know.

---

## 1. Clock and Bit Timing

| Parameter | Value |
|---|---|
| SYSCLK | 400 MHz (ArduPilot H743 default) |
| TIM1/TIM8 bus | APB2. Timer kernel clock = 200 MHz (APB2 = 100 MHz, ×2) |
| DShot600 bitrate | 600 kbit/s, giving a bit period of 1.667 µs |
| Ticks per bit | `DSHOT_BIT_WIDTH_TICKS = 20` (high-resolution mode) |
| Target timer frequency | 600k × 20 = 12 MHz |
| Prescaler | PSC = 200 MHz / 12 MHz − 1 ≈ 15.67, which is not an integer |

Because the ratio is not an integer, ArduPilot computes the prescaler and then recomputes the actual bitrate:

```c
psc         = (timer_clock + target_freq/2) / target_freq - 1;   // -> 16 (÷17)
actual_freq = timer_clock / (psc + 1);                           // 11.765 MHz
ARR         = DSHOT_BIT_WIDTH_TICKS - 1;                         // 19
bit_0_ticks = 7;   // ~37.5% duty
bit_1_ticks = 14;  // ~75% duty
```

- **Effective bitrate:** about 588 kbit/s. ESCs tolerate roughly ±5%.
- **Frame time:** 16 bits × 1.70 µs ≈ 27.2 µs, plus 2 reset slots of zero duty.

## 2. Bi-directional Signalling: Inverted Output

In bi-directional mode the line idles **high**, and the CRC is inverted.

**Output-compare configuration:**
- CCMRx: OCxM = PWM mode 1, with CCER.CCxP = 1 (active-low polarity).
- This is equivalent to PWM mode 2 with normal polarity.
- TIM1 and TIM8 are advanced timers. You must also set **BDTR.MOE = 1**, or the outputs stay disabled.

**Packet construction:**
```c
uint16_t pkt = (value << 1) | telem_req;          // 11-bit throttle + T bit
uint16_t csum = (pkt ^ (pkt >> 4) ^ (pkt >> 8));
csum = ~csum;                                     // bidir: inverted CRC
pkt = (pkt << 4) | (csum & 0x0F);
```

## 3. Output DMA: Burst Write via TIMx_UP

A single **UP** DMA request feeds all four channels of one timer in one burst, using DCR/DMAR.

```c
// DMA base address = CCR1 offset (0x34) / 4 = 13, burst length = 4 transfers
TIMx->DCR = (3U << TIM_DCR_DBL_Pos) | (13U << TIM_DCR_DBA_Pos);
TIMx->DIER |= TIM_DIER_UDE;
// DMA stream: memory -> &TIMx->DMAR, 32-bit, MINC, length = 18 * 4
```

**Buffer layout:** interleaved by bit index. There are 16 data bits plus 2 trailing zero words, which force the line back to idle.

```c
uint32_t dma_buf[(16 + 2) * 4];   // [bit][chan], must be in DMA-capable SRAM (AXI/SRAM1-3, not DTCM)
for (bit = 0; bit < 16; bit++)
    dma_buf[bit*4 + chan] = (pkt & (0x8000 >> bit)) ? bit_1_ticks : bit_0_ticks;
```

On H7 you also need **D-cache maintenance**: clean the buffer before TX and invalidate it after RX. ArduPilot avoids this by using non-cacheable memory, which is the safer choice.

### DMAMUX1 Request IDs (H743)

| Request | TIM1 | TIM8 |
|---|---|---|
| CH1 | 11 | 47 |
| CH2 | 12 | 48 |
| CH3 | 13 | 49 |
| CH4 | 14 | 50 |
| UP | 15 | 51 |

In `hwdef.dat`, declare `BIDIR` on the PWM lines so the generator allocates the CHx streams.

## 4. Turnaround: Output to Input Capture

The ESC replies about **30 µs** after the end of the frame. The reply uses GCR at **5/4 × bitrate** (about 750 kbit/s, roughly 1.33 µs per bit) and is 21 bits long.

**Sequence, triggered in the TX DMA completion callback:**

1. Stop the UP DMA and clear UDE.
2. Reconfigure the channel:
   - CCMRx.CCxS = 01 (input capture on TIx).
   - Input filter IC1F suited to the higher rate.
   - Both edges: CCER.CCxP = 1, CCxNP = 1.
3. Retune the timer for capture resolution. ArduPilot uses a free-running counter with PSC chosen for about 1/8 of a telemetry bit per tick, and ARR = 0xFFFF.
4. Point the CHx DMA stream:
   - Source: `&TIMx->CCRx`, 16-bit.
   - Destination: `uint16_t ic_buf[GCR_TELEMETRY_BIT_LEN]`, with roughly 28 edge slots plus margin.
5. Set DIER.CCxDE and enable the counter.
6. Start a timeout of about 40–50 µs worth of edges. On the next output cycle, abort and restore the output-compare configuration.

**When IC streams are scarce:** if there are not enough DMA streams for 4×CHx per timer, ArduPilot captures **one channel per output cycle** and rotates `curr_telem_chan`. Each motor then reports RPM at (loop rate / 4). With TIM1 and TIM8 both used for 8 motors, this rotation is typical.

## 5. RPM Telemetry Decoding

```c
// 1. Edge timestamps -> bit run-lengths -> 21-bit GCR word
uint32_t decode_telemetry(const uint16_t *ic, uint8_t n, float ticks_per_bit) {
    uint32_t value = 0, bits = 0;
    uint16_t prev = ic[0];
    for (uint8_t i = 1; i < n && bits < 21; i++) {
        uint16_t dt  = ic[i] - prev;            // wraps correctly in uint16
        uint8_t  len = (dt + ticks_per_bit/2) / ticks_per_bit;   // 1..3 normally
        value <<= len;
        value |= 1U << (len - 1);               // transition marks a '1'
        bits  += len;
        prev = ic[i];
    }
    if (bits < 21) value <<= (21 - bits);      // trailing idle bits
    return value;
}

// 2. NRZI -> GCR (20 bits)
uint32_t gcr = (value ^ (value >> 1)) & 0xFFFFF;

// 3. GCR 5b -> 