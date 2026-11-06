// ICM-42688-P SPI FIFO driver — Revision 57.0. See icm42688.h.
#include "icm42688.h"
#include <Arduino.h>

using namespace icm42688;

const char* imu_status_str(ImuStatus s) {
    switch (s) {
        case ImuStatus::OK:           return "OK";
        case ImuStatus::NOT_PRESENT:  return "NOT_PRESENT";
        case ImuStatus::CONFIG_FAULT: return "CONFIG_FAULT";
        case ImuStatus::BUS_ERROR:    return "BUS_ERROR";
    }
    return "?";
}

bool ICM42688::writeVerify(uint8_t reg, uint8_t value) {
    uint8_t rb = 0;
    if (!dev_.write(reg, value) || !dev_.read(reg, &rb, 1)) {
        ++bus_errors_;
        return false;
    }
    return rb == value;
}

void ICM42688::flush() {
    if (!dev_.write(REG_SIGNAL_PATH_RESET, SIGNAL_PATH_FIFO_FLUSH)) ++bus_errors_;
    delayMicroseconds(2);  // flush completes within 1.5 us
    resync_ = true;
}

ImuStatus ICM42688::begin() {
    resync_ = true;
    // Soft reset returns every register to its default (including the bank
    // select), so a half-configured device left by a watchdog reset is cleaned up.
    if (!dev_.write(REG_BANK_SEL, 0) || !dev_.write(REG_DEVICE_CONFIG, DEVICE_CONFIG_SOFT_RESET)) {
        ++bus_errors_;
        return status_ = ImuStatus::BUS_ERROR;
    }
    delay(2);  // datasheet: wait >= 1 ms after soft reset before any access

    uint8_t who = 0;
    if (!dev_.read(REG_WHO_AM_I, &who, 1)) {
        ++bus_errors_;
        return status_ = ImuStatus::BUS_ERROR;
    }
    if (who != WHO_AM_I_VALUE) return status_ = ImuStatus::NOT_PRESENT;

    // Sensors are off after reset, so all of these are legal to write now.
    static const uint8_t kCfg[][2] = {
        {REG_INTF_CONFIG0,       INTF_CONFIG0_VALUE},
        {REG_GYRO_CONFIG0,       GYRO_CONFIG0_VALUE},
        {REG_ACCEL_CONFIG0,      ACCEL_CONFIG0_VALUE},
        {REG_GYRO_ACCEL_CONFIG0, GYRO_ACCEL_CONFIG0_VALUE},
        {REG_TMST_CONFIG,        TMST_CONFIG_VALUE},
        {REG_FIFO_CONFIG1,       FIFO_CONFIG1_VALUE},
        {REG_FIFO_CONFIG,        FIFO_CONFIG_STREAM},
    };
    for (const auto& c : kCfg) {
        if (!writeVerify(c[0], c[1])) return status_ = ImuStatus::CONFIG_FAULT;
    }

    // Turn on gyro + accel (LN). No register writes for 200 us afterwards.
    if (!writeVerify(REG_PWR_MGMT0, PWR_MGMT0_LN)) return status_ = ImuStatus::CONFIG_FAULT;
    delay(GYRO_STARTUP_MS + 5);  // discard the gyro start-up transient...
    flush();                     // ...by flushing everything queued so far
    uint8_t st = 0;
    dev_.read(REG_INT_STATUS, &st, 1);  // clear latched status (FIFO_FULL etc.)
    return status_ = ImuStatus::OK;
}

bool ICM42688::decode(const uint8_t* p, ImuRecord& r) {
    // Records are big-endian (SENSOR_DATA_ENDIAN = 1). Axis i: bytes [1+2i, 2+2i]
    // hold accel[19:4], bytes [7+2i, 8+2i] gyro[19:4], byte 17+i holds
    // accel[3:0] in the high nibble and gyro[3:0] in the low nibble.
    // value = (int16)[19:4] * 16 + [3:0] is the exact 20-bit two's-complement value.
    bool valid = true;
    for (int i = 0; i < 3; ++i) {
        const int32_t a_hi = static_cast<int16_t>(static_cast<uint16_t>((p[1 + 2 * i] << 8) | p[2 + 2 * i]));
        const int32_t g_hi = static_cast<int16_t>(static_cast<uint16_t>((p[7 + 2 * i] << 8) | p[8 + 2 * i]));
        if (a_hi == INVALID_HI16 || g_hi == INVALID_HI16) valid = false;
        const int32_t a = a_hi * 16 + static_cast<int32_t>(p[17 + i] >> 4);
        const int32_t g = g_hi * 16 + static_cast<int32_t>(p[17 + i] & 0x0F);
        r.accel[i] = static_cast<float>(a) * ACCEL_G_PER_LSB;
        r.gyro[i]  = static_cast<float>(g) * GYRO_RAD_PER_LSB;
    }
    const int16_t t = static_cast<int16_t>(static_cast<uint16_t>((p[13] << 8) | p[14]));
    r.temp_c     = static_cast<float>(t) / TEMP_LSB_PER_C + TEMP_OFFSET_C;
    r.tmst       = static_cast<uint16_t>((p[15] << 8) | p[16]);
    r.odr_change = (p[0] & 0x03) != 0;
    return valid;
}

size_t ICM42688::readFifo(ImuRecord* out, size_t max, uint16_t* remaining) {
    if (remaining) *remaining = 0;
    if (status_ != ImuStatus::OK) return 0;
    if (max > MAX_BATCH) max = MAX_BATCH;

    // One burst: INT_STATUS (clear-on-read), FIFO_COUNTH (latches L), FIFO_COUNTL.
    uint8_t hdr[3];
    if (!dev_.read(REG_INT_STATUS, hdr, sizeof hdr)) {
        ++bus_errors_;
        return 0;
    }
    if (hdr[0] & INT_STATUS_FIFO_FULL) {
        // Stream mode overwrote the oldest records: there is a gap in time.
        ++overflows_;
        resync_ = true;
    }
    const uint16_t count = static_cast<uint16_t>((hdr[1] << 8) | hdr[2]);
    if (count == 0) return 0;
    if (count > FIFO_MAX_RECORDS) {  // impossible count: SPI corruption
        ++header_errors_;
        flush();
        return 0;
    }
    const size_t n = count < max ? count : max;
    if (!dev_.read(REG_FIFO_DATA, buf_, n * RECORD_BYTES)) {
        ++bus_errors_;
        flush();  // a partial burst leaves the FIFO read pointer mid-record
        return 0;
    }

    size_t got = 0;
    for (size_t k = 0; k < n; ++k) {
        const uint8_t* p = buf_ + k * RECORD_BYTES;
        if (p[0] & HDR_MSG_EMPTY) break;           // drained faster than the count said
        if ((p[0] & HDR_MASK) != HDR_PACKET4) {    // alignment lost: everything after is garbage
            ++header_errors_;
            flush();
            return got;
        }
        if (!decode(p, out[got])) {
            ++invalid_;
            continue;
        }
        ++got;
        ++records_;
    }
    if (remaining) *remaining = static_cast<uint16_t>(count - n);
    return got;
}

#if defined(ARDUINO_ARCH_ESP32)
void ArduinoSpiDevice::begin() {
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
}

bool ArduinoSpiDevice::read(uint8_t reg, uint8_t* buf, size_t n) {
    spi_.beginTransaction(SPISettings(hz_, MSBFIRST, SPI_MODE3));
    digitalWrite(cs_, LOW);
    spi_.transfer(static_cast<uint8_t>(reg | SPI_READ));
    // spiTransferBytesNL() stores through uint32_t*: only use it on 4-byte
    // aligned buffers (the FIFO burst buffer is alignas(4)); short register
    // reads into arbitrary stack bytes go byte-by-byte (unaligned stores trap on LX7).
    if (n >= 8 && (reinterpret_cast<uintptr_t>(buf) & 3u) == 0) {
        spi_.transferBytes(nullptr, buf, static_cast<uint32_t>(n));  // clocks 0xFF
    } else {
        for (size_t i = 0; i < n; ++i) buf[i] = spi_.transfer(0xFF);
    }
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
    return true;
}

bool ArduinoSpiDevice::write(uint8_t reg, uint8_t value) {
    spi_.beginTransaction(SPISettings(hz_, MSBFIRST, SPI_MODE3));
    digitalWrite(cs_, LOW);
    spi_.transfer(static_cast<uint8_t>(reg & 0x7F));
    spi_.transfer(value);
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
    return true;
}
#endif
