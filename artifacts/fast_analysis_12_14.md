# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Clarifications before you build this

**1. "Cycle 12"**
I don't recognize this as a TDK/InvenSense term, datasheet revision, or APEX mode. If it refers to an internal spec, a project milestone, or a specific document, please share it. The design below follows the public ICM-42688-P datasheet.

**2. APEX and 32 kHz ODR are very likely incompatible on the same chip.**
- APEX runs on the on-chip DMP at low rates. `APEX_CONFIG0.DMP_ODR` offers only 25, 50, 100 or 500 Hz.
- The APEX setup flows in the datasheet and in TDK's reference driver configure the accelerometer ODR to match these low rates.
- I'm not aware of any supported configuration that runs APEX while the accelerometer is at 32 kHz. Verify with TDK if this is a hard requirement.

**Practical options:**
- **A (recommended):** Run both IMUs at 32 kHz with the FIFO. Do motion detection (tap, tilt, wake, SMD) in MCU firmware on the high-rate data.
- **B:** IMU-1 at 32 kHz for data. IMU-2 at a low ODR with APEX enabled for event and wake detection. This loses redundancy at 32 kHz.
- **C:** Switch modes at runtime: APEX at low ODR while idle, then reconfigure to 32 kHz when active. Each switch needs about 50 ms of settling.

---

## 1. Bandwidth budget

| Item | Value |
|---|---|
| Sample period at 32 kHz | 31.25 µs |
| FIFO packet 3 (accel + gyro + temp + timestamp) | 16 bytes |
| FIFO packet 4 (20-bit high-res) | 20 bytes |
| Data rate per IMU, packet 3 | 512 kB/s ≈ 4.1 Mbit/s |
| Data rate per IMU, packet 4 | 640 kB/s ≈ 5.1 Mbit/s |
| Dual IMU, packet 4 | ≈ 10.2 Mbit/s payload |
| SPI maximum (datasheet) | 24 MHz |

**Recommendation: use one SPI peripheral per IMU.** Benefits:
- Each bus runs at about 21% utilization at 24 MHz.
- Reads are parallel and the timing is deterministic.
- A fault on one IMU's bus cannot corrupt the other's data.

A shared bus is workable at 24 MHz with FIFO burst reads (about 45–55% utilization), but you lose that isolation.

---

## 2. SPI electrical timing

- **Mode:** SPI mode 0 or 3 (CPOL = CPHA, both work). 4-wire, MSB first.
- **Byte 0:** bit 7 = R/W (1 = read), bits 6:0 = register address. Burst reads auto-increment, except FIFO_DATA, which streams.

Key parameters (approximate; confirm against the "SPI Timing Characterization" table in your datasheet revision):

| Parameter | Typical spec | Design target at 24 MHz |
|---|---|---|
| f_SCLK | ≤ 24 MHz | 24 MHz (or 20 MHz for margin) |
| SCLK high / low | ≥ ~17 ns | 20.8 ns |
| CS setup to first SCLK edge | ≥ ~39 ns | ≥ 50 ns |
| CS hold after last SCLK edge | ≥ ~18 ns | ≥ 50 ns |
| SDI setup / hold | ~13 / ~8 ns | met by MCU |
| SDO output valid after SCLK falling edge | ~20 ns | **critical, see below** |
| CS high between transactions | ≥ ~0.5 µs recommended | 1 µs |

**SDO timing margin.** At 24 MHz the half-period is 20.8 ns. That is about equal to SDO valid delay plus PCB and MCU input delay, so the margin is near zero.

Fixes:
- Run at 16–20 MHz if traces are longer than about 5 cm.
- Or enable late or delayed sampling on the MCU, if it supports this (e.g., STM32H7 `MasterRxAutoSuspend`/sample delay, NXP `SAMPLE_POINT`).
- Keep traces short and matched. Use 22–33 Ω series termination on SCLK at the source.

**Disable I2C/I3C** so stray SDA-like activity can't corrupt the SPI interface. Set `INTF_CONFIG0` (0x4C) bits [1:0] `UI_SIFS_CFG` = `11`.

### Transaction timing per watermark read

At 24 MHz with packet 4 and a 16-packet watermark:
- Transfer size: 1 address byte + 2 count bytes + 320 data bytes = 323 bytes ≈ **108 µs**.
- One read every 500 µs gives about 22% bus utilization.

---

## 3. Register initialization (per IMU, Bank 0 unless noted)

```
0x76 REG_BANK_SE