// Rev 8.0 host unit tests: 512-point anti-cogging LUT, electrical-angle
// normalisation, harmonic synthesis/extraction, calibration fit and the
// Vq feed-forward. Build & run: firmware/test/host/run_host_tests.sh
#include <cstdio>
#include <cmath>
#include <random>
#include "gimbal_config.h"
#include "anti_cogging.h"

using namespace cogging;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((double)(a) - (double)(b)) <= (double)(tol))

static const double kPi = 3.14159265358979323846;

// Angle difference wrapped to (-pi, pi]
static double adiff(double a, double b) { double d = std::fmod(a - b, 2 * kPi); if (d > kPi) d -= 2 * kPi; if (d <= -kPi) d += 2 * kPi; return d; }

static void test_fundamental_order() {
    CHECK(fundamental_order(12, 7) == 12);     // 12N14P: 84 cogging cycles / mech rev, 12 / elec rev
    CHECK(lcm(12, 14) == 84);
    CHECK(fundamental_order(9, 6) == 6);       // 9N12P: lcm(9,12)=36 / 6pp
    CHECK(fundamental_order(12, 4) == 6);      // 12N8P: lcm(12,8)=24 / 4pp
    CHECK(fundamental_order(24, 11) == 24);    // 24N22P: lcm(24,22)=264 / 11pp
    CHECK(fundamental_order(MOTOR_STATOR_SLOTS, MOTOR_POLE_PAIRS) == 12);
    CHECK(MOTOR_POLE_PAIRS == 7);              // regression: Rev <= 7.0 used 14 for a 12N14P motor
}

static void test_wrap_and_electrical_angle() {
    const float tp = TWO_PI_F;
    NEAR(wrap_2pi(0.0f), 0.0f, 0);
    NEAR(wrap_2pi(tp), 0.0f, 1e-6);
    NEAR(wrap_2pi(-0.5f), tp - 0.5f, 1e-6);
    NEAR(wrap_2pi(7.0f * tp + 1.0f), 1.0f, 1e-4);
    NEAR(wrap_2pi(-123.4f), (float)std::fmod(-123.4 + 20 * 2 * kPi, 2 * kPi), 1e-4);
    const float r = wrap_2pi(-1e-8f);  // must not round up to exactly 2pi
    CHECK(r >= 0.0f && r < tp);
    CHECK(std::isnan(wrap_2pi(NAN)));
    CHECK(std::isnan(wrap_2pi(INFINITY)));
    for (int i = -2000; i <= 2000; ++i) { const float w = wrap_2pi(i * 0.0137f); CHECK(w >= 0.0f && w < tp); }

    // Matches SimpleFOC FOCMotor::electricalAngle() = _normalizeAngle(dir*pp*mech - zero)
    const int pp = MOTOR_POLE_PAIRS;
    for (int dir : {1, -1}) for (float zero : {0.0f, 1.234f, 5.9f}) for (int i = 0; i < 200; ++i) {
        const float m = i * (6.2831853f / 200.0f);
        double ref = std::fmod((double)dir * pp * m - zero, 2 * kPi); if (ref < 0) ref += 2 * kPi;
        NEAR(adiff(electrical_angle(m, pp, dir, zero), ref), 0.0, 2e-5);
    }
    // Multi-turn mechanical input gives the same electrical angle as single-turn
    NEAR(adiff(electrical_angle(0.3f + 5 * TWO_PI_F, pp, 1, 0.7f), electrical_angle(0.3f, pp, 1, 0.7f)), 0.0, 5e-5);
    // One mechanical rev = pp electrical revs; 1/pp mech rev = exactly one elec rev
    NEAR(adiff(electrical_angle(0.1f + TWO_PI_F / pp, pp, 1, 0.0f), electrical_angle(0.1f, pp, 1, 0.0f)), 0.0, 2e-5);
    // Direction flips the sign
    NEAR(adiff(electrical_angle(0.2f, pp, -1, 0.0f), -(double)electrical_angle(0.2f, pp, 1, 0.0f)), 0.0, 2e-5);
    // Invalid direction / input -> NaN (feed-forward suppressed)
    CHECK(std::isnan(electrical_angle(0.2f, pp, 0, 0.0f)));
    CHECK(std::isnan(electrical_angle(NAN, pp, 1, 0.0f)));
    CHECK(std::isnan(electrical_angle(0.2f, 0, 1, 0.0f)));
}

static void test_lut_synthesis_and_interpolation() {
    HarmonicSet hs;
    CHECK(hs.add(12, 3.0e-4f, 0.4f));
    CHECK(hs.add(24, 1.0e-4f, -1.1f));
    Lut lut;
    CHECK(!lut.valid());
    NEAR(lut.sample(1.0f), 0.0f, 0);         // unbuilt table outputs nothing
    CHECK(lut.build(hs));
    CHECK(lut.valid());
    auto model = [](double th) { return 3.0e-4 * std::sin(12 * th + 0.4) + 1.0e-4 * std::sin(24 * th - 1.1); };

    // Grid points exact; between-grid linear interpolation error bound:
    // max|f''| h^2 / 8, f'' <= 3e-4*144 + 1e-4*576, h = 2pi/512
    const double h = 2 * kPi / LUT_SIZE;
    const double bound = (3e-4 * 144 + 1e-4 * 576) * h * h / 8 + 1e-9;
    double worst = 0, peak = 0;
    for (int i = 0; i < LUT_SIZE; ++i) NEAR(lut.point(i), model(i * h), 1e-9);
    for (int i = 0; i < 20000; ++i) {
        const double th = i * (2 * kPi / 20000);
        worst = std::fmax(worst, std::fabs(lut.sample((float)th) - model(th)));
        peak = std::fmax(peak, std::fabs(model(th)));
    }
    std::printf("  LUT interp worst error %.3e N*m (bound %.3e, %.2f%% of peak)\n", worst, bound, 100 * worst / peak);
    CHECK(worst <= bound);
    NEAR(lut.peak_nm(), peak, peak * 0.01);
    // Periodicity / wrap-around at the 511 -> 0 seam and for any angle range
    NEAR(lut.sample(TWO_PI_F - 1e-4f), model(-1e-4), bound);
    NEAR(lut.sample(-0.37f), lut.sample(TWO_PI_F - 0.37f), 1e-8);
    NEAR(lut.sample(0.37f + 9 * TWO_PI_F), lut.sample(0.37f), 2e-7);
    NEAR(lut.sample(NAN), 0.0f, 0);

    // Aliasing / invalid harmonic sets are rejected and keep the old table
    HarmonicSet bad; CHECK(!bad.add(LUT_SIZE / 2, 1.0f, 0.0f));   // Nyquist order rejected, not truncated
    CHECK(!bad.add(268, 1.0f, 0.0f));                              // was silently accepted as order 12
    CHECK(bad.count == 0);
    CHECK(!lut.build(bad));                                        // empty set keeps the previous table
    CHECK(lut.valid());
    NEAR(lut.point(5), model(5 * h), 1e-9);
    HarmonicSet bad2; CHECK(!bad2.add(12, NAN, 0.0f)); CHECK(!bad2.add(0, 1.0f, 0.0f));
    HarmonicSet full; for (int k = 0; k < MAX_HARMONICS; ++k) CHECK(full.add(12, 1e-5f, 0.0f));
    CHECK(!full.add(12, 1e-5f, 0.0f));
}

static void test_harmonic_extraction_roundtrip() {
    static float table[LUT_SIZE];
    for (int i = 0; i < LUT_SIZE; ++i) {
        const double th = 2 * kPi * i / LUT_SIZE;
        table[i] = (float)(2.5e-4 * std::sin(12 * th + 2.0) + 0.8e-4 * std::sin(24 * th - 0.3)
                           + 5e-4 * std::sin(th) + 1e-3);  // non-cogging 1st order + DC must not leak
    }
    const Harmonic a = extract_harmonic(table, LUT_SIZE, 12);
    const Harmonic b = extract_harmonic(table, LUT_SIZE, 24);
    const Harmonic c = extract_harmonic(table, LUT_SIZE, 36);
    NEAR(a.amp_nm, 2.5e-4, 1e-8); NEAR(adiff(a.phase_rad, 2.0), 0, 1e-4);
    NEAR(b.amp_nm, 0.8e-4, 1e-8); NEAR(adiff(b.phase_rad, -0.3), 0, 1e-4);
    NEAR(c.amp_nm, 0.0, 1e-8);
}

// Closed-loop-ish calibration model: during a slow sweep the position loop
// produces vq = -tau_cog*R/Kt + friction*sign(v) + gravity(theta_m) + noise.
static void test_calibration_recovers_harmonics() {
    const int   pp = MOTOR_POLE_PAIRS, k0 = fundamental_order(MOTOR_STATOR_SLOTS, pp);
    const float kt = MOTOR_KT, R = MOTOR_PHASE_R;
    const double A1 = 3.6e-4, P1 = -0.8, A2 = 1.3e-4, P2 = 2.2, A3 = 0.6e-4, P3 = 1.0;  // 12th/24th + a 6th (non-cogging)
    auto tau_cog = [&](double te) { return A1 * std::sin(12 * te + P1) + A2 * std::sin(24 * te + P2); };
    std::mt19937 rng(8);
    std::normal_distribution<double> noise(0.0, 0.02);   // 20 mV Vq noise (>> 0.03 V cogging @ h24)

    Calibrator cal;
    cal.reset();
    const double span = 2.0 * 2 * kPi / pp, rate = 0.2, dt = 5e-4, zero = 2.71;
    const int steps = (int)(span / (rate * dt));
    double m = 1.0;  // start pose (mech rad)
    for (int pass = 0; pass < 4; ++pass) {
        const double dir = (pass == 0 || pass == 3) ? 1 : -1;
        for (int s = 0; s < steps; ++s) {
            m += dir * rate * dt;
            const float te = electrical_angle((float)m, pp, 1, (float)zero);
            const double vq = -tau_cog(te) * R / kt
                              + 0.15 * dir                      // Coulomb friction (cancels fwd/rev)
                              + 0.30 * std::sin(m)              // gravity/imbalance in mechanical angle
                              + A3 * R / kt * std::sin(6 * te + P3)   // eccentricity-like non-slot harmonic
                              + noise(rng);
            cal.accumulate(te, (float)vq);
        }
    }
    std::printf("  calibration coverage %.1f%%, samples/bin ~%u\n", 100 * cal.coverage(), cal.count(100));
    CHECK(cal.coverage() > 0.999f);
    HarmonicSet hs;
    CHECK(cal.fit(k0, ANTI_COGGING_ORDER, kt, R, hs));
    CHECK(hs.count == 2);
    CHECK(hs.h[0].order == 12 && hs.h[1].order == 24);
    std::printf("  fitted h12 %.4f mNm @ %.3f rad (true %.4f @ %.3f), h24 %.4f mNm @ %.3f (true %.4f @ %.3f)\n",
                1e3 * hs.h[0].amp_nm, hs.h[0].phase_rad, 1e3 * A1, P1, 1e3 * hs.h[1].amp_nm, hs.h[1].phase_rad, 1e3 * A2, P2);
    NEAR(hs.h[0].amp_nm, A1, 0.03 * A1); NEAR(adiff(hs.h[0].phase_rad, P1), 0, 0.03);
    NEAR(hs.h[1].amp_nm, A2, 0.06 * A2); NEAR(adiff(hs.h[1].phase_rad, P2), 0, 0.08);

    // Rebuilt LUT + feed-forward cancels >= 90% of the cogging torque RMS at standstill
    Lut lut; CHECK(lut.build(hs));
    FeedforwardParams p; p.kt = kt; p.phase_r = R;
    double e0 = 0, e1 = 0;
    for (int i = 0; i < 4096; ++i) {
        const double te = 2 * kPi * i / 4096;
        const double tau_ff = feedforward_vq(lut, p, (float)te, 0.0f, 0.0f) * kt / R;
        e0 += tau_cog(te) * tau_cog(te);
        e1 += (tau_cog(te) + tau_ff) * (tau_cog(te) + tau_ff);
    }
    const double red = 1 - std::sqrt(e1 / e0);
    std::printf("  residual cogging after feed-forward: %.2f%% (reduction %.1f%%)\n", 100 * std::sqrt(e1 / e0), 100 * red);
    CHECK(red > 0.90);

    // Insufficient coverage is rejected (a short sweep would extrapolate garbage)
    Calibrator thin; thin.reset();
    for (int i = 0; i < 100; ++i) thin.accumulate(i * 0.01f, 0.1f);
    HarmonicSet hs2;
    CHECK(!thin.fit(k0, ANTI_COGGING_ORDER, kt, R, hs2));
    CHECK(hs2.count == 0);
    CHECK(!cal.fit(k0, LUT_SIZE / 4, kt, R, hs2));   // order beyond Nyquist/2 guard
    CHECK(!cal.fit(k0, ANTI_COGGING_ORDER, 0.0f, R, hs2));
    // NaN samples are ignored
    Calibrator c3; c3.reset(); c3.accumulate(NAN, 1.0f); c3.accumulate(1.0f, NAN);
    NEAR(c3.coverage(), 0.0f, 0);
}

static void test_calibration_gap_fill() {
    // 95% coverage with contiguous gaps: interpolated fill keeps the fit accurate.
    Calibrator cal; cal.reset();
    const double A = 3e-4, P = 0.5, kt = MOTOR_KT, R = MOTOR_PHASE_R;
    for (int b = 0; b < LUT_SIZE; ++b) {
        if (b % 40 < 2) continue;                                  // 2-bin gap every 40 bins
        const double te = 2 * kPi * b / LUT_SIZE;
        cal.accumulate((float)te, (float)(-A * std::sin(12 * te + P) * R / kt));
    }
    CHECK(cal.coverage() >= 0.90f && cal.coverage() < 0.96f);
    HarmonicSet hs;
    CHECK(cal.fit(12, 24, (float)kt, (float)R, hs));
    NEAR(hs.h[0].amp_nm, A, 0.02 * A);
    NEAR(adiff(hs.h[0].phase_rad, P), 0, 0.02);
}

static void test_feedforward() {
    HarmonicSet hs; hs.add(12, 4.0e-4f, 0.0f);
    Lut lut; lut.build(hs);
    FeedforwardParams p; p.kt = 0.035f; p.phase_r = 8.5f; p.vq_limit = 0.5f; p.fade_start = 20; p.fade_end = 120;
    const float th = (float)(kPi / 24);                       // sin(12*th) = 1 -> peak cogging
    const float vpk = 4.0e-4f * 8.5f / 0.035f;                // 0.0971 V
    // th sits between LUT points (index 10.67): linear interpolation of the 12th
    // harmonic undershoots the peak by 0.24 %. Tolerance = the 0.5 % interpolation
    // bound proven in test_lut_synthesis_and_interpolation (was 1e-4 V = 0.1 %,
    // which hid behind a compile error until Rev 57).
    const float tol = 0.005f * vpk;
    NEAR(feedforward_vq(lut, p, th, 0, 0), -vpk, tol);       // opposes cogging
    NEAR(feedforward_vq(lut, p, th + (float)kPi / 12, 0, 0), vpk, tol);  // half a cogging period later
    // Phase advance: evaluating at theta - w*T with latency T equals evaluating at theta
    NEAR(feedforward_vq(lut, p, th - 10.0f * 1e-3f, 10.0f, 1e-3f), feedforward_vq(lut, p, th, 0, 0), 2e-4);
    // Speed fade: full below start, half in the middle, zero above end, symmetric in sign
    NEAR(feedforward_vq(lut, p, th, 70.0f, 0), -vpk * 0.5f, tol);
    NEAR(feedforward_vq(lut, p, th, -70.0f, 0), -vpk * 0.5f, tol);
    NEAR(feedforward_vq(lut, p, th, 19.0f, 0), -vpk, tol);
    NEAR(feedforward_vq(lut, p, th, 120.0f, 0), 0.0f, 0);
    NEAR(feedforward_vq(lut, p, th, 500.0f, 0), 0.0f, 0);
    // Clamp
    p.vq_limit = 0.05f;
    NEAR(feedforward_vq(lut, p, th, 0, 0), -0.05f, 1e-7);
    // Faults: NaN angle/speed, bad latency, unbuilt LUT -> 0 or no advance
    NEAR(feedforward_vq(lut, p, NAN, 0, 0), 0.0f, 0);
    NEAR(feedforward_vq(lut, p, th, NAN, 0), 0.0f, 0);
    NEAR(feedforward_vq(lut, p, th, 0, NAN), -0.05f, 1e-7);
    Lut empty;
    NEAR(feedforward_vq(empty, p, th, 0, 0), 0.0f, 0);
}

static void test_firmware_api_seed() {
    init_anti_cogging();
    for (uint8_t a = 0; a < 3; ++a) {
        const Lut* l = anti_cogging_lut(a);
        CHECK(l && l->valid());
        CHECK(l->harmonics().count == 2);
        CHECK(l->harmonics().h[0].order == 12 && l->harmonics().h[1].order == 24);
    }
    CHECK(anti_cogging_lut(3) == nullptr);
    NEAR(get_anti_cogging_voltage(3, 1.0f, 0, 0), 0.0f, 0);
    // Seed preserves the Rev 7.0 Vq amplitudes (pan: 0.08 V @ k0, 0.03 V @ 2k0), now as a
    // -cogging correction: Vq_ff(theta) = +0.08 sin(12 th) + 0.03 sin(24 th)
    for (int i = 0; i < 64; ++i) {
        const double th = 2 * kPi * i / 64 + 0.01;
        NEAR(get_anti_cogging_voltage(0, (float)th, 0, 0), 0.08 * std::sin(12 * th) + 0.03 * std::sin(24 * th), 2e-3);
    }
    // Pan peak is < the 0.5 V clamp, so the seed is not saturating
    CHECK(anti_cogging_lut(0)->peak_nm() * MOTOR_PHASE_R / MOTOR_KT < ANTI_COGGING_VQ_LIMIT);
    // Loading a calibrated set; orders above ANTI_COGGING_ORDER are refused
    HarmonicSet ok; ok.add(12, 1e-4f, 0.0f);
    CHECK(anti_cogging_load(1, ok));
    CHECK(anti_cogging_lut(1)->harmonics().count == 1);
    HarmonicSet hi; hi.add(36, 1e-4f, 0.0f);
    CHECK(!anti_cogging_load(1, hi));
    CHECK(anti_cogging_lut(1)->harmonics().count == 1);
    CHECK(!anti_cogging_load(7, ok));
}

static void test_rev7_mechanical_index_bug_is_gone() {
    // Rev 7.0 indexed the LUT with the MECHANICAL angle, giving 6 cogging cycles
    // per mechanical rev instead of 84. With electrical indexing, stepping the rotor by one cogging
    // pitch (2pi/84 mech) must reproduce the same feed-forward.
    init_anti_cogging();
    const int pp = MOTOR_POLE_PAIRS;
    const float pitch = TWO_PI_F / 84.0f;
    for (int i = 0; i < 50; ++i) {
        const float m = 0.05f + i * 0.021f;
        const float a = get_anti_cogging_voltage(0, electrical_angle(m, pp, 1, 0.9f), 0, 0);
        const float b = get_anti_cogging_voltage(0, electrical_angle(m + pitch, pp, 1, 0.9f), 0, 0);
        NEAR(a, b, 1e-3);
    }
}

int main() {
    std::printf("== anti-cogging tests\n");
    test_fundamental_order();
    test_wrap_and_electrical_angle();
    test_lut_synthesis_and_interpolation();
    test_harmonic_extraction_roundtrip();
    test_calibration_recovers_harmonics();
    test_calibration_gap_fill();
    test_feedforward();
    test_firmware_api_seed();
    test_rev7_mechanical_index_bug_is_gone();
    std::printf("anti-cogging: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
