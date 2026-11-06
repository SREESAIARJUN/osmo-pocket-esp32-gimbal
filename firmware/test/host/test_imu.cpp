// Rev 57.0 host tests: ICM-42688-P SPI FIFO driver, Madgwick AHRS, gyro
// temperature-bias model and the full 1 kHz ImuChannel pipeline, run against a
// register-level ICM-42688-P model (FakeIcm below).
// Build & run: firmware/test/host/run_host_tests.sh
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <random>
#include <vector>
#include "Arduino.h"
#include "icm42688.h"
#include "madgwick_ahrs.h"
#include "imu_bias.h"
#include "imu_pipeline.h"

namespace mock {
uint32_t now_us = 1000;
std::map<int, int> pin_mode, pin_level;
int reset_pulses = 0;
int reset_pin = -1;
}

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { const double _a = (a), _b = (b); if (std::fabs(_a - _b) <= (tol)) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL %s:%d  %s = %.6g, expected %.6g +/- %.3g\n", __FILE__, __LINE__, #a, _a, _b, (double)(tol)); } } while (0)
#define TEST(name) static void name(); struct name##_reg { name##_reg() { tests().push_back({#name, name}); } } name##_inst; static void name()
struct T { const char* n; void (*f)(); };
static std::vector<T>& tests() { static std::vector<T> v; return v; }

using namespace icm42688;
static constexpr double D2R = 3.14159265358979323846 / 180.0;
static constexpr double R2D = 180.0 / 3.14159265358979323846;

// ===========================================================================
// Register-level ICM-42688-P model
// ===========================================================================
struct Truth {
    double gyro[3] = {0, 0, 0};    // rad/s body == sensor (identity mount in these tests)
    double accel[3] = {0, 0, 1};   // g
    double temp = 25.0;            // degC
};

struct FakeIcm : SpiDevice {
    uint8_t reg[128] = {};
    uint8_t stuck_mask[128] = {};       // bits that refuse to change (config-fault injection)
    std::deque<uint8_t> fifo;           // byte stream
    bool present = true;
    uint8_t who = WHO_AM_I_VALUE;       // e.g. 0x42 for an ICM-42605 fitted by mistake
    // device clock: real sample period = 1000 us / (1 + clock_err)
    double clock_err = 0.0;
    double next_sample_us = 0.0;
    uint16_t tmst = 0;
    bool running = false;
    std::function<Truth(double t_s)> truth = [](double) { return Truth{}; };
    double b0_dps[3] = {0, 0, 0}, b1_dps_c[3] = {0, 0, 0};   // true bias(T)
    double gyro_noise_dps = 0.0, accel_noise_g = 0.0;
    std::mt19937 rng{12345};
    uint32_t generated = 0;
    int corrupt_next = 0;               // corrupt the header of the next N generated records

    FakeIcm() { softReset(); }

    void softReset() {
        std::memset(reg, 0, sizeof reg);
        reg[REG_INTF_CONFIG0] = 0x30;
        reg[REG_GYRO_CONFIG0] = 0x06;
        reg[REG_ACCEL_CONFIG0] = 0x06;
        reg[REG_GYRO_ACCEL_CONFIG0] = 0x11;
        reg[REG_TMST_CONFIG] = 0x23;
        reg[REG_WHO_AM_I] = who;
        fifo.clear();
        running = false;
    }
    bool hires() const { return (reg[REG_FIFO_CONFIG1] & 0x1F) == 0x1F && reg[REG_FIFO_CONFIG] == 0x40; }

    static void put20(uint8_t* hi, uint8_t* lo, int32_t v, uint8_t& nib) {
        const int32_t h = v >> 4;  // arithmetic shift
        *hi = static_cast<uint8_t>((h >> 8) & 0xFF);
        *lo = static_cast<uint8_t>(h & 0xFF);
        nib = static_cast<uint8_t>(v & 0xF);
    }
    static int32_t clamp20(double v) {
        long r = std::lround(v);
        if (r > 524287) r = 524287;
        if (r < -524287) r = -524287;
        return static_cast<int32_t>(r);
    }

    void generate(double t_host_us) {
        std::normal_distribution<double> n01(0.0, 1.0);
        const Truth tr = truth(t_host_us * 1e-6);
        uint8_t p[20] = {};
        p[0] = HDR_PACKET4;
        if (corrupt_next > 0) { p[0] = 0x68; --corrupt_next; }
        for (int i = 0; i < 3; ++i) {
            const double bias = b0_dps[i] + b1_dps_c[i] * (tr.temp - 25.0);
            const double g_dps = tr.gyro[i] * R2D + bias + gyro_noise_dps * n01(rng);
            const double a_g = tr.accel[i] + accel_noise_g * n01(rng);
            uint8_t na = 0, ng = 0;
            put20(&p[1 + 2 * i], &p[2 + 2 * i], clamp20(a_g * ACCEL_LSB_PER_G), na);
            put20(&p[7 + 2 * i], &p[8 + 2 * i], clamp20(g_dps * GYRO_LSB_PER_DPS), ng);
            p[17 + i] = static_cast<uint8_t>((na << 4) | ng);
        }
        const int16_t t = static_cast<int16_t>(std::lround((tr.temp - 25.0) * TEMP_LSB_PER_C));
        p[13] = static_cast<uint8_t>((t >> 8) & 0xFF);
        p[14] = static_cast<uint8_t>(t & 0xFF);
        tmst = static_cast<uint16_t>(tmst + 1000);
        p[15] = static_cast<uint8_t>(tmst >> 8);
        p[16] = static_cast<uint8_t>(tmst & 0xFF);
        fifo.insert(fifo.end(), p, p + 20);
        while (fifo.size() > FIFO_MAX_RECORDS * RECORD_BYTES) {   // stream mode: drop oldest record
            for (size_t k = 0; k < RECORD_BYTES; ++k) fifo.pop_front();
            reg[REG_INT_STATUS] |= INT_STATUS_FIFO_FULL;
        }
        ++generated;
    }

    void advance() {
        if (!running || !hires()) return;
        const double period = 1000.0 / (1.0 + clock_err);
        while (next_sample_us <= static_cast<double>(mock::now_us)) {
            generate(next_sample_us);
            next_sample_us += period;
        }
    }

    uint8_t readByte(uint8_t r) {
        if (r == REG_FIFO_DATA) {
            if (fifo.empty()) return 0xFF;
            const uint8_t b = fifo.front();
            fifo.pop_front();
            return b;
        }
        if (r == REG_FIFO_COUNTH || r == REG_FIFO_COUNTL) {
            const size_t c = (reg[REG_INTF_CONFIG0] & 0x40) ? fifo.size() / RECORD_BYTES : fifo.size();
            return static_cast<uint8_t>(r == REG_FIFO_COUNTH ? (c >> 8) : (c & 0xFF));
        }
        const uint8_t v = reg[r & 0x7F];
        if (r == REG_INT_STATUS) reg[REG_INT_STATUS] = 0;  // clear on read
        return v;
    }

    bool read(uint8_t r, uint8_t* buf, size_t n) override {
        advance();
        if (!present) { std::memset(buf, 0xFF, n); return true; }   // MISO pulled high
        for (size_t i = 0; i < n; ++i) {
            buf[i] = readByte(r);
            if (r != REG_FIFO_DATA) r = static_cast<uint8_t>(r + 1);
        }
        return true;
    }

    bool write(uint8_t r, uint8_t v) override {
        advance();
        if (!present) return true;
        if (r == REG_DEVICE_CONFIG && (v & 1)) { softReset(); return true; }
        if (r == REG_SIGNAL_PATH_RESET) { if (v & SIGNAL_PATH_FIFO_FLUSH) fifo.clear(); return true; }
        reg[r] = static_cast<uint8_t>((reg[r] & stuck_mask[r]) | (v & ~stuck_mask[r]));
        if (r == REG_PWR_MGMT0) {
            const bool on = (reg[r] & 0x0F) == 0x0F;
            if (on && !running) next_sample_us = static_cast<double>(mock::now_us) + 1000.0 / (1.0 + clock_err);
            running = on;
        }
        return true;
    }
};

static ImuChannel::Config default_cfg() {
    ImuChannel::Config c;
    c.ahrs.beta = 0.02f;
    c.ahrs.beta_init = 1.0f;
    c.ahrs.init_time_s = 0.5f;
    return c;
}

// Run the pipeline at a 1 ms service cadence for @p seconds.
static void run(ImuChannel& ch, double seconds, uint32_t step_us = 1000) {
    const uint32_t end = mock::now_us + static_cast<uint32_t>(seconds * 1e6);
    while (static_cast<int32_t>(end - mock::now_us) > 0) {
        mock::now_us += step_us;
        ch.service(mock::now_us);
    }
}

static double wrap_deg(double a) {
    while (a > 180.0) a -= 360.0;
    while (a < -180.0) a += 360.0;
    return a;
}

// ===========================================================================
// Driver
// ===========================================================================
TEST(decode_20bit_sign_extension_and_scaling) {
    uint8_t p[20] = {};
    p[0] = HDR_PACKET4;
    uint8_t na, ng;
    FakeIcm::put20(&p[1], &p[2], -1, na);          // accel X = -1 LSB
    FakeIcm::put20(&p[7], &p[8], -1, ng);          // gyro X = -1 LSB
    p[17] = static_cast<uint8_t>((na << 4) | ng);
    FakeIcm::put20(&p[3], &p[4], 32768, na);       // accel Y = +1 g
    FakeIcm::put20(&p[9], &p[10], 524287, ng);     // gyro Y = +full scale
    p[18] = static_cast<uint8_t>((na << 4) | ng);
    FakeIcm::put20(&p[5], &p[6], -32768 * 2, na);  // accel Z = -2 g
    FakeIcm::put20(&p[11], &p[12], -262144, ng);   // gyro Z = -1000 dps
    p[19] = static_cast<uint8_t>((na << 4) | ng);
    p[13] = 0x0A; p[14] = 0x58;                    // 2648 LSB -> 25 + 19.988 degC
    p[15] = 0xFF; p[16] = 0xFE;
    ImuRecord r;
    CHECK(ICM42688::decode(p, r));
    CHECK_NEAR(r.accel[0], -1.0 / 32768.0, 1e-9);
    CHECK_NEAR(r.gyro[0] * R2D, -1.0 / 262.144, 1e-6);
    CHECK_NEAR(r.accel[1], 1.0, 1e-6);
    CHECK_NEAR(r.gyro[1] * R2D, 524287.0 / 262.144, 1e-3);   // ~2000 dps, not clipped to 16 bits
    CHECK_NEAR(r.accel[2], -2.0, 1e-6);
    CHECK_NEAR(r.gyro[2] * R2D, -1000.0, 1e-3);
    CHECK_NEAR(r.temp_c, 25.0 + 2648.0 / 132.48, 1e-4);
    CHECK(r.tmst == 0xFFFE);
    // Invalid-sample marker 0x8000 in [19:4]
    p[7] = 0x80; p[8] = 0x00;
    CHECK(!ICM42688::decode(p, r));
}

TEST(begin_configures_and_verifies_registers) {
    FakeIcm f;
    ICM42688 imu(f);
    f.reg[REG_FIFO_CONFIG1] = 0x55;  // garbage left by a previous boot: soft reset must clear it
    CHECK(imu.begin() == ImuStatus::OK);
    CHECK(f.reg[REG_INTF_CONFIG0] == 0x73);
    CHECK(f.reg[REG_PWR_MGMT0] == 0x0F);
    CHECK(f.reg[REG_GYRO_CONFIG0] == 0x06);
    CHECK(f.reg[REG_ACCEL_CONFIG0] == 0x06);
    CHECK(f.reg[REG_TMST_CONFIG] == 0x01);
    CHECK(f.reg[REG_FIFO_CONFIG1] == 0x1F);
    CHECK(f.reg[REG_FIFO_CONFIG] == 0x40);
    CHECK(f.fifo.empty() || f.fifo.size() <= 2 * RECORD_BYTES);  // start-up transient flushed

    FakeIcm absent; absent.present = false;
    ICM42688 imu2(absent);
    CHECK(imu2.begin() == ImuStatus::NOT_PRESENT);
    ImuRecord r[4];
    CHECK(imu2.readFifo(r, 4) == 0);

    FakeIcm stuck; stuck.stuck_mask[REG_FIFO_CONFIG1] = 0x10;  // HIRES bit cannot be set
    ICM42688 imu3(stuck);
    CHECK(imu3.begin() == ImuStatus::CONFIG_FAULT);

    FakeIcm wrong; wrong.who = 0x42;   // ICM-42605 fitted by mistake
    ICM42688 imu4(wrong);
    CHECK(imu4.begin() == ImuStatus::NOT_PRESENT);
}

TEST(fifo_drains_records_in_order_with_timestamps) {
    FakeIcm f;
    ICM42688 imu(f);
    CHECK(imu.begin() == ImuStatus::OK);
    f.fifo.clear();
    mock::now_us += 5000;
    ImuRecord r[ICM42688::MAX_BATCH];
    uint16_t rem = 99;
    const size_t n = imu.readFifo(r, ICM42688::MAX_BATCH, &rem);
    CHECK(n == 5);
    CHECK(rem == 0);
    for (size_t k = 1; k < n; ++k) CHECK(static_cast<uint16_t>(r[k].tmst - r[k - 1].tmst) == 1000);
    CHECK_NEAR(r[0].accel[2], 1.0, 1e-4);
    // Batch cap: 40 ms backlog is read 16 records at a time and reports the remainder.
    mock::now_us += 40000;
    CHECK(imu.readFifo(r, ICM42688::MAX_BATCH, &rem) == ICM42688::MAX_BATCH);
    CHECK(rem == 40 - ICM42688::MAX_BATCH);
    CHECK(imu.overflows() == 0);
}

TEST(fifo_overflow_and_header_corruption_force_resync) {
    FakeIcm f;
    ICM42688 imu(f);
    CHECK(imu.begin() == ImuStatus::OK);
    ImuRecord r[ICM42688::MAX_BATCH];
    imu.takeResync();
    mock::now_us += 150000;  // 150 ms unserviced > 102-record FIFO
    CHECK(imu.readFifo(r, ICM42688::MAX_BATCH) == ICM42688::MAX_BATCH);
    CHECK(imu.overflows() == 1);
    CHECK(imu.takeResync());
    CHECK(!imu.takeResync());
    // Drain, then inject a corrupted header: driver flushes and flags resync.
    for (int i = 0; i < 10; ++i) imu.readFifo(r, ICM42688::MAX_BATCH);
    f.corrupt_next = 1;
    mock::now_us += 3000;
    const size_t n = imu.readFifo(r, ICM42688::MAX_BATCH);
    CHECK(n == 0);
    CHECK(imu.headerErrors() == 1);
    CHECK(imu.takeResync());
    CHECK(f.fifo.empty());
    mock::now_us += 3000;
    CHECK(imu.readFifo(r, ICM42688::MAX_BATCH) == 3);  // stream recovered, aligned
}

// ===========================================================================
// Madgwick
// ===========================================================================
static void gravity_in_body(double roll, double pitch, double a[3]) {
    a[0] = -std::sin(pitch);
    a[1] = std::sin(roll) * std::cos(pitch);
    a[2] = std::cos(roll) * std::cos(pitch);
}

TEST(madgwick_aligns_from_accel_and_holds) {
    MadgwickAHRS f;
    double a[3];
    gravity_in_body(20 * D2R, -10 * D2R, a);
    f.update(0, 0, 0, (float)a[0], (float)a[1], (float)a[2], 0.001f);
    CHECK(f.initialized());
    CHECK_NEAR(f.roll() * R2D, 20.0, 1e-3);
    CHECK_NEAR(f.pitch() * R2D, -10.0, 1e-3);
    CHECK_NEAR(f.yaw() * R2D, 0.0, 1e-3);
    std::mt19937 rng(7);
    std::normal_distribution<double> n(0, 1);
    for (int i = 0; i < 20000; ++i) {
        f.update((float)(0.044 * D2R * n(rng)), (float)(0.044 * D2R * n(rng)), (float)(0.044 * D2R * n(rng)),
                 (float)(a[0] + 0.001 * n(rng)), (float)(a[1] + 0.001 * n(rng)), (float)(a[2] + 0.001 * n(rng)), 0.001f);
    }
    CHECK_NEAR(f.roll() * R2D, 20.0, 0.05);
    CHECK_NEAR(f.pitch() * R2D, -10.0, 0.05);
    float g[3];
    f.gravityBody(g);
    CHECK_NEAR(g[0], a[0], 1e-3);
    CHECK_NEAR(g[2], a[2], 1e-3);
}

TEST(madgwick_tracks_rotation_and_converges_from_error) {
    MadgwickAHRS f;
    f.initFromAccel(0, 0, 1);
    // 90 deg/s roll for 1 s with consistent gravity: pure tracking.
    for (int i = 1; i <= 1000; ++i) {
        double a[3];
        gravity_in_body(90.0 * D2R * i * 1e-3, 0, a);
        f.update((float)(90 * D2R), 0, 0, (float)a[0], (float)a[1], (float)a[2], 0.001f);
    }
    CHECK_NEAR(f.roll() * R2D, 90.0, 0.2);
    // Converge from a 5 deg initial error with the steady-state gain only.
    MadgwickAHRS::Config c; c.init_time_s = 0.0f;
    MadgwickAHRS g(c);
    g.initFromAccel(0, std::sin(5 * D2R), std::cos(5 * D2R));
    for (int i = 0; i < 10000; ++i) g.update(0, 0, 0, 0, 0, 1, 0.001f);
    CHECK(std::fabs(g.roll() * R2D) < 0.05);   // beta = 0.02 rad/s -> 1.15 deg/s slew
}

TEST(madgwick_accel_gating_rejects_linear_acceleration) {
    // Level and not rotating, but the handle is swung: lateral acceleration for 1 s.
    auto tilt_after = [](float lateral_g, bool gate) {
        MadgwickAHRS::Config c; c.init_time_s = 0.0f;
        if (!gate) { c.gate_lo_g = 10.0f; c.gate_hi_g = 20.0f; }
        MadgwickAHRS f(c);
        f.initFromAccel(0, 0, 1);
        for (int i = 0; i < 1000; ++i) f.update(0, 0, 0, 0, lateral_g, 1.0f, 0.001f);
        return std::fabs(f.roll() * R2D);
    };
    // 0.8 g swing: |a| = 1.28 g -> weight 0, horizon untouched.
    CHECK(tilt_after(0.8f, true) < 1e-4);
    CHECK(tilt_after(0.8f, false) > 1.0);
    // 0.3 g: |a| = 1.044 g is inside the gate's full-weight band. A magnitude gate
    // cannot see a small lateral push; the bound then comes from beta alone:
    // |q_dot| = beta  ->  attitude slew <= 2*beta = 0.04 rad/s = 2.29 deg/s.
    const double t03 = tilt_after(0.3f, true);
    CHECK(t03 <= 2.0 * 0.02 * R2D + 1e-3);
    CHECK(t03 > 0.5);   // documents the limitation rather than hiding it
    CHECK_NEAR(MadgwickAHRS::gateWeight(1.0f, 0.05f, 0.2f), 1.0, 1e-6);
    CHECK_NEAR(MadgwickAHRS::gateWeight(1.125f, 0.05f, 0.2f), 0.5, 1e-5);
    CHECK_NEAR(MadgwickAHRS::gateWeight(0.7f, 0.05f, 0.2f), 0.0, 1e-6);
}

TEST(madgwick_rejects_bad_dt_and_recovers_from_nan) {
    MadgwickAHRS f;
    f.initFromAccel(0, 0, 1);
    f.update(1, 0, 0, 0, 0, 1, -0.001f);
    f.update(1, 0, 0, 0, 0, 1, 0.5f);
    CHECK_NEAR(f.roll(), 0.0, 1e-6);
    f.update(NAN, 0, 0, 0, 0, 1, 0.001f);
    CHECK(!f.initialized());                       // reset, re-align on next sample
    f.update(0, 0, 0, 0, 0, 1, 0.001f);
    CHECK(f.initialized());
    CHECK(std::isfinite(f.roll()));
}

// ===========================================================================
// Temperature bias model
// ===========================================================================
TEST(stillness_detector_accepts_still_rejects_motion) {
    StillnessDetector d;
    StillnessDetector::Window w;
    std::mt19937 rng(3);
    std::normal_distribution<double> n(0, 1);
    auto feed = [&](std::function<void(int, float*, float*)> gen) {
        bool any = false, still = false;
        for (int i = 0; i < 500; ++i) {
            float g[3], a[3];
            gen(i, g, a);
            if (d.push(g, a, 30.0f, w)) still = true;
            any = true;
        }
        (void)any;
        return still;
    };
    // Still with bias + noise
    CHECK(feed([&](int, float* g, float* a) {
        for (int k = 0; k < 3; ++k) g[k] = (float)(0.005 + 0.044 * D2R * n(rng));
        a[0] = (float)(0.0005 * n(rng)); a[1] = (float)(0.0005 * n(rng)); a[2] = (float)(1 + 0.0005 * n(rng));
    }));
    CHECK_NEAR(w.gyro_mean[0], 0.005, 2e-4);
    CHECK_NEAR(w.temp_mean, 30.0, 1e-4);
    // Hand tremor: 2 dps 8 Hz
    CHECK(!feed([&](int i, float* g, float* a) {
        g[0] = (float)(2 * D2R * std::sin(2 * M_PI * 8 * i * 1e-3)); g[1] = g[2] = 0;
        a[0] = 0; a[1] = 0; a[2] = 1;
    }));
    // Slow steady 0.5 dps roll: low variance, plausible "bias" — caught by gravity rotation.
    CHECK(!feed([&](int i, float* g, float* a) {
        g[0] = (float)(0.5 * D2R); g[1] = g[2] = 0;
        double ga[3]; gravity_in_body(0.5 * D2R * i * 1e-3, 0, ga);
        a[0] = (float)ga[0]; a[1] = (float)ga[1]; a[2] = (float)ga[2];
    }));
    // Free fall / being carried with acceleration
    CHECK(!feed([&](int, float* g, float* a) { g[0] = g[1] = g[2] = 0; a[0] = 0; a[1] = 0; a[2] = 0.8f; }));
}

TEST(bias_model_learns_offset_then_slope) {
    GyroTempBias b;
    std::mt19937 rng(11);
    std::normal_distribution<double> n(0, 1);
    const double b0[3] = {0.30 * D2R, -0.20 * D2R, 0.25 * D2R};
    const double b1[3] = {0.004 * D2R, -0.003 * D2R, 0.006 * D2R};
    auto window = [&](double T) {
        StillnessDetector::Window w;
        for (int i = 0; i < 3; ++i) {
            w.gyro_mean[i] = (float)(b0[i] + b1[i] * (T - 25.0) + 0.002 * D2R * n(rng));
            w.gyro_var[i] = (float)std::pow(0.044 * D2R, 2);
        }
        w.temp_mean = (float)T;
        w.n = 500;
        return w;
    };
    // Single temperature (30 degC): offset at that temperature is learned, slope stays at prior 0.
    for (int k = 0; k < 20; ++k) { b.predict(0.5f); b.update(window(30.0)); }
    float out[3];
    b.bias(30.0f, out);
    for (int i = 0; i < 3; ++i) CHECK_NEAR(out[i] * R2D, (b0[i] + b1[i] * 5.0) * R2D, 0.003);
    CHECK(std::fabs(b.b1(2) * R2D) < 0.003);
    // Warm-up 30 -> 50 degC: slope becomes observable.
    for (int k = 0; k < 400; ++k) { b.predict(0.5f); b.update(window(30.0 + 20.0 * k / 400.0)); }
    for (int i = 0; i < 3; ++i) CHECK_NEAR(b.b1(i) * R2D, b1[i] * R2D, 0.0006);
    b.bias(60.0f, out);  // 10 degC extrapolation beyond anything seen
    for (int i = 0; i < 3; ++i) CHECK_NEAR(out[i] * R2D, (b0[i] + b1[i] * 35.0) * R2D, 0.01);
    CHECK(b.tempSpan() > 19.0f);
    // An outlier window (slow pan sneaking through the detector) is gated out.
    StillnessDetector::Window bad = window(50.0);
    bad.gyro_mean[2] += (float)(0.5 * D2R);
    const uint32_t rej = b.rejected();
    CHECK(b.update(bad) == 2);
    CHECK(b.rejected() == rej + 1);
}

TEST(bias_model_persistence_roundtrip_and_crc) {
    GyroTempBias b;
    StillnessDetector::Window w{{0.01f, -0.02f, 0.003f}, {1e-6f, 1e-6f, 1e-6f}, 33.0f, 500};
    b.update(w);
    GyroTempModel m;
    b.exportModel(m);
    CHECK(m.valid());
    GyroTempBias c;
    CHECK(c.importModel(m, 0.0f));
    float o1[3], o2[3];
    b.bias(40.0f, o1);
    c.bias(40.0f, o2);
    for (int i = 0; i < 3; ++i) CHECK(o1[i] == o2[i]);
    // Import floors the offset sigma at turn-on repeatability (0.1 dps); age only adds to it.
    GyroTempBias d;
    CHECK(d.importModel(m, 3600.0f));
    CHECK(c.b0Sigma(0) >= 0.1f * (float)D2R * 0.999f);
    CHECK(d.b0Sigma(0) >= c.b0Sigma(0));
    GyroTempModel bad = m;
    reinterpret_cast<uint8_t*>(&bad)[10] ^= 0x01;
    CHECK(!bad.valid());
    CHECK(!c.importModel(bad));
    GyroTempModel nan = m;
    nan.b0[1] = NAN;
    nan.seal();
    CHECK(!nan.valid());
}

TEST(imu_mount_validation) {
    constexpr ImuMount id{{1, 2, 3}};
    constexpr ImuMount rotz90{{2, -1, 3}};    // body X = sensor Y, body Y = -sensor X
    constexpr ImuMount mirror{{1, 2, -3}};
    constexpr ImuMount dup{{1, 1, 3}};
    constexpr ImuMount swap_ok{{2, 1, -3}};   // odd permutation x (-1) = proper
    static_assert(id.valid() && rotz90.valid() && swap_ok.valid(), "");
    static_assert(!mirror.valid() && !dup.valid(), "");
    const float s[3] = {1, 2, 3};
    float b[3];
    rotz90.apply(s, b);
    CHECK(b[0] == 2 && b[1] == -1 && b[2] == 3);
}

// ===========================================================================
// Full pipeline on the device model
// ===========================================================================
TEST(pipeline_static_alignment_and_publish) {
    mock::now_us = 1000;
    FakeIcm f;
    f.truth = [](double) { Truth t; gravity_in_body(3 * D2R, -2 * D2R, t.accel); return t; };
    f.gyro_noise_dps = 0.044; f.accel_noise_g = 0.0011;
    ICM42688 imu(f);
    ImuChannel ch(imu, ImuMount{{1, 2, 3}}, default_cfg());
    CHECK(ch.begin(mock::now_us) == ImuStatus::OK);
    run(ch, 2.0);
    AttitudeState s;
    CHECK(ch.latest(s));
    CHECK_NEAR(s.roll * R2D, 3.0, 0.05);
    CHECK_NEAR(s.pitch * R2D, -2.0, 0.05);
    CHECK(s.flags & IMU_FLAG_ALIGNED);
    CHECK(s.flags & IMU_FLAG_BIAS_LEARNED);
    CHECK(s.flags & IMU_FLAG_STILL);
    CHECK(s.samples >= 1990 && s.samples <= 2001);
    CHECK(s.status == static_cast<uint8_t>(ImuStatus::OK));
    CHECK(mock::now_us - s.t_us <= 1000);
}

TEST(pipeline_device_clock_error_is_measured_and_removed) {
    // ICM oscillator 1.5 % fast: 1000 device-us is only 985.2 real us.
    mock::now_us = 1000;
    FakeIcm f;
    f.clock_err = 0.015;
    const double rate = 30.0 * D2R;
    f.truth = [&](double t) { Truth tr; if (t > 6.0 && t <= 9.0) tr.gyro[2] = rate; return tr; };
    ICM42688 imu(f);
    ImuChannel ch(imu, ImuMount{{1, 2, 3}}, default_cfg());
    ch.begin(mock::now_us);
    run(ch, 5.0);
    CHECK_NEAR(ch.clockScale(), 1.0 / 1.015, 5e-4);
    AttitudeState s; ch.latest(s);
    CHECK(s.flags & IMU_FLAG_CLOCK_LOCKED);
    const double yaw0 = ch.ahrs().yaw() * R2D;
    run(ch, 5.0);  // includes the 3 s, 90 deg pan
    CHECK_NEAR(wrap_deg(ch.ahrs().yaw() * R2D - yaw0), 90.0, 0.25);   // uncorrected: 91.35 deg
}

TEST(pipeline_late_and_jittery_scheduling_keeps_dt_exact) {
    mock::now_us = 1000;
    FakeIcm f;
    f.truth = [](double t) { Truth tr; if (t > 1.0 && t <= 3.0) tr.gyro[2] = 45.0 * D2R; return tr; };
    ICM42688 imu(f);
    ImuChannel ch(imu, ImuMount{{1, 2, 3}}, default_cfg());
    ch.begin(mock::now_us);
    const double yaw0 = 0.0;
    // Service every 1..7 ms (a starved task), FIFO absorbs the backlog.
    const uint32_t end = mock::now_us + 4000000;
    uint32_t k = 0;
    while (static_cast<int32_t>(end - mock::now_us) > 0) {
        mock::now_us += 1000 * (1 + (k++ * 7919u) % 7u);
        ch.service(mock::now_us);
    }
    CHECK_NEAR(wrap_deg(ch.ahrs().yaw() * R2D - yaw0), 90.0, 0.1);
    CHECK(imu.overflows() == 0);
}

TEST(pipeline_reinitialises_a_device_that_stopped) {
    mock::now_us = 1000;
    FakeIcm f;
    ICM42688 imu(f);
    ImuChannel ch(imu, ImuMount{{1, 2, 3}}, default_cfg());
    ch.begin(mock::now_us);
    run(ch, 0.5);
    f.softReset();               // brown-out: device back at defaults, sensors off
    run(ch, 0.2);
    AttitudeState s; ch.latest(s);
    CHECK(s.reinits == 1);
    const uint32_t before = s.samples;
    run(ch, 0.2);
    ch.latest(s);
    CHECK(s.samples > before + 150);  // streaming again
}

// Yaw drift with the board warming up: still (anchor), then in-use (tremor,
// never still) while the die heats 32 -> 45 degC. The yaw axis is where the
// accelerometer cannot help; compare the temperature model against a
// constant-bias calibration (slope forced to its prior 0).
static double warmup_yaw_drift(bool temp_model) {
    mock::now_us = 1000;
    FakeIcm f;
    f.gyro_noise_dps = 0.044; f.accel_noise_g = 0.0011;
    f.b0_dps[0] = 0.30; f.b0_dps[1] = -0.20; f.b0_dps[2] = 0.25;
    f.b1_dps_c[0] = 0.004; f.b1_dps_c[1] = -0.003; f.b1_dps_c[2] = 0.006;
    // 0-150 s still, warming 25 -> 32 degC; 150-450 s in use (tremor), 32 -> 45 degC.
    f.truth = [](double t) {
        Truth tr;
        tr.temp = t < 150.0 ? 25.0 + 7.0 * t / 150.0 : 32.0 + 13.0 * std::min(1.0, (t - 150.0) / 300.0);
        if (t >= 150.0) {
            // +/-1.5 dps 5 Hz roll/pitch tremor with consistent gravity, zero net yaw.
            const double w = 2 * M_PI * 5.0, A = 1.5 * D2R;
            tr.gyro[0] = A * std::sin(w * t);
            tr.gyro[1] = A * std::cos(w * t);
            const double roll = -A / w * std::cos(w * t), pitch = A / w * std::sin(w * t);
            gravity_in_body(roll, pitch, tr.accel);
        }
        return tr;
    };
    ImuChannel::Config c = default_cfg();
    if (!temp_model) c.bias.b1_sigma0 = 1e-9f;   // constant-offset calibration only
    ICM42688 imu(f);
    ImuChannel ch(imu, ImuMount{{1, 2, 3}}, c);
    ch.begin(mock::now_us);
    run(ch, 150.0);
    const double yaw_start = ch.ahrs().yaw() * R2D;
    run(ch, 300.0);
    return wrap_deg(ch.ahrs().yaw() * R2D - yaw_start);
}

TEST(pipeline_temperature_model_cancels_warmup_yaw_drift) {
    const double drift_model = warmup_yaw_drift(true);
    const double drift_const = warmup_yaw_drift(false);
    std::printf("  yaw drift over 300 s warm-up in use: temp model %.3f deg, constant bias %.3f deg, raw ~%.0f deg\n",
                drift_model, drift_const, 0.25 * 300 + 0.006 * 300 * 13.5);
    CHECK(std::fabs(drift_model) < 1.0);
    CHECK(std::fabs(drift_const) > 8.0);
}

// ===========================================================================
// Rev 57 review regressions
// ===========================================================================
static StillnessDetector::Window still_window(double bias_rad_s, double T, std::mt19937& rng) {
    std::normal_distribution<double> n(0, 1);
    StillnessDetector::Window w;
    const double sd = 0.044 * D2R / std::sqrt(500.0);
    for (int i = 0; i < 3; ++i) {
        w.gyro_mean[i] = (float)(bias_rad_s + sd * n(rng));
        w.gyro_var[i] = (float)std::pow(0.044 * D2R, 2);
    }
    w.temp_mean = (float)T;
    w.n = 500;
    return w;
}

TEST(review_bias_step_after_convergence_reanchors_within_seconds) {
    // Was: a 0.04 dps step after 300 s of convergence went untracked for 1122 s.
    GyroTempBias b;
    std::mt19937 rng(21);
    for (int k = 0; k < 600; ++k) { b.predict(0.5f); b.update(still_window(0.25 * D2R, 30.0, rng)); }
    const double stepped = 0.29 * D2R;
    int windows = 0;
    float out[3];
    for (; windows < 200; ++windows) {
        b.predict(0.5f);
        b.update(still_window(stepped, 30.0, rng));
        b.bias(30.0f, out);
        if (std::fabs(out[2] - stepped) < 0.005 * D2R) break;
    }
    std::printf("  0.04 dps bias step re-anchored after %d windows (%.1f s), reanchors=%u\n",
                windows + 1, 0.5 * (windows + 1), (unsigned)b.reanchors());
    CHECK(windows < 12);            // ~3 s of re-anchor run + convergence
    CHECK(b.reanchors() >= 1);
    // A single outlier is still rejected without re-anchoring.
    const uint32_t ra = b.reanchors();
    StillnessDetector::Window o = still_window(stepped, 30.0, rng);
    o.gyro_mean[2] += (float)(0.3 * D2R);
    b.update(o);
    for (int k = 0; k < 4; ++k) b.update(still_window(stepped, 30.0, rng));
    CHECK(b.reanchors() == ra);
}

TEST(review_import_then_turn_on_shift_is_tracked_immediately) {
    // Was: importModel(m, 3600 s) + 0.08 dps turn-on shift -> ~50 min lock-out.
    GyroTempBias a;
    std::mt19937 rng(5);
    for (int k = 0; k < 600; ++k) { a.predict(0.5f); a.update(still_window(0.25 * D2R, 30.0, rng)); }
    GyroTempModel m;
    a.exportModel(m);
    GyroTempBias b;
    CHECK(b.importModel(m, 3600.0f));
    CHECK(b.b0Sigma(0) >= 0.1f * (float)D2R * 0.999f);
    for (int k = 0; k < 6; ++k) { b.predict(0.5f); b.update(still_window(0.33 * D2R, 30.0, rng)); }
    float out[3];
    b.bias(30.0f, out);
    CHECK_NEAR(out[2] * R2D, 0.33, 0.005);
    CHECK(b.rejected() == 0);
}

TEST(review_fifo_byte_slip_with_negative_accel_resyncs_without_reinit) {
    // Was: header byte >= 0x80 after a 1-byte slip was taken as "FIFO empty" on
    // every burst -> 50 ms of silence + full re-init stalling both IMUs.
    mock::now_us = 1000;
    FakeIcm f;
    f.truth = [](double) { Truth t; t.accel[0] = -0.1; t.accel[2] = 0.995; return t; };
    ICM42688 imu(f);
    ImuChannel ch(imu, ImuMount{{1, 2, 3}}, default_cfg());
    ch.begin(mock::now_us);
    run(ch, 0.2);
    mock::now_us += 3000;
    f.advance();
    f.fifo.pop_front();                       // lose one byte: stream now starts at accel-X high (0xFC..)
    run(ch, 0.01);
    AttitudeState s; ch.latest(s);
    CHECK(imu.headerErrors() == 1);
    CHECK(s.reinits == 0);
    CHECK(mock::now_us - s.t_us <= 2000);     // streaming again within a couple of ms
}

TEST(review_clock_ratio_unbiased_by_fifo_backlog_after_stalls) {
    // Was: window opened/closed with a 64+ ms backlog -> clock_scale off by ~2 %.
    mock::now_us = 1000;
    FakeIcm f;
    ICM42688 imu(f);
    ImuChannel ch(imu, ImuMount{{1, 2, 3}}, default_cfg());
    ch.begin(mock::now_us);
    for (int cycle = 0; cycle < 12; ++cycle) {
        run(ch, 2.5);   // windows (2 s) must be able to close across the backlogged stalls
        mock::now_us += (cycle % 2) ? 80000 : 130000;   // stalls: 80 ms backlog, 130 ms overflow
        for (int k = 0; k < 8; ++k) { mock::now_us += 1000; ch.service(mock::now_us); }
    }
    AttitudeState s; ch.latest(s);
    CHECK(s.flags & IMU_FLAG_CLOCK_LOCKED);
    CHECK_NEAR(ch.clockScale(), 1.0, 1e-3);
}

TEST(review_rotated_mount_and_learning_inhibit) {
    // Sensor rotated 90 deg about Z on the PCB: body X = sensor Y, body Y = -sensor X.
    // A body-frame roll must appear as roll even though the sensor sees it on Y.
    mock::now_us = 1000;
    FakeIcm f;
    const double r = 60.0 * D2R;
    f.truth = [&](double t) {
        Truth tr;   // in SENSOR axes: sensor Y = body X, sensor X = -body Y
        const double roll = (t > 1.0) ? std::min(1.0, t - 1.0) * r : 0.0;
        if (t > 1.0 && t <= 2.0) tr.gyro[1] = r;
        double gb[3]; gravity_in_body(roll, 0, gb);
        tr.accel[0] = -gb[1]; tr.accel[1] = gb[0]; tr.accel[2] = gb[2];
        return tr;
    };
    f.b0_dps[0] = 0.2;   // sensor-X bias: must be removed in SENSOR frame (lands on body Y)
    ICM42688 imu(f);
    ImuChannel ch(imu, ImuMount{{2, -1, 3}}, default_cfg());
    ch.begin(mock::now_us);
    run(ch, 3.0);
    CHECK_NEAR(ch.ahrs().roll() * R2D, 60.0, 0.3);
    CHECK_NEAR(ch.ahrs().pitch() * R2D, 0.0, 0.3);
    CHECK_NEAR(ch.bias().b0(0) * R2D, 0.2, 0.01);
    CHECK(std::fabs(ch.bias().b0(1)) < 0.01 * D2R);
    // Learning inhibit: a commanded slow motorised yaw pan (0.5 dps, gravity not
    // rotating -> passes the stillness test) must not be learned as bias.
    const uint32_t upd = ch.bias().updates();
    ch.setLearningInhibit(true);
    f.truth = [](double) { Truth tr; tr.gyro[2] = 0.5 * D2R; return tr; };
    run(ch, 5.0);
    CHECK(ch.bias().updates() == upd);
    CHECK(std::fabs(ch.bias().b0(2)) < 0.01 * D2R);
}

int main() {
    for (auto& t : tests()) {
        const int f0 = g_fail;
        t.f();
        std::printf("%s %s\n", g_fail == f0 ? "PASS" : "FAIL", t.n);
    }
    std::printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
