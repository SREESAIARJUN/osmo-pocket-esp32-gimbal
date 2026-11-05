# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Before You Build: Three Issues

**1. APEX and 32 kHz accel ODR are incompatible.** The APEX/DMP engine (pedometer, tilt, tap, raise-to-wake, WoM, SMD) runs at a DMP ODR of 25/50/100/500 Hz and expects accel ODR in roughly the 50 Hz to 1 kHz range. Tap detection, for example, needs 200/500/1000 Hz. TDK does not support APEX with accel at 32 kHz. Gyro and accel ODRs are independent, so the practical solution is:
- **Gyro: 32 kHz** (for control loop / vibration analysis)
- **Accel: 1 kHz** (feeds APEX), or 500 Hz for tap and pedometer

**2. "Cycle 4" is not a TDK term.** I don't recognize it from the datasheet or the APEX application note. If it's an internal project phase, a FIFO packet type, or a specific APEX feature set, tell me and I'll adapt this. I've assumed it means your fourth design iteration and have not assigned it any technical meaning.

**3. Verify the numbers.** Register addresses and bit fields below come from the ICM-42688-P datasheet (DS-000347). Check them against your datasheet revision, especially the AC timing values, which I've given as conservative design targets rather than quoted specifications.

---

## 1. System Architecture

**Recommended: two independent SPI buses.** This gives true simultaneous capture, no CS arbitration and fault isolation.

```
MCU SPI1 ──► IMU_A   INT1_A ──► EXTI (DMA trigger)
MCU SPI2 ──► IMU_B   INT1_B ──► EXTI (DMA trigger)
Optional: shared FSYNC / CLKIN (32.768 kHz on pin 9) for synchronized sampling
```

**Alternative: one shared bus** with CS_A and CS_B. This is feasible: the bandwidth budget below shows less than 30% utilization. However, the reads are serialized, which adds about 5 µs of skew between IMUs.

**Clock sync (strongly recommended for dual IMU):** Feed both IMUs an external 32.768 kHz CLKIN, configured via `INTF_CONFIG1.RTC_MODE=1` and pin 9 as CLKIN. Without it, the two internal oscillators drift (±1–2% tolerance), and the 32 kHz streams will slowly walk apart.

---

## 2. SPI Electrical and Timing

| Parameter | Value | Notes |
|---|---|---|
| Mode | 0 (CPOL=0, CPHA=0) or 3 | Mode 3 is used in many reference drivers |
| SCLK max | 24 MHz | Run at **24 MHz** if layout is clean; drop to 12–16 MHz for long traces or a shared bus |
| Bit order | MSB first | |
| Address byte | bit7 = R/W (1 = read), bits6:0 = register | |
| Auto-increment | Yes, on burst reads | FIFO_DATA (0x30) does not increment; it streams |
| CS setup / hold | Design for ≥ 50 ns / ≥ 50 ns | Check the datasheet AC table |
| CS high between transactions | ≥ 100 ns | Conservative |
| SDO valid after SCLK edge | ~20 ns typical | Limits max frequency over long traces |
| VDDIO | 1.71–3.6 V | Match MCU I/O |

**Layout:**
- Keep SCLK/MOSI/MISO under about 5 cm.
- Add 22–33 Ω series resistors at the MCU on SCLK and MOSI.
- Run a solid ground under the traces.
- On the shared-bus option, put CS pull-ups (10 kΩ) on each CS so the idle IMU never floats onto MISO during boot.

**Important:** Disable I²C/I3C on the IMU so SPI noise can't trigger the I²C state machine. This is set via `INTF_CONFIG0` / `DRIVE_CONFIG` in the init sequence.

---

## 3. Bandwidth Budget

FIFO packet sizes:
- **Packet 3** (accel + gyro + temp + timestamp, 16-bit): 16 bytes
- **Packet 4** (20-bit hi-res): 20 bytes

With gyro at 32 kHz and accel at 1 kHz, most packets are gyro-dominant. Budget for the worst case (16 B every 31.25 µs per IMU).

| Item | Per IMU | Both IMUs |
|---|---|---|
| Payload | 512 kB/s = 4.10 Mbit/s | 8.19 Mbit/s |
| At 24 MHz SCLK | 17% bus | Separate buses: 17% each; shared: 34% |

**Transaction strategy (choose one):**

**A. FIFO watermark batching (recommended)**
- Set the watermark to 16 packets (256 B), giving an interrupt every 500 µs at 2 kHz.
- Each read: 1 address byte + 2 count bytes + 256 bytes ≈ 87 µs at 24 MHz.
- CPU interrupt load is low and DMA does everything.

**B. Per-sample DRDY reads (lowest latency)**
- Burst-read from `TEMP_DATA1` (0x1D) through `GYRO_DATA_Z0` (0x2A): 1 + 14 bytes = 5.0 µs at 24 MHz.
- This happens every 31.25 µs, so there are 32k interrupts/s per IMU. That's only viable on a fast MCU (H7/F7 class) with a DMA-chained trigger.

---

## 4. Register Initialization (per IMU)

```c
// Bank 0 unless noted
WR(0x76, 0x00);              // REG_BANK_SEL = 0
WR(0x11, 0x01);              // DEVICE_CONFIG: soft reset
delay_ms(2);
assert(RD(0x75) == 0x47);    // WHO_AM_I

WR(0x4C, 0x30 | 0x40);       // INTF_CONFIG0: big-endian data + FIFO_COUNT in records (bit6)
                             //  verify bit meanings for your rev
WR(0x4D, 0x95);              // INTF_CONFIG1: default; set RTC_MODE (bit2) if using CLKIN

// ODRs and full scale
WR(0x4F, 0x01);              // GYRO_CONFIG0:  ±2000 dps, ODR = 32 kHz
WR(0x50, 0x06);              // ACCEL_CONFIG0: ±16 g,     ODR = 1 kHz (APEX-compatible)

// Filters: at 32 kHz, set gyro UI filter BW appropriately
WR(0x52, 0x11);              // GYRO_ACCEL_CONFIG0: filter BW selection (tune)

// FIFO
WR(0x16, 0x40);              // FIFO_CONFIG: stream-to-FIFO
WR(0x5F, 0x0F);              // FIFO_CONFIG1: accel+gyro+temp+tmst (bit4 = HIRES -> 20 B pkt)
WR(0x60, 16);                // FIFO_CONFIG2: watermark low byte (records if COUNT_REC=1)
WR(0x61, 0x00);              //