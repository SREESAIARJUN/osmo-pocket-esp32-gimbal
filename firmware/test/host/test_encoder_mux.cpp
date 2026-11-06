// Rev 7.0 host unit tests: TCA9548A driver, multiplexed AS5600 EncoderBus,
// MuxedAS5600 SimpleFOC adapter and the compile-time ESP32-S3 pin map.
// Build & run: firmware/test/host/run_host_tests.sh
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "Arduino.h"
#include "Wire.h"
#include "SimpleFOC.h"
#include "gimbal_pins.h"
#include "gimbal_config.h"
#include "tca9548a.h"
#include "as5600_mux.h"

namespace mock {
uint32_t now_us = 1000;
std::map<int, int> pin_mode, pin_level;
int reset_pulses = 0;
int reset_pin = pins::ENC_MUX_RST_N;
}

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name(); struct name##_reg { name##_reg() { tests().push_back({#name, name}); } } name##_inst; static void name()

#include <vector>
#include <functional>
struct T { const char* n; void (*f)(); };
static std::vector<T>& tests() { static std::vector<T> v; return v; }

static const uint8_t kCh[3] = {ENC_MUX_CH_PAN, ENC_MUX_CH_ROLL, ENC_MUX_CH_PITCH};

struct Rig {
    TwoWire    wire;
    TCA9548A   mux{wire, ENC_MUX_ADDR, pins::ENC_MUX_RST_N};
    EncoderBus bus{wire, mux, pins::ENC_I2C_SDA, pins::ENC_I2C_SCL, kCh};
    Rig(uint16_t a = 100, uint16_t b = 2000, uint16_t c = 3500) {
        wire.enc[0].raw = a; wire.enc[1].raw = b; wire.enc[2].raw = c;
        for (int i = 3; i < 8; ++i) wire.enc[i].present = false;
    }
};

static uint16_t raw_of(EncoderBus& bus, uint8_t axis) {
    EncoderSample s; bus.latest(axis, s); return s.raw;
}

// ---------------------------------------------------------------------------
TEST(pinmap_is_valid_for_esp32s3) {
    CHECK(pins::all_usable());
    CHECK(pins::all_unique());
    CHECK(!pins::exists_on_s3(22));          // the Rev 6.0 SCL pin
    for (int p = 22; p <= 25; ++p) CHECK(!pins::usable(p));
    for (int p = 26; p <= 37; ++p) CHECK(!pins::usable(p));  // flash + octal PSRAM
    CHECK(!pins::usable(0) && !pins::usable(3) && !pins::usable(45) && !pins::usable(46));
    CHECK(!pins::usable(19) && !pins::usable(20));
    CHECK(pins::usable(pins::ENC_I2C_SDA) && pins::usable(pins::ENC_I2C_SCL));
}

TEST(legacy_shared_bus_reproduces_address_conflict) {
    // Rev <= 6.0 topology: three AS5600 @0x36 directly on one bus, no mux.
    TwoWire w; w.mux_present = false; w.hz = 400000;
    for (int i = 3; i < 8; ++i) w.enc[i].present = false;
    w.enc[0].raw = 100; w.enc[1].raw = 2000; w.enc[2].raw = 3500;
    w.beginTransmission(0x36); w.write(0x0C); CHECK(w.endTransmission() == 0);
    CHECK(w.requestFrom((uint8_t)0x36, (uint8_t)2) == 2);
    uint16_t r = (uint16_t)((w.read() << 8) | w.read());
    CHECK(r == (100 & 2000 & 3500));         // wired-AND garbage: matches no axis
    CHECK(r != 100 && r != 2000 && r != 3500);
}

TEST(begin_brings_up_mux_and_all_three_encoders) {
    Rig rig;
    CHECK(rig.bus.begin(ENC_I2C_HZ));
    CHECK(rig.wire.hz == 400000);
    CHECK(mock::reset_pulses >= 1);          // RESET pulsed on GPIO2
    for (int a = 0; a < 3; ++a) {
        CHECK(rig.bus.health(a) == EncoderHealth::OK);
        CHECK(rig.bus.fastReadEnabled(a));
        CHECK(rig.bus.agc(a) == 0x80);
    }
}

TEST(each_axis_reads_its_own_encoder_through_the_mux) {
    Rig rig(100, 2000, 3500);
    CHECK(rig.bus.begin());
    rig.wire.enc[0].raw = 111; rig.wire.enc[1].raw = 2222; rig.wire.enc[2].raw = 3333;
    CHECK(rig.bus.pollOnce() == 3);
    CHECK(raw_of(rig.bus, 0) == 111);
    CHECK(raw_of(rig.bus, 1) == 2222);
    CHECK(raw_of(rig.bus, 2) == 3333);
    // Exactly one channel enabled after every transaction
    CHECK(rig.wire.mux_mask == (1u << ENC_MUX_CH_PITCH));
}

TEST(bus_speed_is_clamped_to_tca9548a_fast_mode) {
    Rig rig;
    CHECK(rig.bus.begin(1000000));
    CHECK(rig.wire.hz == 400000);
}

TEST(sweep_timing_budget_at_400khz) {
    Rig rig;
    CHECK(rig.bus.begin());
    const uint64_t b0 = rig.wire.bits;
    const uint32_t N = 100;
    for (uint32_t i = 0; i < N; ++i) rig.bus.pollOnce();
    const double us_per_sweep = (rig.wire.bits - b0) * 1e6 / 400000.0 / N;
    std::printf("  sweep: %.1f us bus time (3 axes) -> %.0f Hz per-axis encoder rate\n",
                us_per_sweep, 1e6 / us_per_sweep);
    CHECK(us_per_sweep < 400.0);             // fast read: 3 x (mux write 20b + 2-byte read 29b)
    CHECK(rig.bus.lastSweepUs() > 0);
}

TEST(fast_read_disabled_for_auto_incrementing_device) {
    Rig rig;
    rig.wire.enc[1].auto_increment = true;   // pointer walks 0x0C->0x0E->0x10...
    rig.wire.enc[1].raw = 1234;
    CHECK(rig.bus.begin());
    CHECK(rig.bus.fastReadEnabled(0));
    CHECK(!rig.bus.fastReadEnabled(1));      // self-test caught it
    for (int i = 0; i < 5; ++i) rig.bus.pollOnce();
    CHECK(raw_of(rig.bus, 1) == 1234);       // full (pointer-write) reads stay correct
}

TEST(magnet_diagnostics_gate_the_axis) {
    Rig rig;
    rig.wire.enc[0].status = 0x00;           // MD=0
    rig.wire.enc[1].status = 0x20 | 0x10;    // ML
    rig.wire.enc[2].status = 0x20 | 0x08;    // MH
    CHECK(!rig.bus.begin());
    CHECK(rig.bus.health(0) == EncoderHealth::NO_MAGNET);
    CHECK(rig.bus.health(1) == EncoderHealth::MAGNET_WEAK);
    CHECK(rig.bus.health(2) == EncoderHealth::MAGNET_STRONG);
    CHECK(rig.bus.pollOnce() == 0);          // unhealthy axes are never polled
}

TEST(missing_encoder_is_not_present) {
    Rig rig;
    rig.wire.enc[2].present = false;
    CHECK(!rig.bus.begin());
    CHECK(rig.bus.health(0) == EncoderHealth::OK);
    CHECK(rig.bus.health(2) == EncoderHealth::NOT_PRESENT);
}

TEST(missing_mux_fails_begin) {
    Rig rig;
    rig.wire.mux_present = false;
    rig.wire.enc[1].present = rig.wire.enc[2].present = false;
    CHECK(!rig.bus.begin());
    CHECK(rig.wire.ends >= 1);               // bus-clear recovery was attempted
}

TEST(transient_fault_triggers_single_recovery_then_resumes) {
    Rig rig;
    CHECK(rig.bus.begin());
    const int resets0 = mock::reset_pulses;
    rig.wire.enc[1].nack = true;
    for (int i = 0; i < EncoderBus::MAX_CONSECUTIVE_FAILS; ++i) rig.bus.pollOnce();
    CHECK(rig.bus.recoveries() == 1);
    CHECK(mock::reset_pulses == resets0 + 1);
    CHECK(rig.bus.health(1) == EncoderHealth::OK);
    rig.wire.enc[1].nack = false;
    rig.wire.enc[1].raw = 777;
    CHECK(rig.bus.pollOnce() == 3);
    CHECK(raw_of(rig.bus, 1) == 777);
    CHECK(rig.bus.health(1) == EncoderHealth::OK);
}

TEST(persistent_fault_latches_bus_fault_without_killing_other_axes) {
    Rig rig;
    CHECK(rig.bus.begin());
    rig.wire.enc[1].nack = true;
    for (int i = 0; i < 2 * EncoderBus::MAX_CONSECUTIVE_FAILS; ++i) rig.bus.pollOnce();
    CHECK(rig.bus.health(1) == EncoderHealth::BUS_FAULT);
    CHECK(rig.bus.recoveries() == 1);
    rig.wire.enc[0].raw = 42; rig.wire.enc[2].raw = 4000;
    CHECK(rig.bus.pollOnce() == 2);
    CHECK(raw_of(rig.bus, 0) == 42);
    CHECK(raw_of(rig.bus, 2) == 4000);
}

// Worst-case age of the newest sample across the given axes, as the FOC watchdog sees it.
static uint32_t max_age(EncoderBus& bus) {
    uint32_t m = 0;
    for (uint8_t a = 0; a < 3; ++a) {
        EncoderSample smp;
        if (bus.health(a) == EncoderHealth::OK && bus.latest(a, smp)) {
            uint32_t age = mock::now_us - smp.t_us;
            if (age > m) m = age;
        }
    }
    return m;
}

TEST(bus_hang_recovers_before_foc_stale_watchdog) {
    // Review finding (Rev 7.0): with timeout-type faults (2 ms per transaction)
    // a count-only trigger reached recovery after ~16-48 ms, after the 10 ms
    // FOC watchdog had already latched the axes off. Recovery is now time-based.
    Rig rig;
    CHECK(rig.bus.begin());
    rig.wire.hang = true;                    // e.g. a slave holding SDA low
    uint32_t worst = 0;
    for (int i = 0; i < 6; ++i) {
        rig.bus.pollOnce();
        uint32_t a = max_age(rig.bus);
        if (a > worst) worst = a;
    }
    std::printf("  worst sample age through bus hang: %u us (watchdog %u us)\n",
                worst, (unsigned)ENC_STALE_LIMIT_US);
    CHECK(worst < ENC_STALE_LIMIT_US);
    CHECK(rig.bus.recoveries() == 1);
    for (int a = 0; a < 3; ++a) CHECK(rig.bus.health(a) == EncoderHealth::OK);
    CHECK(rig.bus.pollOnce() == 3);
}

TEST(unrecoverable_bus_hang_latches_fault_quickly) {
    Rig rig;
    CHECK(rig.bus.begin());
    rig.wire.hang = true;
    rig.wire.hang_survives_clear = true;     // hard short: bus-clear cannot help
    const uint32_t t0 = mock::now_us;
    for (int i = 0; i < 20; ++i) rig.bus.pollOnce();
    for (int a = 0; a < 3; ++a) CHECK(rig.bus.health(a) == EncoderHealth::BUS_FAULT);
    CHECK(rig.bus.pollOnce() == 0);          // faulted axes no longer cost bus time
    std::printf("  all axes BUS_FAULT after %u us\n", (unsigned)(mock::now_us - t0));
}

TEST(mux_select_is_cached) {
    TwoWire w; w.hz = 400000;
    TCA9548A mux(w, 0x70, -1);
    CHECK(mux.begin());
    const uint32_t n0 = w.mux_writes;
    CHECK(mux.select(3)); CHECK(mux.select(3)); CHECK(mux.select(3));
    CHECK(w.mux_writes == n0 + 1);
    CHECK(!mux.select(8));                   // out of range
    mux.invalidate();
    CHECK(mux.select(3));
    CHECK(w.mux_writes == n0 + 2);
    uint8_t m = 0; CHECK(mux.readControl(m) && m == 0x08);
}

TEST(sample_sequence_and_timestamps_advance) {
    Rig rig;
    CHECK(rig.bus.begin());
    EncoderSample a, b;
    CHECK(rig.bus.latest(0, a));
    rig.bus.pollOnce();
    CHECK(rig.bus.latest(0, b));
    CHECK(b.seq == a.seq + 1);
    CHECK(b.t_us > a.t_us);
    CHECK(std::fabs(b.angle - 100 * 6.28318530718f / 4096.0f) < 1e-5f);
    CHECK(!rig.bus.latest(3, b));
}

TEST(muxed_sensor_uses_sample_timestamps_and_tracks_turns) {
    Rig rig(4000, 0, 0);
    CHECK(rig.bus.begin());
    MuxedAS5600 s(rig.bus, 0);
    s.init();
    EncoderSample smp; rig.bus.latest(0, smp);
    CHECK(s.timestamp() == (long)smp.t_us);

    // No new sample -> update() must not touch the timestamp
    mock::now_us += 500;
    s.update();
    CHECK(s.timestamp() == (long)smp.t_us);

    // Cross zero forwards: 4000 -> 50 counts => +1 full rotation
    rig.wire.enc[0].raw = 50;
    rig.bus.pollOnce();
    s.update();
    rig.bus.latest(0, smp);
    CHECK(s.timestamp() == (long)smp.t_us);
    const float expect = 6.28318530718f + 50 * 6.28318530718f / 4096.0f;
    CHECK(std::fabs(s.getAngle() - expect) < 1e-4f);
    CHECK(s.healthy());
    CHECK(s.sampleAgeUs() < 1000);
}

int main() {
    std::printf("Rev 7.0 encoder-mux host tests\n");
    for (auto& t : tests()) {
        const int f0 = g_fail;
        mock::reset_pulses = 0;
        mock::pin_level.clear();
        t.f();
        std::printf("%s %s\n", g_fail == f0 ? "[PASS]" : "[FAIL]", t.n);
    }
    std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
