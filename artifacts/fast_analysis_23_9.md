# Dual ICM-42688-P at 32 kHz: SPI Timing and DMA Configuration

## Caveats First

- **"Cycle 23"**: I don't recognize this as a TDK/InvenSense document revision, silicon cycle, or standard term. If it refers to an internal project milestone or a specific datasheet/app-note revision, share it and I'll align the details. Otherwise, treat everything below as based on the public ICM-42688-P datasheet (DS-000347).
- **APEX at 32 kHz conflicts**: APEX features (pedometer, tilt, tap, SMD, raise-to-wake, WoM) run on the internal DMP at a **DMP ODR of 25, 50, 100, or 500 Hz**. They are designed for low-power accel operation, not a 32 kHz high-rate pipeline. Running 32 kHz accel/gyro streaming and APEX simultaneously is not a documented, validated configuration. Typical solutions:
  1. Use 32 kHz streaming on both IMUs and implement motion detection on the host, or
  2. Dedicate one IMU to APEX (low ODR) and the other to 32 kHz, or
  3. Switch modes at runtime.
  
  Confirm with TDK before relying on concurrent operation.
- **Verify all values against your datasheet revision.** Register addresses and bitfields below are from Bank 0 as I recall them. Timing minimums in particular should be checked.

---

## 1. Bandwidth Budget

| Item | Value |
|---|---|
| Sample period @ 32 kHz | 31.25 µs |
| FIFO packet (Packet 3: header + accel + gyro + temp + timestamp) | 16 bytes |
| FIFO packet (Packet 4: 20-bit high-res) | 20 bytes |
| Data rate per IMU (16 B) | 512 kB/s ≈ 4.1 Mbit/s |
| Data rate per IMU (20 B) | 640 kB/s ≈ 5.1 Mbit/s |
| Both IMUs (20 B) | ≈ 10.2 Mbit/s + overhead |

**Recommendation:**
- Use **two separate SPI peripherals**, one per IMU, at **≥ 12 MHz** (max SCLK is 24 MHz).
- A shared bus at 24 MHz also works on paper, but it leaves little margin for CS gaps, FIFO count reads, and ISR latency.
- **Do not service per-sample DRDY interrupts** at a 31.25 µs period. Use FIFO watermark interrupts with burst DMA reads.

---

## 2. SPI Electrical/Timing Parameters (verify against datasheet)

| Parameter | Value |
|---|---|
| Mode | SPI Mode 0 or Mode 3 (CPOL=CPHA=0 or 1), MSB first |
| Max SCLK | 24 MHz |
| Read/write | Bit 7 of address byte: 1 = read, 0 = write |
| Burst read | Auto-increment. FIFO_DATA does not increment (repeated reads drain the FIFO). |
| CS setup / hold | Tens of ns (check the datasheet's SPI timing table for exact tCSS/tCSH) |
| CS high between transactions | Keep ≥ ~100 ns as a safe margin |
| Interface lock | Set `INTF_CONFIG0`/`DEVICE_CONFIG` so SPI is used and I²C is disabled (`INTF_CONFIG6`/bank-dependent; verify) |

**Layout:**
- Keep SCLK traces short and series-terminated (22–33 Ω) at 24 MHz.
- Use one CS per device.
- Pull-ups on CS lines prevent spurious selection during MCU reset.

---

## 3. Register Initialization (per IMU)

```c
// Bank 0
write(0x76, 0x00);            // REG_BANK_SEL = 0
write(0x11, 0x01);            // DEVICE_CONFIG: soft reset
delay_ms(2);
// verify WHO_AM_I (0x75) == 0x47

write(0x4C, 0x30);            // INTF_CONFIG0: FIFO count in records, big-endian (verify bits)
write(0x4F, (0<<5) | 0x01);   // GYRO_CONFIG0: ±2000 dps, ODR = 32 kHz
write(0x50, (0<<5) | 0x01);   // ACCEL_CONFIG0: ±16 g, ODR = 32 kHz
write(0x16, 0x40);            // FIFO_CONFIG: stream-to-FIFO
write(0x5F, 0x07 | (1<<3));   // FIFO_CONFIG1: accel+gyro+temp+tmst (add HIRES bit 4 for 20-byte)
write(0x60, WM & 0xFF);       // FIFO_CONFIG2: watermark low
write(0x61, (WM >> 8) & 0x0F);// FIFO_CONFIG3: watermark high
write(0x14, 0x03);            // INT_CONFIG: INT1 push-pull, active-high, pulsed (verify)
write(0x65, 0x04);            // INT_SOURCE0: FIFO threshold -> INT1
write(0x4E, 0x0F);            // PWR_MGMT0: gyro LN + accel LN  (do LAST)
delay_us(200);                // gyro start-up (≥45 ms before data is valid)
```

**Watermark choice:**
- `WM = 16` records gives an interrupt every 500 µs (2 kHz) and 256–320 bytes per DMA burst.
- Larger watermarks lower CPU load but add latency.
- The FIFO is 2 KB, so at 16 B/record you overflow after ~128 records (~4 ms). Keep the watermark well below that.

**Clock sync:** Feed a common 32.768 kHz clock to both IMUs' CLKIN pins (pin 9, enable RTC mode via `INTF_CONFIG1` and pin-function bits in Bank 1). This keeps both ODRs locked and makes timestamps comparable.

---

## 4. DMA Read Transaction

```
CS low
TX: [0x2E | 0x80] 0x00 0x00          -> FIFO_COUNTH/L (2 bytes)
CS high
CS low
TX: [0x30 | 0x80] + N dummy bytes    -> FIFO_DATA burst (N = count × packet size)
CS high
```

Optimization: read a fixed `WM × packet_size` bytes directly. This skips the count read and halves the transactions. Then validate each packet header byte, and discard packets with header `0x80` (empty/invalid).

---

## 5. DMA Configuration (STM32-style example, per SPI instance)

```c
// SPI: master, 8-bit, Mode 3, MSB first, prescaler -> 12–24 MHz, software NSS
// RX DMA: periph->mem, mem increment, byte, normal mode, priority VERY_HIGH
// TX DMA: mem->periph, mem increment OFF (send constant 0x00 dummy after addr), priority HIGH
// Start RX DMA before TX DMA to avoid overrun.

void EXTI_IMUx_IRQHandler(void) {          // INT1 watermark
    clear_exti();
    imu->tx[0] = 0x30 | 0x80;              // FIFO_DATA read
    CS_LOW(imu);
    HAL_SPI_TransmitReceive_DMA(imu->hspi, imu->tx, imu->rx[imu->buf], 1 + WM*PKT);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h) {
    imu_t *imu = imu_from_hspi(h);
    CS_HIGH(imu);
    imu->ready = imu->buf;  imu->buf ^= 1; // ping-pong buffers
    signal_processing_task(imu);
}
```

**DMA notes:**
- **Use ping-pong (double) buffers** so parsing never races the next DMA transfer.
- **Cache coherency (Cortex-M7/H7):** place buffers in non-cacheable RAM (MPU region) or invalidate D-cache after RX. Align buffers to 32 bytes.
- **Use separate DMA streams per SPI** so both IMUs can transfer concurrently.
- **On a shared bus**, arbitrate in software and queue the second IMU's transfer in the completion callback.
- **Set