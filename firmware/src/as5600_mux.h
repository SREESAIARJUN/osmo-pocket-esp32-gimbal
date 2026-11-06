/**
 * as5600_mux.h — Three AS5600 encoders behind a TCA9548A (Revision 7.0)
 *
 * Problem (Rev <= 6.0): the AS5600 has a single, non-programmable I2C address
 * (0x36; only the AS5600L is re-addressable). Three of them on one bus all ACK
 * the same frames and drive SDA simultaneously, so every axis read a wired-AND
 * of three encoders — garbage commutation angles.
 *
 * Architecture (Rev 7.0):
 *   - EncoderBus owns the I2C bus and the TCA9548A. It sweeps channel 0..2,
 *     reads the 12-bit RAW ANGLE of each AS5600 and publishes a timestamped
 *     sample per axis through a lock-free seqlock.
 *   - On the ESP32 the sweep runs in its own FreeRTOS task pinned to core 0,
 *     so the ~375 us I2C sweep never blocks loopFOC() on core 1.
 *   - MuxedAS5600 is a SimpleFOC Sensor that consumes those samples and uses
 *     the *sample* timestamp for velocity estimation (not the time the FOC loop
 *     happened to look), avoiding velocity spikes from repeated stale reads.
 *
 * AS5600 register facts (ams DS000365):
 *   0x0B STATUS  bit5 MD (magnet detected), bit4 ML (too weak), bit3 MH (too strong)
 *   0x0C/0x0D RAW ANGLE[11:0] (12-bit, 4096 counts/rev — NOT 14-bit)
 *   0x1A AGC
 *   For RAW ANGLE the address pointer does not advance past the low byte, so once
 *   the pointer is parked on 0x0C a bare 2-byte read returns a fresh angle
 *   ("fast read": 1 frame instead of 2). This is self-tested at startup and
 *   automatically disabled if a device does not behave that way.
 */
#ifndef AS5600_MUX_H
#define AS5600_MUX_H

#include <stdint.h>
#include <atomic>
#include <Wire.h>
#include "tca9548a.h"

namespace as5600 {
constexpr uint8_t ADDR          = 0x36;
constexpr uint8_t REG_STATUS    = 0x0B;
constexpr uint8_t REG_RAW_ANGLE = 0x0C;
constexpr uint8_t REG_AGC       = 0x1A;
constexpr uint8_t STATUS_MD     = 0x20;
constexpr uint8_t STATUS_ML     = 0x10;
constexpr uint8_t STATUS_MH     = 0x08;
constexpr uint16_t COUNTS       = 4096;
}  // namespace as5600

enum class EncoderHealth : uint8_t {
    OK = 0,
    NOT_PRESENT,     // no ACK at 0x36 on this mux channel
    NO_MAGNET,       // STATUS.MD == 0
    MAGNET_WEAK,     // STATUS.ML == 1
    MAGNET_STRONG,   // STATUS.MH == 1
    BUS_FAULT,       // consecutive read failures exceeded the limit
};

const char* encoder_health_str(EncoderHealth h);

struct EncoderSample {
    uint16_t raw = 0;        // 0..4095
    float    angle = 0.0f;   // rad, 0..2pi
    uint32_t t_us = 0;       // micros() at the end of the I2C read
    uint32_t seq = 0;        // monotonically increasing per channel, 0 = never sampled
};

class EncoderBus {
public:
    static constexpr uint8_t  NUM_AXES              = 3;
    static constexpr uint8_t  MAX_CONSECUTIVE_FAILS = 8;    // ~3 ms of lost samples
    static constexpr uint16_t FAST_READ_TOLERANCE   = 8;    // counts (0.7 deg) for self-test

    /**
     * @param wire      I2C peripheral (not yet begun)
     * @param mux       TCA9548A on that peripheral
     * @param sda, scl  GPIOs (used for bus-clear recovery)
     * @param channels  mux channel for axis 0..2 (Pan, Roll, Pitch)
     */
    EncoderBus(TwoWire& wire, TCA9548A& mux, int8_t sda, int8_t scl,
               const uint8_t (&channels)[NUM_AXES]);

    /**
     * Bring up the bus at @p bus_hz (clamped to 400 kHz, the TCA9548A limit),
     * reset the mux, probe each AS5600, check its magnet, self-test fast read
     * and take one sample per axis. Returns true only if all axes are OK.
     */
    bool begin(uint32_t bus_hz = 400000);

    /** Read every healthy axis once. Returns number of successful reads. */
    uint8_t pollOnce();

    /** Copy the latest sample for @p axis. Lock-free, callable from any core. */
    bool latest(uint8_t axis, EncoderSample& out) const;

    EncoderHealth health(uint8_t axis) const { return health_[axis].load(std::memory_order_relaxed); }
    uint8_t  agc(uint8_t axis) const { return agc_[axis]; }
    bool     fastReadEnabled(uint8_t axis) const { return fast_read_[axis]; }
    uint32_t sweepCount() const { return sweeps_.load(std::memory_order_relaxed); }
    uint32_t recoveries() const { return recoveries_; }
    uint32_t lastSweepUs() const { return last_sweep_us_; }

#if defined(ARDUINO_ARCH_ESP32)
    /** Spawn the sweep task pinned to @p core (default core 0, away from FOC). */
    bool startTask(uint8_t core = 0, uint8_t priority = 5);
#endif

private:
    bool probe(uint8_t axis);
    bool readReg8(uint8_t reg, uint8_t& v);
    bool pointTo(uint8_t reg);
    bool readRawFull(uint16_t& raw);
    bool readRawFast(uint16_t& raw);
    bool readAxis(uint8_t axis, uint16_t& raw);
    void publish(uint8_t axis, uint16_t raw);
    void recover();

    TwoWire&  wire_;
    TCA9548A& mux_;
    int8_t    sda_, scl_;
    uint32_t  bus_hz_ = 400000;
    uint8_t   channel_[NUM_AXES];
    std::atomic<EncoderHealth> health_[NUM_AXES];   // written on core 0, read by the FOC loop on core 1
    bool      fast_read_[NUM_AXES] = {false, false, false};
    uint8_t   agc_[NUM_AXES] = {0, 0, 0};
    uint8_t   fails_[NUM_AXES] = {0, 0, 0};
    uint32_t  recoveries_ = 0;
    uint32_t  last_sweep_us_ = 0;
    std::atomic<uint32_t> sweeps_{0};

    // Seqlock per axis: writer makes seq odd, writes, makes it even.
    // Payload fields are relaxed atomics so the pattern is data-race-free under
    // the C++ memory model; on Xtensa LX7 these compile to plain 32-bit loads/stores.
    struct Slot {
        std::atomic<uint32_t> seq{0};
        std::atomic<uint32_t> raw{0};
        std::atomic<uint32_t> t_us{0};
        std::atomic<uint32_t> count{0};
    };
    Slot slot_[NUM_AXES];
};

#if __has_include(<SimpleFOC.h>)
#include <SimpleFOC.h>

/**
 * SimpleFOC Sensor fed by EncoderBus. update() only advances the sensor state
 * when a new sample has arrived and stamps it with the sample's own capture
 * time, so getVelocity() differentiates over the true sampling interval.
 */
class MuxedAS5600 : public Sensor {
public:
    MuxedAS5600(EncoderBus& bus, uint8_t axis) : bus_(bus), axis_(axis) {}

    void  init() override;
    void  update() override;
    float getSensorAngle() override;

    /** micros() age of the newest sample; used by the FOC watchdog. */
    uint32_t sampleAgeUs() const;
    bool healthy() const { return bus_.health(axis_) == EncoderHealth::OK; }

private:
    EncoderBus& bus_;
    uint8_t     axis_;
    uint32_t    last_seq_ = 0;
};
#endif

#endif  // AS5600_MUX_H
