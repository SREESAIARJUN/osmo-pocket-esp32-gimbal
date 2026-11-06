#include "as5600_mux.h"
#include <Arduino.h>

namespace {
constexpr float TWO_PI_F = 6.28318530718f;

inline float raw_to_rad(uint16_t raw) {
    return static_cast<float>(raw) * (TWO_PI_F / static_cast<float>(as5600::COUNTS));
}

inline uint16_t circ_diff(uint16_t a, uint16_t b) {
    uint16_t d = static_cast<uint16_t>((a - b) & (as5600::COUNTS - 1));
    return d > as5600::COUNTS / 2 ? static_cast<uint16_t>(as5600::COUNTS - d) : d;
}
}  // namespace

const char* encoder_health_str(EncoderHealth h) {
    switch (h) {
        case EncoderHealth::OK:            return "OK";
        case EncoderHealth::NOT_PRESENT:   return "NOT_PRESENT";
        case EncoderHealth::NO_MAGNET:     return "NO_MAGNET";
        case EncoderHealth::MAGNET_WEAK:   return "MAGNET_WEAK";
        case EncoderHealth::MAGNET_STRONG: return "MAGNET_STRONG";
        case EncoderHealth::BUS_FAULT:     return "BUS_FAULT";
    }
    return "?";
}

EncoderBus::EncoderBus(TwoWire& wire, TCA9548A& mux, int8_t sda, int8_t scl,
                       const uint8_t (&channels)[NUM_AXES])
    : wire_(wire), mux_(mux), sda_(sda), scl_(scl) {
    for (uint8_t i = 0; i < NUM_AXES; ++i) {
        channel_[i] = channels[i];
        health_[i]  = EncoderHealth::NOT_PRESENT;
    }
}

// --------------------------------------------------------------------------
// Low-level AS5600 transactions (assume the right mux channel is selected)
// --------------------------------------------------------------------------
bool EncoderBus::pointTo(uint8_t reg) {
    wire_.beginTransmission(as5600::ADDR);
    wire_.write(reg);
    return wire_.endTransmission(true) == 0;
}

bool EncoderBus::readReg8(uint8_t reg, uint8_t& v) {
    if (!pointTo(reg)) return false;
    if (wire_.requestFrom(as5600::ADDR, static_cast<uint8_t>(1)) != 1) return false;
    v = static_cast<uint8_t>(wire_.read());
    return true;
}

bool EncoderBus::readRawFast(uint16_t& raw) {
    if (wire_.requestFrom(as5600::ADDR, static_cast<uint8_t>(2)) != 2) return false;
    const uint8_t hi = static_cast<uint8_t>(wire_.read());
    const uint8_t lo = static_cast<uint8_t>(wire_.read());
    if (hi & 0xF0) return false;  // RAW ANGLE[15:12] are always 0: integrity check
    raw = static_cast<uint16_t>((hi << 8) | lo);
    return true;
}

bool EncoderBus::readRawFull(uint16_t& raw) {
    return pointTo(as5600::REG_RAW_ANGLE) && readRawFast(raw);
}

// --------------------------------------------------------------------------
// Bring-up
// --------------------------------------------------------------------------
bool EncoderBus::probe(uint8_t axis) {
    fast_read_[axis] = false;
    if (!mux_.select(channel_[axis])) {
        health_[axis] = EncoderHealth::NOT_PRESENT;
        return false;
    }
    uint8_t status = 0;
    if (!readReg8(as5600::REG_STATUS, status)) {
        health_[axis] = EncoderHealth::NOT_PRESENT;
        return false;
    }
    readReg8(as5600::REG_AGC, agc_[axis]);

    if (!(status & as5600::STATUS_MD))      health_[axis] = EncoderHealth::NO_MAGNET;
    else if (status & as5600::STATUS_ML)    health_[axis] = EncoderHealth::MAGNET_WEAK;
    else if (status & as5600::STATUS_MH)    health_[axis] = EncoderHealth::MAGNET_STRONG;
    else                                    health_[axis] = EncoderHealth::OK;
    if (health_[axis] != EncoderHealth::OK) return false;

    // Park the pointer on RAW ANGLE and self-test the single-frame fast read.
    // Several consecutive fast reads are required so that a device whose pointer
    // auto-increments (0x0C -> 0x0E ANGLE -> 0x10 ...) is caught on read 2+.
    uint16_t ref = 0;
    if (!readRawFull(ref)) {
        health_[axis] = EncoderHealth::NOT_PRESENT;
        return false;
    }
    bool fast_ok = true;
    for (int i = 0; i < 4 && fast_ok; ++i) {
        uint16_t r = 0;
        fast_ok = readRawFast(r) && circ_diff(r, ref) <= FAST_READ_TOLERANCE;
    }
    fast_read_[axis] = fast_ok;
    fails_[axis] = 0;
    publish(axis, ref);
    return true;
}

bool EncoderBus::begin(uint32_t bus_hz) {
    bus_hz_ = bus_hz > 400000 ? 400000 : bus_hz;  // TCA9548A is Fast-mode only
    wire_.begin(sda_, scl_, bus_hz_);
    wire_.setTimeOut(2);                          // ms; a stuck bus must not stall the sweep

    if (!mux_.begin()) {
        // Possibly a slave holding SDA from before reset: clear and retry once.
        wire_.end();
        i2c_bus_clear(sda_, scl_);
        wire_.begin(sda_, scl_, bus_hz_);
        wire_.setTimeOut(2);
        if (!mux_.begin()) return false;
    }

    bool all_ok = true;
    for (uint8_t a = 0; a < NUM_AXES; ++a) all_ok &= probe(a);
    return all_ok;
}

// --------------------------------------------------------------------------
// Runtime sweep
// --------------------------------------------------------------------------
bool EncoderBus::readAxis(uint8_t axis, uint16_t& raw) {
    if (!mux_.select(channel_[axis])) return false;
    return fast_read_[axis] ? readRawFast(raw) : readRawFull(raw);
}

void EncoderBus::publish(uint8_t axis, uint16_t raw) {
    Slot& s = slot_[axis];
    const uint32_t q = s.seq.load(std::memory_order_relaxed);
    s.seq.store(q + 1, std::memory_order_relaxed);         // odd: write in progress
    std::atomic_thread_fence(std::memory_order_release);
    s.raw.store(raw, std::memory_order_relaxed);
    s.t_us.store(static_cast<uint32_t>(micros()), std::memory_order_relaxed);
    s.count.store(s.count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    s.seq.store(q + 2, std::memory_order_release);         // even: stable
    last_good_us_[axis] = s.t_us.load(std::memory_order_relaxed);
}

bool EncoderBus::latest(uint8_t axis, EncoderSample& out) const {
    if (axis >= NUM_AXES) return false;
    const Slot& s = slot_[axis];
    uint32_t q1, q2;
    do {
        q1 = s.seq.load(std::memory_order_acquire);
        out.raw  = static_cast<uint16_t>(s.raw.load(std::memory_order_relaxed));
        out.t_us = s.t_us.load(std::memory_order_relaxed);
        out.seq  = s.count.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        q2 = s.seq.load(std::memory_order_relaxed);
    } while ((q1 & 1u) || q1 != q2);
    out.angle = raw_to_rad(out.raw);
    return out.seq != 0;
}

void EncoderBus::recover() {
    ++recoveries_;
    wire_.end();
    i2c_bus_clear(sda_, scl_);
    wire_.begin(sda_, scl_, bus_hz_);
    wire_.setTimeOut(2);
    mux_.hardReset();
    mux_.invalidate();
    // Re-park every AS5600 pointer (a reset/hung transfer may have moved it).
    for (uint8_t a = 0; a < NUM_AXES; ++a) {
        if (health_[a] == EncoderHealth::OK && mux_.select(channel_[a]))
            pointTo(as5600::REG_RAW_ANGLE);
    }
}

uint8_t EncoderBus::pollOnce() {
    const uint32_t t0 = static_cast<uint32_t>(micros());
    uint8_t ok = 0;

    for (uint8_t a = 0; a < NUM_AXES; ++a) {
        if (health_[a] != EncoderHealth::OK) continue;
        uint16_t raw = 0;
        if (readAxis(a, raw)) {
            publish(a, raw);
            fails_[a] = 0;
            recovered_[a] = false;
            ++ok;
            continue;
        }
        mux_.invalidate();  // channel state unknown after an error
        if (fails_[a] < 255) ++fails_[a];
        const uint32_t starved = static_cast<uint32_t>(micros()) - last_good_us_[a];

        if (!recovered_[a]) {
            if (fails_[a] >= MAX_CONSECUTIVE_FAILS || starved >= RECOVERY_AFTER_US) {
                // Recover immediately (not at end of sweep) so the remaining axes
                // are read on a cleared bus within this same sweep.
                recovered_[a] = true;
                recover();
            }
        } else if (fails_[a] >= 2 * MAX_CONSECUTIVE_FAILS || starved >= FAULT_AFTER_US) {
            health_[a] = EncoderHealth::BUS_FAULT;  // recovery did not help: latch fault
        }
    }

    last_sweep_us_ = static_cast<uint32_t>(micros()) - t0;
    sweeps_.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

#if defined(ARDUINO_ARCH_ESP32)
static void encoder_task(void* arg) {
    EncoderBus* bus = static_cast<EncoderBus*>(arg);
    for (;;) {
        // The I2C driver blocks on its ISR semaphore during each transfer, which
        // already yields core 0. If nothing was read (all axes faulted) the sweep
        // returns immediately, so sleep a tick to keep IDLE0 and the task WDT fed.
        // A periodic 1-tick sleep also bounds worst-case IDLE starvation to 64 sweeps.
        if (bus->pollOnce() == 0 || bus->sweepCount() % 64 == 0) vTaskDelay(1);
    }
}

bool EncoderBus::startTask(uint8_t core, uint8_t priority) {
    return xTaskCreatePinnedToCore(encoder_task, "enc_i2c", 4096, this, priority,
                                   nullptr, core) == pdPASS;
}
#endif

// --------------------------------------------------------------------------
// SimpleFOC adapter
// --------------------------------------------------------------------------
#if __has_include(<SimpleFOC.h>)
float MuxedAS5600::getSensorAngle() {
    EncoderSample s;
    if (!bus_.latest(axis_, s)) return -1.0f;  // <0 tells Sensor::update() to skip
    return s.angle;
}

void MuxedAS5600::init() {
    Sensor::init();
    EncoderSample s;
    if (bus_.latest(axis_, s)) {
        last_seq_     = s.seq;
        angle_prev    = s.angle;
        angle_prev_ts = static_cast<long>(s.t_us);
    }
}

void MuxedAS5600::update() {
    EncoderSample s;
    if (!bus_.latest(axis_, s) || s.seq == last_seq_) return;  // no new data
    last_seq_ = s.seq;
    const float d_angle = s.angle - angle_prev;
    if (fabsf(d_angle) > 0.8f * _2PI) full_rotations += (d_angle > 0) ? -1 : 1;
    angle_prev    = s.angle;
    angle_prev_ts = static_cast<long>(s.t_us);
}

uint32_t MuxedAS5600::sampleAgeUs() const {
    EncoderSample s;
    if (!bus_.latest(axis_, s)) return UINT32_MAX;
    return static_cast<uint32_t>(micros()) - s.t_us;
}
#endif
