// Rev 59.0 host tests: joystick exponential curves, dynamic radial deadband,
// release guard, centre tracking, fault handling and the ease/accel-limited
// rate smoother (firmware/src/joystick_shaper.cpp), driven by a stick model
// with ADC noise, spring-return ringing and thermal centre drift.
// Build & run: firmware/test/host/run_host_tests.sh
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>
#include "joystick_shaper.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { const double _a = (a), _b = (b); if (std::fabs(_a - _b) <= (tol)) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL %s:%d  %s = %.6g, expected %.6g +/- %.3g\n", __FILE__, __LINE__, #a, _a, _b, (double)(tol)); } } while (0)
#define TEST(name) static void name(); struct name##_reg { name##_reg() { tests().push_back({#name, name}); } } name##_inst; static void name()
struct T { const char* n; void (*f)(); };
static std::vector<T>& tests() { static std::vector<T> v; return v; }

using namespace joy;
static constexpr float DT = 0.005f;          // 200 Hz, as JOY_PERIOD_MS
static constexpr double R2D = 57.29577951308232;

// ---------------------------------------------------------------------------
// Stick + ADC model
// ---------------------------------------------------------------------------
struct Stick {
    double center[2] = {1650.0, 1650.0};      // true electrical centre (mV)
    double span = 1500.0;                      // mV per unit deflection (real pot ~1550 to the rail)
    double noise_mv = 4.0;                     // ADC + pot noise, 1 sigma, after oversampling
    std::mt19937 rng{12345};
    std::normal_distribution<double> n{0.0, 1.0};
    double defl[2] = {0.0, 0.0};               // mechanical deflection, [-1, 1]
    float read(int a) {
        const double v = center[a] + defl[a] * span + noise_mv * n(rng);
        return (float)std::min(3300.0, std::max(0.0, v));
    }
};

static Shaper make_shaper(const Config& cfg, float cx = 1650.0f, float cy = 1650.0f) {
    AxisCal cal[AXES] = {{cx, cfg.span_default_mv, cfg.span_default_mv},
                         {cy, cfg.span_default_mv, cfg.span_default_mv}};
    Shaper s;
    s.configure(cfg, cal, false);
    return s;
}

struct Run {
    double integ[2] = {0, 0};          // integrated angle (rad), as main.cpp does
    double peak_acc[2] = {0, 0};       // |d rate / dt| max
    double max_rate[2] = {-1e9, -1e9}, min_rate[2] = {1e9, 1e9};
    int nonzero_ticks = 0, ticks = 0;
};

// Run @p n ticks; @p drive(tick) sets the stick state before each read.
static Run run(Shaper& s, Stick& st, int n, const std::function<void(int)>& drive, bool precision = false) {
    Run r;
    float prev[2] = {s.last().rate[0], s.last().rate[1]};
    for (int i = 0; i < n; ++i) {
        drive(i);
        const Command& c = s.update(st.read(0), st.read(1), DT, precision);
        for (int a = 0; a < 2; ++a) {
            r.integ[a] += (double)c.rate[a] * DT;
            r.peak_acc[a] = std::max(r.peak_acc[a], std::fabs((double)(c.rate[a] - prev[a])) / DT);
            r.max_rate[a] = std::max(r.max_rate[a], (double)c.rate[a]);
            r.min_rate[a] = std::min(r.min_rate[a], (double)c.rate[a]);
            prev[a] = c.rate[a];
        }
        if (c.rate[0] != 0.0f || c.rate[1] != 0.0f) ++r.nonzero_ticks;
        ++r.ticks;
    }
    return r;
}

// ===========================================================================
// Pure curve / deadband functions
// ===========================================================================
TEST(expo_curve_endpoints_symmetry_monotone) {
    for (float k : {-2.0f, 0.0f, 0.5f, 2.5f, 3.0f, 6.0f}) {
        CHECK(expo_curve(0.0f, k) == 0.0f);
        CHECK_NEAR(expo_curve(1.0f, k), 1.0, 1e-6);
        CHECK_NEAR(expo_curve(-1.0f, k), -1.0, 1e-6);
        CHECK_NEAR(expo_curve(1.7f, k), 1.0, 1e-6);  // clamped beyond full deflection
        bool mono = true, odd = true;
        float prev = -2.0f;
        for (int i = -1000; i <= 1000; ++i) {
            const float x = (float)i / 1000.0f;
            const float y = expo_curve(x, k);
            mono = mono && y >= prev;
            odd = odd && std::fabs(y + expo_curve(-x, k)) < 1e-6f;
            prev = y;
        }
        CHECK(mono);
        CHECK(odd);
        // centre gain = numerical derivative at 0
        CHECK_NEAR(expo_curve(1e-3f, k) / 1e-3f, expo_center_gain(k), 2e-3 * std::max(1.0f, std::fabs(k)));
    }
    CHECK_NEAR(expo_center_gain(3.0f), 3.0 / (std::exp(3.0) - 1.0), 1e-6);   // 0.157: 6.4x finer at centre
    CHECK(expo_curve(0.5f, 3.0f) < 0.5f * 0.5f);   // k=3 is softer than a square law at half stick
    CHECK(expo_curve(0.5f, -2.0f) > 0.5f);         // negative k: sharper centre
    CHECK_NEAR(expo_curve(0.37f, 0.0f), 0.37, 1e-7);
    CHECK(expo_curve(NAN, 3.0f) == 0.0f);
}

TEST(radial_deadband_continuous_no_cross_full_scale) {
    float out[2];
    const float r = 0.08f;
    const float in0[2] = {0.05f, 0.05f};  // |u| = 0.0707 < r
    radial_deadband(in0, r, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f);
    // continuity at the edge: output ~ 0 just outside the circle
    const float in1[2] = {r + 1e-4f, 0.0f};
    radial_deadband(in1, r, out);
    CHECK(out[0] > 0.0f && out[0] < 2e-4f);
    // axis full scale preserved
    const float in2[2] = {-1.0f, 0.0f};
    radial_deadband(in2, r, out);
    CHECK_NEAR(out[0], -1.0, 1e-6);
    CHECK(out[1] == 0.0f);
    // diagonal corner reaches full rate on both axes
    const float in3[2] = {1.0f, 1.0f};
    radial_deadband(in3, r, out);
    CHECK_NEAR(out[0], 1.0, 1e-6);
    CHECK_NEAR(out[1], 1.0, 1e-6);
    // no "cross": a shallow diagonal just outside the radius moves BOTH axes,
    // with the input direction preserved (per-axis deadband would give (x, 0)).
    const float in4[2] = {0.09f, 0.03f};
    radial_deadband(in4, r, out);
    CHECK(out[0] > 0.0f && out[1] > 0.0f);
    CHECK_NEAR(out[1] / out[0], 0.03 / 0.09, 1e-5);
    // NaN input never escapes
    const float in5[2] = {NAN, 0.5f};
    radial_deadband(in5, r, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f);
    // Lipschitz on a sweep across the edge (no kick when leaving the deadband)
    float prev = 0.0f;
    double max_step = 0.0;
    for (int i = 0; i <= 2000; ++i) {
        const float in[2] = {(float)i * 1e-4f, 0.0f};
        radial_deadband(in, r, out);
        max_step = std::max(max_step, (double)std::fabs(out[0] - prev));
        prev = out[0];
    }
    CHECK(max_step <= 1e-4 / (1.0 - r) + 1e-7);
}

TEST(soft_limit_rate_decelerates_into_limit_only) {
    const float L = 1.0f, Z = 0.25f;
    CHECK(soft_limit_rate(0.5f, 0.0f, L, Z) == 0.5f);      // far from the limit
    CHECK_NEAR(soft_limit_rate(0.5f, 0.8f, L, Z), 0.4, 1e-6);  // in the zone, toward the limit
    CHECK(soft_limit_rate(-0.5f, 0.8f, L, Z) == -0.5f);    // moving away: untouched
    CHECK(soft_limit_rate(0.5f, 1.2f, L, Z) == 0.0f);      // beyond: no further travel
    CHECK(soft_limit_rate(-0.5f, -0.9f, L, Z) < -0.19f && soft_limit_rate(-0.5f, -0.9f, L, Z) > -0.21f);
    CHECK(soft_limit_rate(0.7f, 50.0f, 0.0f, Z) == 0.7f);  // limit disabled
    // Integrated: a held full-rate command converges to the limit, never past it.
    double pos = 0.0;
    for (int i = 0; i < 20000; ++i) pos += (double)soft_limit_rate(1.57f, (float)pos, L, Z) * 0.0005;
    CHECK(pos <= 1.0 && pos > 0.999);
}

TEST(center_calibrator_accept_and_reject) {
    Config cfg;
    CenterCalibrator::Limits lim;
    AxisCal cal[2];
    std::mt19937 rng(7);
    std::normal_distribution<float> n(0.0f, 4.0f);
    CenterCalibrator c;
    for (int i = 0; i < 100; ++i) c.add(1712.0f + n(rng), 1601.0f + n(rng));
    CHECK(c.result(cfg, lim, cal));
    CHECK_NEAR(cal[0].center_mv, 1712.0, 2.0);
    CHECK_NEAR(cal[1].center_mv, 1601.0, 2.0);
    CHECK(cal[0].span_pos_mv == cfg.span_default_mv);

    c.reset();  // thumb holding the stick deflected at boot
    for (int i = 0; i < 100; ++i) c.add(2400.0f + n(rng), 1650.0f + n(rng));
    CHECK(!c.result(cfg, lim, cal));
    CHECK(cal[0].center_mv == lim.nominal_mv && cal[1].center_mv == lim.nominal_mv);

    c.reset();  // thumb wobbling on the stick: mean fine, spread too large
    std::normal_distribution<float> wob(0.0f, 60.0f);
    for (int i = 0; i < 100; ++i) c.add(1650.0f + wob(rng), 1650.0f + n(rng));
    CHECK(!c.result(cfg, lim, cal));

    c.reset();  // too few samples
    for (int i = 0; i < 10; ++i) c.add(1650.0f, 1650.0f);
    CHECK(!c.result(cfg, lim, cal));
}

// ===========================================================================
// Shaper behaviour
// ===========================================================================
TEST(rest_with_noise_gives_exact_zero_and_no_target_creep) {
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;  // 4 mV noise
    Run r = run(s, st, 200 * 120, [](int) {});   // 2 minutes untouched
    CHECK(r.nonzero_ticks == 0);
    CHECK(r.integ[0] == 0.0 && r.integ[1] == 0.0);
    CHECK(s.last().flags & FLAG_IN_DEADBAND);
    CHECK(s.last().flags & FLAG_SETTLED);
    // noise estimate tracks the truth (4 mV / 1400 mV span)
    CHECK_NEAR(s.last().sigma[0], 4.0 / 1400.0, 0.25 * 4.0 / 1400.0);
    // a clean stick keeps a tight deadband (not the fixed worst case)
    CHECK(s.last().radius < 0.05f);
}

TEST(noisy_offset_stick_dynamic_deadband_and_tracking_stop_drift) {
    // Worn pot + PWM coupling: 25 mV sigma (0.018 normalised) AND a +30 mV
    // centre shift after the boot calibration (motor drivers warming the
    // board), linear curve (worst case: no expo attenuation near centre).
    Config cfg;
    cfg.expo_k[0] = cfg.expo_k[1] = 0.0f;
    Stick st;
    st.noise_mv = 25.0;
    st.center[0] += 30.0;
    Shaper s = make_shaper(cfg);
    Run r = run(s, st, 200 * 60, [](int) {});
    CHECK(s.last().radius >= 5.0f * 0.9f * (25.0f / 1400.0f));
    CHECK_NEAR(s.last().center_mv[0], 1680.0, 8.0);               // offset tracked out
    Run r2 = run(s, st, 200 * 60, [](int) {});                     // steady state, next minute
    CHECK(r2.nonzero_ticks == 0);
    CHECK(std::fabs(r.integ[0]) * R2D < 0.01);

    // The same stick through a FIXED minimum deadband (noise term disabled)
    // keeps leaving the deadband: the "breathing" gimbal + slow drift.
    Config fixed = cfg;
    fixed.db_noise_k = 0.0f;
    fixed.db_base = fixed.db_min;
    fixed.release_radius = 0.0f;
    Shaper f = make_shaper(fixed);
    Stick st2;
    st2.noise_mv = 25.0;
    st2.center[0] += 30.0;
    Run rf = run(f, st2, 200 * 60, [](int) {});
    Run rf2 = run(f, st2, 200 * 60, [](int) {});
    CHECK(rf2.nonzero_ticks > 20);
    CHECK(std::fabs(rf.integ[0] + rf2.integ[0]) > 3.0 * std::fabs(r.integ[0] + r2.integ[0]));
}

TEST(noise_estimator_ignores_slow_deliberate_motion) {
    // A slow push toward the deadband edge (inside the estimator's gate) must
    // not be mistaken for noise: the 2nd difference cancels the ramp.
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;
    run(s, st, 400, [](int) {});
    const float sig0 = s.last().sigma[0];
    run(s, st, 200, [&](int i) { st.defl[0] = 0.15 * i / 200.0; });   // 0 -> 0.15 in 1 s
    CHECK(s.last().sigma[0] < 1.3f * sig0);
}

TEST(full_deflection_reaches_max_rate_with_bounded_accel_no_overshoot) {
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;
    st.noise_mv = 0.0;
    run(s, st, 100, [](int) {});
    Run r = run(s, st, 400, [&](int) { st.defl[0] = 1.0; st.defl[1] = -1.0; });   // step, 2 s
    CHECK_NEAR(s.last().rate[0], cfg.max_rate[0], 1e-4);
    CHECK_NEAR(s.last().rate[1], -cfg.max_rate[1], 1e-4);
    CHECK(r.max_rate[0] <= cfg.max_rate[0] * (1.0 + 1e-6));     // never overshoots
    CHECK(r.min_rate[1] >= -cfg.max_rate[1] * (1.0 + 1e-6));
    CHECK(r.peak_acc[0] <= cfg.max_accel[0] * 1.0001);
    CHECK(r.peak_acc[1] <= cfg.max_accel[1] * 1.0001);
    // ...and the stop is equally gentle
    Run d = run(s, st, 600, [&](int) { st.defl[0] = st.defl[1] = 0.0; });
    CHECK(d.peak_acc[0] <= cfg.max_accel[0] * 1.0001);
    CHECK(d.min_rate[0] >= 0.0);                                 // no reverse kick on stop
    CHECK(d.max_rate[1] <= 0.0);
    CHECK(s.last().rate[0] == 0.0f && s.last().rate[1] == 0.0f);
    CHECK(s.last().flags & FLAG_SETTLED);
}

TEST(step_response_monotone_and_ease_in) {
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;
    st.noise_mv = 0.0;
    std::vector<float> v;
    for (int i = 0; i < 300; ++i) {
        st.defl[0] = 0.6;
        v.push_back(s.update(st.read(0), st.read(1), DT, false).rate[0]);
    }
    bool mono = true;
    for (size_t i = 1; i < v.size(); ++i) mono = mono && v[i] >= v[i - 1];
    CHECK(mono);
    // ease-in: the first ticks accelerate gently (2-pole start: ~quadratic)
    CHECK(v[1] < 0.02f * v.back());
    // expected final value: expo(radial(0.6 * 1500/1400)) * max_rate
    CHECK(v.back() > 0.0f && v.back() < cfg.max_rate[0]);
}

TEST(spring_release_ringing_never_commands_reverse) {
    // Stick held at 0.3, released: centres in 25 ms, overshoots and rings.
    auto release_profile = [](int i, double t0, double ov, double f, double decay) {
        const double t = i * (double)DT - t0;
        if (t < 0) return 0.3;
        if (t < 0.025) return 0.3 * (1.0 - t / 0.025);
        const double tt = t - 0.025;
        return -ov * std::sin(2 * M_PI * f * tt) * std::exp(-tt / decay);
    };
    auto reverse_deg = [&](Config c, double ov, double f, double decay) {
        Shaper s = make_shaper(c);
        Stick st;
        run(s, st, 400, [](int) {});
        Run r = run(s, st, 600, [&](int i) { st.defl[0] = release_profile(i, 1.0, ov, f, decay); });
        CHECK(s.last().rate[0] == 0.0f);
        return r.min_rate[0] < 0.0 ? -r.min_rate[0] * R2D : 0.0;
    };
    Config def;
    // Default tuning, typical stick (14 Hz, 40 ms decay): no reverse at all.
    CHECK(reverse_deg(def, 0.18, 14.0, 0.04) == 0.0);
    // Responsive tuning (tau = 30 ms) + lightly damped stick (9 Hz, 250 ms):
    // without the guard the ringing commands a visible reverse "bounce".
    Config fast = def;
    fast.smooth_tau_s = 0.03f;
    Config fast_ng = fast;
    fast_ng.release_radius = 0.0f;
    const double with_guard = reverse_deg(fast, 0.25, 9.0, 0.25);
    const double no_guard   = reverse_deg(fast_ng, 0.25, 9.0, 0.25);
    CHECK(no_guard > 0.1);          // deg/s of reverse rate
    CHECK(with_guard == 0.0);
    {   // a SLOW deliberate return to centre does not trigger the guard
        Shaper s = make_shaper(def);
        Stick st;
        st.noise_mv = 0.0;
        bool guard = false;
        for (int i = 0; i < 400; ++i) {
            st.defl[0] = 0.5 * (1.0 - i / 400.0);   // 2 s return
            guard = guard || (s.update(st.read(0), st.read(1), DT, false).flags & FLAG_RELEASE_GUARD);
        }
        CHECK(!guard);
    }
}

TEST(center_drift_is_tracked_while_resting) {
    // Thermal drift: true centre walks +90 mV over 3 minutes (0.064 normalised,
    // more than the clean-stick deadband radius).
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;
    const int N = 200 * 180;
    Run r = run(s, st, N, [&](int i) { st.center[0] = 1650.0 + 90.0 * i / N; });
    CHECK(r.nonzero_ticks == 0);                       // never escaped the deadband
    CHECK_NEAR(s.last().center_mv[0], 1740.0, 10.0);   // centre followed
    // bound: tracking never moves beyond center_track_max_mv from boot
    Shaper b = make_shaper(cfg);
    Stick st2;
    st2.center[0] = 1650.0;
    run(b, st2, 200 * 600, [&](int i) { st2.center[0] = 1650.0 + 400.0 * std::min(1.0, i / 24000.0); });
    CHECK(b.last().center_mv[0] <= 1650.0f + cfg.center_track_max_mv + 1e-3f);
    // symmetry after tracking: equal +/- deflections give equal |rate|
    Stick st3;
    st3.noise_mv = 0.0;
    st3.center[0] = 1740.0;
    run(s, st3, 50, [](int) {});
    run(s, st3, 400, [&](int) { st3.defl[0] = 0.5; });
    const float pos = s.last().rate[0];
    run(s, st3, 600, [&](int) { st3.defl[0] = 0.0; });
    run(s, st3, 400, [&](int) { st3.defl[0] = -0.5; });
    CHECK_NEAR(-s.last().rate[0], pos, 0.03 * pos);
}

TEST(open_wire_fault_eases_to_zero_and_latches_until_clean) {
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;
    st.noise_mv = 0.0;
    run(s, st, 400, [&](int) { st.defl[0] = 1.0; });
    CHECK(s.last().rate[0] > 1.5f);
    // wiper open -> RC pulls to GND
    float prev = s.last().rate[0];
    bool mono = true;
    int ticks_to_zero = -1;
    for (int i = 0; i < 400; ++i) {
        const Command& c = s.update(5.0f, st.read(1), DT, false);
        CHECK(c.flags & FLAG_FAULT);
        mono = mono && c.rate[0] <= prev;
        prev = c.rate[0];
        if (ticks_to_zero < 0 && c.rate[0] == 0.0f) ticks_to_zero = i;
    }
    CHECK(mono);
    CHECK(ticks_to_zero > 0 && ticks_to_zero * DT < cfg.max_rate[0] / cfg.max_accel[0] + 0.6f);
    // flickering contact: a plausible reading for < fault_clear_s does not clear it
    st.defl[0] = 0.0;
    for (int i = 0; i < 20; ++i) s.update(st.read(0), st.read(1), DT, false);
    s.update(3300.0f, st.read(1), DT, false);
    for (int i = 0; i < 40; ++i) CHECK(s.update(st.read(0), st.read(1), DT, false).flags & FLAG_FAULT);
    for (int i = 0; i < 20; ++i) s.update(st.read(0), st.read(1), DT, false);
    CHECK(!(s.last().flags & FLAG_FAULT));
    CHECK(s.last().rate[0] == 0.0f);
    // NaN input is a fault, not a NaN rate
    const Command& c = s.update(NAN, 1650.0f, DT, false);
    CHECK((c.flags & FLAG_FAULT) && std::isfinite(c.rate[0]) && std::isfinite(c.rate[1]));
}

TEST(diagonal_moves_start_both_axes_together) {
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;
    st.noise_mv = 0.0;
    int first[2] = {-1, -1};
    for (int i = 0; i < 400; ++i) {
        st.defl[0] = st.defl[1] = 0.4 * i / 400.0;  // slow diagonal push
        const Command& c = s.update(st.read(0), st.read(1), DT, false);
        for (int a = 0; a < 2; ++a)
            if (first[a] < 0 && c.shaped[a] != 0.0f) first[a] = i;
    }
    CHECK(first[0] >= 0 && first[0] == first[1]);
}

TEST(precision_mode_scales_and_eases) {
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;
    st.noise_mv = 0.0;
    run(s, st, 600, [&](int) { st.defl[0] = 1.0; });
    const float full = s.last().rate[0];
    Run r = run(s, st, 600, [&](int) { st.defl[0] = 1.0; }, true);
    CHECK_NEAR(s.last().rate[0], cfg.precision_scale * full, 1e-4);
    CHECK(s.last().flags & FLAG_PRECISION);
    CHECK(r.peak_acc[0] <= cfg.max_accel[0] * 1.0001);
}

TEST(span_learning_and_asymmetric_spans) {
    Config cfg;
    Shaper s = make_shaper(cfg);
    Stick st;
    st.noise_mv = 0.0;
    st.span = 1550.0;  // pot reaches 1550 mV of travel, default span 1400
    run(s, st, 50, [&](int) { st.defl[0] = 1.0; });
    CHECK_NEAR(s.cal(0).span_pos_mv, 1550.0, 1.0);
    CHECK_NEAR(s.last().norm[0], 1.0, 1e-6);
    CHECK_NEAR(s.cal(0).span_neg_mv, cfg.span_default_mv, 1e-3);   // other side untouched
    // spans never exceed span_max (e.g. a fault spike that stays plausible)
    s.update(1650.0f + 1569.0f, 1650.0f, DT, false);
    CHECK(s.cal(0).span_pos_mv <= cfg.span_max_mv);
}

TEST(dt_sanitised_outputs_always_finite) {
    Config cfg;
    Shaper s = make_shaper(cfg);
    for (float dt : {0.0f, -1.0f, NAN, 1e-9f, 10.0f, 1e30f}) {
        const Command& c = s.update(2600.0f, 900.0f, dt, false);
        CHECK(std::isfinite(c.rate[0]) && std::isfinite(c.rate[1]) && std::isfinite(c.radius));
        CHECK(std::fabs(c.rate[0]) <= cfg.max_rate[0] * 1.0001f);
    }
    // a 10 s stall must not turn into one huge acceleration step
    Shaper s2 = make_shaper(cfg);
    const float r = s2.update(3000.0f, 1650.0f, 10.0f, false).rate[0];
    CHECK(r <= cfg.max_accel[0] * 0.05f * 1.0001f);
}

TEST(inverted_axis_and_per_axis_curves) {
    Config cfg;
    cfg.sign[1] = -1.0f;
    cfg.expo_k[0] = 0.0f;   // linear pan
    Shaper s = make_shaper(cfg);
    Stick st;
    st.noise_mv = 0.0;
    run(s, st, 800, [&](int) { st.defl[0] = 0.5; st.defl[1] = 0.5; });
    CHECK(s.last().rate[1] < 0.0f);
    CHECK(s.last().rate[0] > 0.0f);
    // linear pan at ~0.5 deflection is far above the expo pitch (normalised to max rate)
    CHECK(s.last().rate[0] / cfg.max_rate[0] > 2.0f * (-s.last().rate[1] / cfg.max_rate[1]));
}

int main() {
    for (auto& t : tests()) {
        const int f0 = g_fail;
        t.f();
        std::printf("%s %s\n", g_fail == f0 ? "PASS" : "FAIL", t.n);
    }
    std::printf("joystick: %zu tests, %d checks passed, %d failed\n", tests().size(), g_pass, g_fail);
    return g_fail ? 1 : 0;
}
