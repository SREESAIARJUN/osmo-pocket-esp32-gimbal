# Dual ICM-42688-P: SPI Timing and DMA Configuration at 32 kHz with APEX

## Unresolved Questions

**1. "APEX motion engine (Cycle 1)" is not a term I recognize.** It does not appear in TDK's ICM-42688-P documentation. It might be your project's phase naming, a TDK eMD/driver version, or a mix-up with another part. Please clarify what it means.

**2. APEX and 32 kHz may be incompatible.** The APEX/DMP features (pedometer, tilt, tap, raise-to-wake, SMD, WoM) run at a DMP ODR of 25, 50, 100, or 500 Hz. They have specific accelerometer ODR and power-mode requirements, and tap detection is tied to particular accel ODRs. Running the accelerometer at 32 kHz LN while APEX is active is likely unsupported or produces undefined APEX behavior. Typical solutions:
- Gyro at 32 kHz and accel at an APEX-compatible ODR, or
- 32 kHz on IMU A, with APEX on IMU B at a lower ODR, or
- Doing motion detection in firmware from the 32 kHz stream.

Check the APEX section of datasheet DS-000347 and TDK's APEX application note before committing.

**3. No MCU was specified.** The DMA section below uses an STM32H7/F7-style architecture as an example. Tell me your actual MCU and I'll adapt it.

Register values below are from memory of the datasheet. **Verify every address and bitfield against the current DS-000347 revision** before use.

---

## 1. Bus Topology

**Recommended: one SPI peripheral per IMU.**
```
MCU SPI1 ── IMU_A (CS_A, INT1_A → EXTI)
MCU SPI2 ── IMU_B (CS_B, INT1_B → EXTI)
Optional: shared external CLKIN (RTC/CLKIN pin) for sample-time alignment
```
Separate buses give deterministic, parallel transfers with no CS arbitration jitter. A shared bus is possible (budget below) but serializes the reads.

## 2. SPI Electrical/Timing Parameters

| Parameter | Value |
|---|---|
| Mode | 0 (CPOL=0, CPHA=0) or 3; mode 3 is common |
| Max SCLK | 24 MHz (verify) |
| Practical SCLK | 12–24 MHz, depending on trace length and signal integrity |
| Bit order | MSB first |
| Read flag | Address bit 7 = 1 |
| Auto-increment | Yes, for burst reads |
| CS setup/hold | Tens of ns. Use the datasheet tCSS/tCSH values, and insert ≥100 ns guard in firmware |
| Interface select | Disable I²C via INTF_CONFIG0/INTF_CONFIG6 per the datasheet, to avoid mode ambiguity |

## 3. Bandwidth Budget (per IMU)

Sample period at 32 kHz = **31.25 µs**.

| Read method | Bytes/transfer | Time @ 24 MHz | Time @ 12 MHz |
|---|---|---|---|
| Direct register burst (0x1D–0x2A: temp + accel + gyro) | 1 + 14 = 15 | 5.0 µs | 10.0 µs |
| FIFO packet 3 (16 B), single | 1 + 16 = 17 | 5.7 µs | 11.3 µs |
| FIFO, 16-packet watermark | 1 + 256 = 257 | ~86 µs (every 500 µs) | ~171 µs |

- Raw data rate is 32k × 16 B = 512 kB/s, about 4.1 Mbit/s per IMU.
- Two IMUs on one bus at 24 MHz use about 35% of the bus, which is feasible.
- At 12 MHz on one bus, utilization reaches about 70%. Use separate buses instead.

**Use the FIFO, not per-sample interrupts.** A 32 kHz interrupt rate on two IMUs (64k IRQ/s) is wasteful and jitter-sensitive. A watermark gives batching, timestamps, and tolerance to latency.

## 4. ICM-42688-P Register Configuration (Bank 0 Unless Noted)

```c
// Bank select: REG_BANK_SEL = 0x76
#define REG_BANK_SEL     0x76
#define DEVICE_CONFIG    0x11  // bit0 SOFT_RESET
#define INT_CONFIG       0x14
#define FIFO_CONFIG      0x16  // bits7:6 mode: 01 = stream
#define INTF_CONFIG0     0x4C
#define PWR_MGMT0        0x4E
#define GYRO_CONFIG0     0x4F  // [7:5] FS_SEL, [3:0] ODR
#define ACCEL_CONFIG0    0x50
#define FIFO_CONFIG1     0x5F
#define FIFO_CONFIG2     0x60  // watermark [7:0]
#define FIFO_CONFIG3     0x61  // watermark [11:8]
#define INT_SOURCE0      0x65
#define WHO_AM_I         0x75  // expect 0x47

init_sequence:
 1. write DEVICE_CONFIG = 0x01          ; soft reset, wait >= 1 ms
 2. read  WHO_AM_I == 0x47
 3. write INT_CONFIG    = 0x03          ; INT1 push-pull, active-high, pulsed (choose to suit)
 4. write INTF_CONFIG0  = 0x30 (verify) ; big-endian sensor data, FIFO count in records
 5. write GYRO_CONFIG0  = 0x01          ; ±2000 dps, ODR = 32 kHz
 6. write ACCEL_CONFIG0 = 0x01          ; ±16 g, ODR = 32 kHz  (see APEX caveat)
 7. write FIFO_CONFIG1  = 0x07 (+bit5 WM_GT_TH) ; accel+gyro+temp → packet 3 (16 B)
 8. write FIFO_CONFIG2/3 = 16           ; watermark = 16 records
 9. write FIFO_CONFIG   = 0x40          ; stream-to-FIFO
10. write INT_SOURCE0   = FIFO_THS_INT1_EN (bit2)
11. write PWR_MGMT0     = 0x0F          ; gyro LN + accel LN (32 kHz requires LN)
12. wait  >= 45 ms gyro start-up; discard first samples
```

Also configure anti-alias/UI filter bandwidth (GYRO_CONFIG1, GYRO_ACCEL_CONFIG0, and the Bank 1/2 AAF registers) for your application. At 32 kHz the defaults may not be what you want.

**APEX (if compatible with your chosen ODRs):** set DMP ODR in APEX_CONFIG0 (bank 0, ~0x56). Set feature enables and thresholds in APEX_CONFIG1–9 (bank 4), and run DMP init via SIGNAL_PATH_RESET (DMP_INIT_EN, DMP_MEM_RESET). Route APEX interrupts to **INT2**, so the FIFO DMA path on INT1 is never delayed by APEX status reads.

## 5. DMA Architecture (STM32-Style Example)

Per IMU (×2, on independent SPI + DMA streams):

```
INT1 (FIFO watermark) ──EXTI──► ISR:
    CS low
    start DMA: TX = [0x2E|0x80, 0x00, 0x00]   ; FIFO_COUNTH/L (0x2E/0x2F) read
    → on complete: count = records
    start DMA: TX = [0x30|0x80, 0x00 × N*16]   ; FIFO_DATA burst (0x30)
           RX → ping-pong buffer[active]
    → DMA TC ISR: CS high, flip buffer, post to processing task
```

**DM