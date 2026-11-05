# Bi-Directional DShot600 on STM32H743 TIM1/TIM8: ArduPilot-Style Formulation

This follows the structure of ArduPilot's `RCOutput.cpp` and `RCOutput_bdshot.cpp` in `AP_HAL_ChibiOS`. Constants are representative. Check exact values against your ArduPilot branch.

I couldn't find a specific meaning for "Cycle 17" in ArduPilot. I've read it as one iteration of the per-loop output → turnaround → capture → decode sequence (§3.3). If it refers to something else, such as a specific test step, please clarify.

---

## 1. Hardware Basis (STM32H743)

| Item | Value |
|---|---|
| TIM1/TIM8 bus | APB2 (100 MHz) → timer kernel clock **200 MHz** (×2 multiplier) |
| Channels | 4 per timer, giving 8 motors total (TIM1 CH1–4, TIM8 CH1–4) |
| DMA routing | DMAMUX1, so any DMA1/DMA2 stream can take any request |
| Output DMA request | `TIM1_UP` (DMAMUX req 15), `TIM8_UP` (req 51) |
| Capture DMA requests | `TIM1_CH1..4` (11–14), `TIM8_CH1..4` (47–50) |
| Cache | D-cache enabled. DMA buffers must be in non-cacheable or DMA-safe SRAM (`hal.util->malloc_type(..., MEM_DMA_SAFE)`), or cleaned/invalidated explicitly |

Example hwdef:
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
`BIDIR` on one channel of each pair (CH1/CH2, CH3/CH4) tells the DMA allocator to assign capture streams.

---

## 2. Timing Math (DShot600)

```
bit_rate      = 600 kbit/s  → T_bit = 1.667 µs
ticks_per_bit = DSHOT_BIT_WIDTH_TICKS = 20
bit0_ticks    = 7   (~35% duty)
bit1_ticks    = 14  (~70% duty)

prescaler = round(f_tim / (bit_rate * ticks_per_bit)) - 1
          = round(200e6 / 12e6) - 1 = 16   → actual tick = 85 ns,
            bit = 1.70 µs (≈2% slow, within ESC tolerance)
ARR       = ticks_per_bit - 1 = 19
```

Frame = 16 bits plus 2 trailing zero slots (line reset). Duration ≈ 30.6 µs.

**Bidirectional differences:**
- Output polarity is inverted: idle high, active-low pulses (`CCxP = 1`).
- The checksum is inverted: `crc = (~(v ^ v>>4 ^ v>>8)) & 0x0F`.
- The ESC replies about 30 µs after the frame ends. The reply is 21 GCR bits at 5/4 × bit rate = **750 kbit/s** (1.333 µs/bit).

---

## 3. DMA Request Logic

### 3.1 Output phase (burst DMA through DMAR)

One `TIMx_UP` stream writes all 4 CCRs per update event using DMA burst mode:

```c
// dma_buffer layout: [bit0: CCR1,CCR2,CCR3,CCR4][bit1: ...]...
TIMx->DCR  = (TIM_DMABase_CCR1) | ((4-1) << 8);   // DBA=CCR1 offset, DBL=4 transfers
TIMx->DIER = TIM_DIER_UDE;                         // update → DMA request
dmaStreamSetPeripheral(stream, &TIMx->DMAR);
dmaStreamSetMemory0(stream, dma_buffer);
dmaStreamSetTransactionSize(stream, (16 + 2) * 4);
dmaStreamSetMode(stream, STM32_DMA_CR_DIR_M2P | STM32_DMA_CR_MINC |
                 STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD |
                 STM32_DMA_CR_TCIE | STM32_DMA_CR_PL(3));
```

Filling the buffer:
```c
static void fill_dshot(uint32_t *buf, uint8_t ch, uint16_t packet) {
    for (uint8_t i = 0; i < 16; i++) {
        buf[i*4 + ch] = (packet & 0x8000) ? bit1_ticks : bit0_ticks;
        packet <<= 1;
    }
    buf[16*4 + ch] = buf[17*4 + ch] = 0;   // reset gap
}
// packet = (throttle<<5) | (telem_req<<4) | crc_inverted
```
Clean the D-cache (`cacheBufferFlush`) before starting the transfer.

### 3.2 Turnaround (DMA TC ISR → capture)

When output DMA completes, the ArduPilot callback `dma_up_irq_callback` does the following:

1. Stops `TIMx_UP` DMA and releases the stream if it is shared (`UP_SHARED`).
2. For each BIDIR channel to be captured this cycle:
   - Switches the pin to input with pull-up (the line idles high).
   - Reconfigures the channel for input capture on both edges:
     ```c
     CCMRx: CCxS = 01 (TIx input), ICxF = filter (e.g. 0x2)
     CCER : CCxE=1, CCxP=1, CCxNP=1   // both edges
     PSC  : capture tick ~ 0.1 µs (enough resolution for 1.33 µs bits)
     ARR  : 0xFFFF (free-running)
     DIER : CCxDE = 1                 // capture → DMA
     ```
3. Arms the `TIMx_CHy` capture DMA stream (P2M, 16-bit, `GCR_TELEMETRY_BIT_LEN` ≈ 33 samples).
4. Starts a timeout virtual timer of about `30 + 21×1.33 + margin` ≈ 80 µs.

### 3.3 Channel cycling (one capture per timer per loop)

The H7 has 16 streams, but they are shared with UARTs, SPI, and other peripherals. ArduPilot therefore usually allocates **one capture stream per timer**, or per channel pair. The channel captured rotates each output cycle:

```c
// per timer group
group.bdshot.curr_telem_chan = bdshot_find_next_ic_channel(group);
// e.g. cycle N: CH1, N+1: CH2(via CH1's stream if paired), N+2: CH3, ...
```

Each motor's RPM therefore updates every 2 or 4 output frames. At a 1–2 kHz loop, that is still at least 250 Hz per motor.

### 3.4 Capture complete / timeout ISR

```c
void bdshot_finish_dshot_gcr_transaction(void *p) {
    dmaStreamDisable(ic_stream);
    cacheBufferInvalidate(ic_buf, sizeof(ic_buf));
    uint16_t n = GCR_TELEMETRY_BIT_LEN - dmaStreamGetTransactionSize(ic_stream);
    group.bdshot.erpm[chan] = bdshot_decode_telemetry_packet(ic_buf, n);
    restore_output_mode(group);           // pin → AF, CCMR → PWM1, CCxP inverted, UDE
    chEvtSignalI(thread, EVT_PWM_SEND_NEXT);
}
```

---

## 