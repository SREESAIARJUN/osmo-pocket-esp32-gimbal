/**
 * icm42688.h — TDK InvenSense ICM-42688-P SPI FIFO driver (Revision 57.0)
 *
 * Configuration (all registers in user bank 0, DS-000347 rev 1.x):
 *   - Gyro + accel in Low-Noise mode, ODR = 1 kHz, UI filter BW = ODR/4.
 *   - FIFO in stream mode with 20-bit high-resolution records ("packet 4",
 *     20 bytes): header, accel/gyro [19:4], 16-bit temperature, 16-bit
 *     timestamp, and three extension bytes holding accel/gyro [3:0].
 *     In hi-res mode the full scale is fixed at +/-2000 dps / +/-16 g and the
 *     20-bit words are scaled so that 2^19 LSB = full scale:
 *         gyro  : 262.144 LSB/dps (19 significant bits, 131 LSB/dps after >>1)
 *         accel : 32768   LSB/g   (18 significant bits, 8192 LSB/g after >>2)
 *         temp  : T[degC] = raw / 132.48 + 25   (16-bit, 7.5 mK/LSB)
 *     Hi-res gives 0.0038 dps resolution at 2000 dps range, so a fast handle
 *     flick cannot clip the gyro and the 16-bit FIFO temperature (instead of
 *     the 8-bit 0.48 degC one in packet 3) is fine enough for the
 *     temperature bias model in imu_bias.h.
 *   - FIFO_COUNT reports RECORDS (FIFO_COUNT_REC = 1), big-endian.
 *   - Device timestamp in every record (TMST_EN, 1 us resolution, absolute
 *     16-bit counter), used by the pipeline to derive per-sample dt.
 *   - I2C/I3C slave interface disabled (UI_SIFS_CFG = 11b): SPI only.
 *
 * Each FIFO service is exactly two SPI transactions: a 3-byte burst at
 * INT_STATUS..FIFO_COUNTL (clears FIFO_FULL and latches the count), then one
 * burst of N x 20 bytes at FIFO_DATA. At 8 MHz one record costs ~22 us of
 * bus time; 1 kHz x 2 IMUs < 5 % of the SPI bus.
 *
 * The register interface is abstracted behind SpiDevice so the driver is
 * host-tested against a register-level device model
 * (firmware/test/host/test_imu.cpp).
 */
#ifndef ICM42688_H
#define ICM42688_H

#include <stdint.h>
#include <stddef.h>

namespace icm42688 {
// ---- User bank 0 registers ----
constexpr uint8_t REG_DEVICE_CONFIG       = 0x11;  // bit0 SOFT_RESET_CONFIG
constexpr uint8_t REG_FIFO_CONFIG         = 0x16;  // [7:6] FIFO_MODE
constexpr uint8_t REG_TEMP_DATA1          = 0x1D;
constexpr uint8_t REG_INT_STATUS          = 0x2D;  // clear-on-read; bit1 FIFO_FULL_INT
constexpr uint8_t REG_FIFO_COUNTH         = 0x2E;
constexpr uint8_t REG_FIFO_COUNTL         = 0x2F;
constexpr uint8_t REG_FIFO_DATA           = 0x30;
constexpr uint8_t REG_SIGNAL_PATH_RESET   = 0x4B;  // bit1 FIFO_FLUSH
constexpr uint8_t REG_INTF_CONFIG0        = 0x4C;
constexpr uint8_t REG_PWR_MGMT0           = 0x4E;
constexpr uint8_t REG_GYRO_CONFIG0        = 0x4F;
constexpr uint8_t REG_ACCEL_CONFIG0       = 0x50;
constexpr uint8_t REG_GYRO_ACCEL_CONFIG0  = 0x52;
constexpr uint8_t REG_TMST_CONFIG         = 0x54;
constexpr uint8_t REG_FIFO_CONFIG1        = 0x5F;
constexpr uint8_t REG_WHO_AM_I            = 0x75;
constexpr uint8_t REG_BANK_SEL            = 0x76;

constexpr uint8_t SPI_READ                = 0x80;
constexpr uint8_t WHO_AM_I_VALUE          = 0x47;

// ---- Field values written by begin() ----
constexpr uint8_t DEVICE_CONFIG_SOFT_RESET = 0x01;
constexpr uint8_t INT_STATUS_FIFO_FULL     = 0x02;
constexpr uint8_t SIGNAL_PATH_FIFO_FLUSH   = 0x02;
// FIFO_COUNT_REC | FIFO_COUNT_ENDIAN(big) | SENSOR_DATA_ENDIAN(big) | UI_SIFS_CFG=11 (disable I2C)
constexpr uint8_t INTF_CONFIG0_VALUE       = 0x73;
// GYRO_MODE = ACCEL_MODE = 11b (Low-Noise), temperature sensor enabled
constexpr uint8_t PWR_MGMT0_LN             = 0x0F;
// FS_SEL = 000 (2000 dps / 16 g; forced anyway in hi-res FIFO), ODR = 0110 (1 kHz)
constexpr uint8_t GYRO_CONFIG0_VALUE       = 0x06;
constexpr uint8_t ACCEL_CONFIG0_VALUE      = 0x06;
// ACCEL_UI_FILT_BW = GYRO_UI_FILT_BW = 1 -> max(400 Hz, ODR)/4 = 250 Hz (1st-order LN filter)
constexpr uint8_t GYRO_ACCEL_CONFIG0_VALUE = 0x11;
// TMST_EN, 1 us resolution, absolute (not delta) counter
constexpr uint8_t TMST_CONFIG_VALUE        = 0x01;
// FIFO_HIRES_EN | FIFO_TMST_FSYNC_EN | FIFO_TEMP_EN | FIFO_GYRO_EN | FIFO_ACCEL_EN
constexpr uint8_t FIFO_CONFIG1_VALUE       = 0x1F;
// FIFO_MODE = 01b: stream-to-FIFO
constexpr uint8_t FIFO_CONFIG_STREAM       = 0x40;

// ---- FIFO packet 4 (20-bit hi-res) ----
constexpr size_t   RECORD_BYTES        = 20;
constexpr size_t   FIFO_BYTES          = 2048;
constexpr uint16_t FIFO_MAX_RECORDS    = FIFO_BYTES / RECORD_BYTES;   // 102
constexpr uint8_t  HDR_MSG_EMPTY       = 0x80;   // FIFO empty marker
constexpr uint8_t  HDR_MASK            = 0xFC;   // ignore ODR-change flags [1:0]
constexpr uint8_t  HDR_PACKET4         = 0x78;   // ACCEL | GYRO | 20BIT | TMST(10b)
constexpr int32_t  INVALID_HI16        = -32768; // 0x8000 in [19:4] = invalid sample

constexpr float GYRO_LSB_PER_DPS = 262.144f;   // 2^19 / 2000
constexpr float ACCEL_LSB_PER_G  = 32768.0f;   // 2^19 / 16
constexpr float TEMP_LSB_PER_C   = 132.48f;
constexpr float TEMP_OFFSET_C    = 25.0f;
constexpr float DEG2RAD          = 0.0174532925199432958f;
constexpr float GYRO_RAD_PER_LSB = DEG2RAD / GYRO_LSB_PER_DPS;
constexpr float ACCEL_G_PER_LSB  = 1.0f / ACCEL_LSB_PER_G;

constexpr uint32_t ODR_HZ           = 1000;
constexpr uint32_t ODR_PERIOD_US    = 1000000u / ODR_HZ;
constexpr uint32_t GYRO_STARTUP_MS  = 45;   // datasheet gyro start-up time from sleep
}  // namespace icm42688

/** Register-level access to one SPI device (one chip select). */
class SpiDevice {
public:
    virtual ~SpiDevice() = default;
    /** Burst-read @p n bytes starting at @p reg (driver sets the R/W bit). */
    virtual bool read(uint8_t reg, uint8_t* buf, size_t n) = 0;
    virtual bool write(uint8_t reg, uint8_t value) = 0;
};

/** One decoded FIFO record in the SENSOR frame. */
struct ImuRecord {
    float    gyro[3];     // rad/s
    float    accel[3];    // g
    float    temp_c;      // die temperature
    uint16_t tmst;        // device timestamp, 1 us/LSB, wraps every 65.536 ms
    bool     odr_change;  // header ODR flag (first record after a config change)
};

enum class ImuStatus : uint8_t {
    OK = 0,
    NOT_PRESENT,     // WHO_AM_I mismatch (no device, MISO stuck, wrong CS)
    CONFIG_FAULT,    // a configuration register did not read back
    BUS_ERROR,       // SpiDevice::read/write returned false
};

const char* imu_status_str(ImuStatus s);

class ICM42688 {
public:
    static constexpr size_t MAX_BATCH = 16;   // records per FIFO burst (320 bytes)

    explicit ICM42688(SpiDevice& dev) : dev_(dev) {}

    /**
     * Soft-reset, verify WHO_AM_I, write and read back the configuration,
     * start the sensors and flush the FIFO after the gyro start-up time.
     */
    ImuStatus begin();

    /**
     * Drain up to @p max (<= MAX_BATCH) records into @p out. Returns the
     * number decoded. Invalid samples are dropped; on a header error (FIFO
     * alignment lost) the FIFO is flushed and resyncPending() is set, as it is
     * after an overflow — the caller must not trust the dt across that gap.
     * @p remaining (optional) receives FIFO records still pending.
     */
    size_t readFifo(ImuRecord* out, size_t max, uint16_t* remaining = nullptr);

    /** Decode one 20-byte packet-4 record. Returns false for invalid data. */
    static bool decode(const uint8_t* p, ImuRecord& r);

    /** Read and clear the resync flag (set by overflow / flush / begin). */
    bool takeResync() { const bool r = resync_; resync_ = false; return r; }

    ImuStatus status() const { return status_; }
    uint32_t overflows() const { return overflows_; }
    uint32_t headerErrors() const { return header_errors_; }
    uint32_t invalidSamples() const { return invalid_; }
    uint32_t records() const { return records_; }
    uint32_t busErrors() const { return bus_errors_; }

private:
    bool writeVerify(uint8_t reg, uint8_t value);
    void flush();

    SpiDevice& dev_;
    ImuStatus  status_ = ImuStatus::NOT_PRESENT;
    bool       resync_ = true;
    uint32_t   overflows_ = 0, header_errors_ = 0, invalid_ = 0, records_ = 0, bus_errors_ = 0;
    alignas(4) uint8_t buf_[MAX_BATCH * icm42688::RECORD_BYTES];
};

#if defined(ARDUINO_ARCH_ESP32)
#include <SPI.h>
/** SpiDevice on an Arduino SPIClass with a GPIO chip select (SPI mode 3). */
class ArduinoSpiDevice : public SpiDevice {
public:
    ArduinoSpiDevice(SPIClass& spi, int8_t cs, uint32_t hz) : spi_(spi), cs_(cs), hz_(hz) {}
    void begin();
    bool read(uint8_t reg, uint8_t* buf, size_t n) override;
    bool write(uint8_t reg, uint8_t value) override;

private:
    SPIClass& spi_;
    int8_t    cs_;
    uint32_t  hz_;
};
#endif

#endif  // ICM42688_H
