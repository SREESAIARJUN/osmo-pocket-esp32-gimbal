/**
 * tca9548a.h — TI TCA9548A 1-to-8 I2C switch driver (Revision 7.0)
 *
 * Resolves the AS5600 fixed-address (0x36) conflict: each encoder sits on its
 * own downstream channel and only one channel is enabled at a time, so exactly
 * one 0x36 device is ever visible on the upstream bus.
 *
 * Device facts (TI SCPS207):
 *   - 7-bit address 0b1110<A2><A1><A0> = 0x70..0x77 (A2..A0 strapped low -> 0x70)
 *   - Single 8-bit control register, bit n = channel n enable; no sub-address
 *   - Fast-mode max 400 kHz (the switch, not the AS5600, limits bus speed)
 *   - RESET is active-low; low pulse >= 6 ns, clears the control register to 0x00
 *     (all channels off) and releases a slave-side hung bus.
 */
#ifndef TCA9548A_H
#define TCA9548A_H

#include <stdint.h>
#include <Wire.h>

class TCA9548A {
public:
    static constexpr uint8_t DEFAULT_ADDR = 0x70;
    static constexpr uint8_t NUM_CHANNELS = 8;
    static constexpr uint8_t NO_CHANNEL   = 0xFF;

    /**
     * @param wire     upstream bus (already begun by the caller)
     * @param addr     7-bit address 0x70..0x77
     * @param rst_pin  GPIO driving RESET (active low), or -1 if not wired
     */
    TCA9548A(TwoWire& wire, uint8_t addr = DEFAULT_ADDR, int8_t rst_pin = -1);

    /** Hardware-reset (if wired), confirm presence, leave all channels off. */
    bool begin();

    /**
     * Enable exactly one channel. Skips the bus transaction when the channel is
     * already selected (saves ~50 us per call at 400 kHz in the encoder loop).
     */
    bool select(uint8_t channel);

    /** Disable all channels (control register = 0x00). */
    bool disableAll();

    /** Read back the control register; returns false on NACK. */
    bool readControl(uint8_t& mask);

    /** Pulse RESET low; clears cached state. No-op returning false if not wired. */
    bool hardReset();

    /** Forget the cached channel so the next select() always hits the bus. */
    void invalidate() { current_ = NO_CHANNEL; }

    uint8_t  current() const { return current_; }
    uint8_t  address() const { return addr_; }
    uint32_t errorCount() const { return errors_; }

private:
    bool writeMask(uint8_t mask);

    TwoWire& wire_;
    uint8_t  addr_;
    int8_t   rst_pin_;
    uint8_t  current_ = NO_CHANNEL;
    uint32_t errors_  = 0;
};

/**
 * Standard I2C bus-clear (NXP UM10204 §3.1.16): if a slave holds SDA low after a
 * brown-out or interrupted transfer, clock SCL up to 9 times until SDA releases,
 * then generate a STOP. Must be called with the Wire peripheral detached
 * (Wire.end()); the caller re-begins the bus afterwards.
 * @return true if SDA is high (bus free) on exit.
 */
bool i2c_bus_clear(int8_t sda_pin, int8_t scl_pin);

#endif  // TCA9548A_H
