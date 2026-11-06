#include "tca9548a.h"
#include <Arduino.h>

TCA9548A::TCA9548A(TwoWire& wire, uint8_t addr, int8_t rst_pin)
    : wire_(wire), addr_(addr), rst_pin_(rst_pin) {}

bool TCA9548A::begin() {
    if (rst_pin_ >= 0) {
        pinMode(rst_pin_, OUTPUT);
        digitalWrite(rst_pin_, HIGH);
        hardReset();
    }
    current_ = NO_CHANNEL;
    if (!disableAll()) return false;
    uint8_t mask = 0xFF;
    return readControl(mask) && mask == 0x00;
}

bool TCA9548A::writeMask(uint8_t mask) {
    wire_.beginTransmission(addr_);
    wire_.write(mask);
    if (wire_.endTransmission(true) != 0) {
        ++errors_;
        current_ = NO_CHANNEL;  // state unknown after a failed write
        return false;
    }
    return true;
}

bool TCA9548A::select(uint8_t channel) {
    if (channel >= NUM_CHANNELS) return false;
    if (channel == current_) return true;
    if (!writeMask(static_cast<uint8_t>(1u << channel))) return false;
    current_ = channel;
    return true;
}

bool TCA9548A::disableAll() {
    if (!writeMask(0x00)) return false;
    current_ = NO_CHANNEL;
    return true;
}

bool TCA9548A::readControl(uint8_t& mask) {
    if (wire_.requestFrom(addr_, static_cast<uint8_t>(1)) != 1) {
        ++errors_;
        return false;
    }
    mask = static_cast<uint8_t>(wire_.read());
    return true;
}

bool TCA9548A::hardReset() {
    current_ = NO_CHANNEL;
    if (rst_pin_ < 0) return false;
    digitalWrite(rst_pin_, LOW);
    delayMicroseconds(2);   // datasheet t_W(L) >= 6 ns; 2 us gives margin for RC on the line
    digitalWrite(rst_pin_, HIGH);
    delayMicroseconds(2);   // t_REC(STA) recovery before first START
    return true;
}

bool i2c_bus_clear(int8_t sda_pin, int8_t scl_pin) {
    pinMode(sda_pin, INPUT_PULLUP);
    pinMode(scl_pin, OUTPUT_OPEN_DRAIN);
    digitalWrite(scl_pin, HIGH);
    delayMicroseconds(5);

    for (int i = 0; i < 9 && digitalRead(sda_pin) == LOW; ++i) {
        digitalWrite(scl_pin, LOW);
        delayMicroseconds(5);       // 100 kHz bit-bang, safe for every slave
        digitalWrite(scl_pin, HIGH);
        delayMicroseconds(5);
    }

    // STOP: SDA low -> high while SCL high
    pinMode(sda_pin, OUTPUT_OPEN_DRAIN);
    digitalWrite(sda_pin, LOW);
    delayMicroseconds(5);
    digitalWrite(scl_pin, HIGH);
    delayMicroseconds(5);
    digitalWrite(sda_pin, HIGH);
    delayMicroseconds(5);

    pinMode(sda_pin, INPUT_PULLUP);
    return digitalRead(sda_pin) == HIGH;
}
