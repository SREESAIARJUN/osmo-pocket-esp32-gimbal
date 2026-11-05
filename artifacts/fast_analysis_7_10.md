# Dual ICM-42688-P at 32 kHz: SPI and DMA Reference Design

## Notes before you implement

1. **"Cycle 7" is not a term I can find** in TDK InvenSense documentation for the ICM-42688-P or its APEX engine. If it comes from an internal spec, an app note, or a different part (e.g., ICM-45686 / BalancedGyro), check that source. I won't invent parameters for it.
2. **APEX does not run at 32 kHz.** The DMP that executes APEX functions (pedometer, tilt, tap, WoM, SMD, raise-to-wake) runs at a separate DMP ODR of 25, 50, 100, or 500 Hz, set in `APEX_CONFIG0`. You can stream 32 kHz sensor data and run APEX at the same time, but they are independent rates. Verify on hardware that your chosen combination of accel mode, ODR, and APEX features is supported. TDK's guidance ties APEX to specific accel ODR and power-mode settings.
3. **32 kHz ODR requires Low-Noise (LN) mode** for both gyro and accel.
4. **Timing values below are from memory of datasheet DS-000347.** Confirm every number against the revision you're designing to.

---

## 1. Bus Architecture

**Use two separate SPI peripherals, one per IMU.** You could share one bus, but at 32 kHz × 2 sensors the CS arbitration, DMA chaining, and jitter make it fragile. Separate buses give:
- independent, simultaneous DMA transfers
- no CS contention
- deterministic sample timestamps

```
MCU SPI1 ──SCLK/MOSI/MISO/CS1── IMU_A   INT1_A → EXTI (trigger)
MCU SPI2 ──SCLK/MOSI/MISO/CS2── IMU_B   INT1_B → EXTI (trigger)
```

For tight inter-IMU sync, drive both IMUs' `CLKIN` (pin 9, configured via `INTF_CONFIG1`/bank 1 `INTF_CONFIG5`) from a shared 32.768 kHz reference.

---

## 2. SPI Electrical / Timing Parameters

| Parameter | Value (verify) | Notes |
|---|---|---|
| Max SCLK | 24 MHz | Use 20–24 MHz with good layout; 12 MHz if traces are long |
| SPI mode | Mode 0 or Mode 3 | Mode 3 (CPOL=1, CPHA=1) is common |
| Bit order | MSB first | |
| Read flag | Address bit 7 = 1 (`addr \| 0x80`) | |
| Address auto-increment | Yes, during burst | |
| CS setup (tCSS) | ~39 ns min | |
| CS hold (tCSH) | ~18 ns min | |
| SCLK high/low | ~17 ns min each | at 24 MHz period ≈ 41.7 ns |
| Data setup/hold to SCLK | ~13 ns / ~8 ns | |
| CS high between transactions | ≥ ~0.5 µs recommended | conservative |
| I/O voltage | VDDIO 1.71–3.6 V | |

**Disable I²C/I3C** so the interface stays locked to SPI. Set `INTF_CONFIG0` and the bank-1 interface bits early in init, per the datasheet.

---

## 3. Bandwidth Budget (per IMU)

Sample period at 32 kHz = **31.25 µs**.

### Option A: Direct register burst on DRDY

- Burst from `TEMP_DATA1` (0x1D) through `GYRO_DATA_Z0` (0x2A) = 14 data bytes + 1 address byte = **15 bytes = 120 bits**
- At 24 MHz this takes ≈ **5.0 µs**, plus CS overhead, so about 6 µs
- Bus utilization ≈ **19%**

### Option B: FIFO batching (recommended)

| FIFO packet | Size | Data rate at 32 kHz |
|---|---|---|
| Packet 3 (accel+gyro+temp+timestamp) | 16 B | 512 kB/s |
| Packet 4 (20-bit hi-res) | 20 B | 640 kB/s |

With watermark = 32 packets (16 B each):
- Interrupt rate is 1 kHz
- Transfer is 2 (FIFO_COUNT) + 512 B per read, about 175 µs at 24 MHz
- Bus utilization ≈ 17.5%, with **32× fewer interrupts**

The FIFO is 2 KB, so keep the watermark ≤ ~50% to tolerate ISR latency.

---

## 4. Register Configuration (Bank 0 unless noted)

Select banks via `REG_BANK_SEL` (0x76).

```c
// Reset
write(0x76, 0x00);          // bank 0
write(0x11, 0x01);          // DEVICE_CONFIG: soft reset
delay_ms(2);
// WHO_AM_I (0x75) should read 0x47

// Gyro: ±2000 dps, 32 kHz
write(0x4F, (0b000 << 5) | 0b0001);   // GYRO_CONFIG0
// Accel: ±16 g, 32 kHz
write(0x50, (0b000 << 5) | 0b0001);   // ACCEL_CONFIG0

// Interrupts: INT1 push-pull, active high, pulsed
write(0x14, 0x03);                    // INT_CONFIG
write(0x63, 0x00);                    // INT_CONFIG1: clear INT_ASYNC_RESET etc. (check datasheet note)

// --- Option A: DRDY on INT1 ---
write(0x65, 0x08);                    // INT_SOURCE0: UI_DRDY_INT1_EN

// --- Option B: FIFO ---
write(0x16, 0x40);                    // FIFO_CONFIG: stream-to-FIFO
write(0x5F, 0x07);                    // FIFO_CONFIG1: accel+gyro+temp en (packet 3)
write(0x60, WM & 0xFF);               // FIFO_CONFIG2 (watermark low)
write(0x61, (WM >> 8) & 0x0F);        // FIFO_CONFIG3 (watermark high)
write(0x65, 0x04);                    // INT_SOURCE0: FIFO_THS_INT1_EN

// Power on: gyro LN + accel LN
write(0x4E, 0x0F);                    // PWR_MGMT0
delay_us(200);                        // no register writes for >=200 µs after this
// Gyro needs ~30 ms to settle; discard early samples
```

### APEX (independent, low rate)

```c
write(0x56, /* APEX_CONFIG0 */ (dmp_power_save) | DMP_ODR_50HZ);  // 25/50/100/500 Hz only
write(0x4B, 0x20);   // SIGNAL_PATH_RESET: DMP_INIT_EN — then wait per datasheet
// Bank 4: APEX_CONFIG1..9 for pedometer/tilt/tap thresholds
write(0x76, 0x04);
// ... feature thresholds ...
write(0x76, 0x00);
// Enable features via APEX_CONFIG0 bits; route APEX interrupts to INT2 (INT_SOURCE6/7)
```

Routing APEX events to **INT2** keeps them off your high-rate INT1 DMA trigger path.

---

## 5. DMA Configuration (STM32H7 example; adapt to your MCU)

### Per IMU
- **SPI**: master, 8-bit, Mode 3, prescaler for ≤24 MHz, hardware NSS off (manual GPIO CS for precise control) **or** hardware NSS with `MSSI`/`MIDI` delays set to meet tCSS/tCSH.
- **DMA RX stream**: peripheral→memory, byte, memory increment, **normal mode** (not circular; each transaction is discrete), priority *Very High*.
- **DMA TX stream**: memory→peripheral, byte, sends `{addr|0x80, 0x00...}`, priority *High*.
- **Buffers**: place in DMA-accessible RAM (D2 SRAM on H7, not DTCM). Either 32-byte align and invalidate D-cache after RX, or use an MPU non-cacheable region.

### Assign