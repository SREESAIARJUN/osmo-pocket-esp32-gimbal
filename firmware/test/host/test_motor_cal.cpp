// Rev 58.0 host tests: start-up motor auto-calibration (motor_cal.{h,cpp}).
//
// The real sequencer and estimators run against SimAxis, a physics model of
// one gimbal axis behind the motorcal::Io interface:
//   * PMSM, amplitude-invariant dq model, wye winding R_eff (= R_phase + Rds),
//     back-EMF Ke, Kt = 1.5 Ke, inductance neglected (L/R ~ 0.1 ms << 0.5 ms tick)
//   * rotor inertia J, viscous b, smoothed Coulomb friction, gravity load
//     m*g*r*sin(theta + phi), 12th-electrical-harmonic cogging
//   * AS5600: 12-bit quantisation, arbitrary magnet offset, either mounting direction
//   * SimpleFOC duty scaling: actual volts = commanded * Vbus / V_supply_cfg
//   * DC-link sense: P/Vbus + quiescent, gain error, ADC offset and noise
//   * closed loop: SimpleFOC 2.3 angle-mode structure (P_angle -> PI velocity,
//     velocity LPF) commutating with the CANDIDATE zero/direction
// Build & run: firmware/test/host/run_host_tests.sh
#include <cstdio>
#include <cmath>
#include <cstring>
#include <random>
#include "gimbal_config.h"
#include "motor_cal.h"

using namespace motorcal;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((double)(a) - (double)(b)) <= (double)(tol))

static const double kPi = 3.14159265358979323846;
static double adiff(double a, double b) { double d = std::fmod(a - b, 2 * kPi); if (d > kPi) d -= 2 * kPi; if (d <= -kPi) d += 2 * kPi; return d; }

// ===========================================================================
// Plant model
// ===========================================================================
struct Plant {
    int    pp        = 7;
    double r_phase   = 8.5;      // Ohm
    double r_driver  = 0.35;     // Ohm per phase (Rds(on))
    double ke        = 0.035 / 1.5;
    double J         = 2.0e-5;   // kg m^2
    double b         = 2.0e-6;   // N m s/rad
    double tau_c     = 0.6e-3;   // Coulomb, N m
    double tau_grav  = 2.0e-3;   // N m amplitude
    double grav_ph   = 0.4;      // rad
    double cog_amp   = 0.8e-3;   // N m, 12th electrical harmonic
    double cog_ph    = 0.3;
    int    enc_dir   = 1;        // +1: sensor angle increases with rotor angle
    double enc_off   = 1.234;    // magnet offset, rad mech
    double vbus      = 7.90;     // actual pack voltage (cfg says 7.4)
    double v_cfg     = 7.4;
    // current sense
    bool   has_sense = true;
    bool   floating  = false;    // unpopulated ISENSE: random ADC readings
    double i_q0      = 0.012;    // A, 3x TMC6300 quiescent
    double i_gain    = 1.02;     // sense gain error
    double i_off     = 0.004;    // A, ADC offset
    double i_noise   = 0.003;    // A rms per reading
    // faults
    double stall_tau = 0.0;      // extra Coulomb friction (seized bearing)
    long   enc_fail_after = -1;  // ticks until the encoder dies (-1 = never)
};

class SimAxis : public Io {
public:
    explicit SimAxis(const Plant& p, double theta0 = 0.3, unsigned seed = 1)
        : p_(p), th_(theta0), rng_(seed), noise_(0.0, 1.0) {
        sens_prev_ = sensor_raw();
        sens_unw_  = sens_prev_;
    }
    double kt() const { return 1.5 * p_.ke; }
    double r_eff() const { return p_.r_phase + p_.r_driver; }
    // The zero_electric_angle that makes SimpleFOC's electricalAngle == true rotor angle.
    double true_zero() const {
        double z = std::fmod((double)p_.enc_dir * p_.pp * p_.enc_off, 2 * kPi);
        return z < 0 ? z + 2 * kPi : z;
    }
    double theta() const { return th_; }
    double max_excursion() const { return th_max_ - th_min_; }

    // ---- Io ----
    void setVoltage(float uq, float ud, float a) override { uq_ = uq; ud_ = ud; ang_ = a; }
    bool readAngle(float& t) override {
        if (dead()) return false;
        track_sensor();
        t = (float)sens_unw_;
        return true;
    }
    bool readCurrent(float& amps) override {
        if (!p_.has_sense) return false;
        if (p_.floating) { amps = (float)(0.8 * std::fabs(noise_(rng_))); return true; }
        amps = (float)(p_.i_gain * (idc_ + p_.i_q0) + p_.i_off + p_.i_noise * noise_(rng_));
        return true;
    }
    float busVoltage() override { return (float)(p_.vbus + 0.01 * noise_(rng_)); }
    void tick() override {
        ++ticks_;
        const int sub = 20;
        const double h = 0.0005 / sub;
        for (int i = 0; i < sub; ++i) integrate(h);
    }
    bool closedLoopBegin(float zero, int dir, float& theta0) override {
        if (dead()) return false;
        cl_zero_ = zero; cl_dir_ = dir;
        track_sensor();
        theta0 = (float)(cl_dir_ * sens_unw_);
        vel_f_ = 0; vint_ = 0; prev_shaft_ = theta0;
        return true;
    }
    bool closedLoopStep(float target, float& vq, float& vel, float& th) override {
        if (dead()) return false;
        track_sensor();
        const double shaft = cl_dir_ * sens_unw_;
        const double dt = 0.0005;
        const double v_raw = (shaft - prev_shaft_) / dt;
        prev_shaft_ = shaft;
        const double Tf = 0.005;
        vel_f_ += (dt / (Tf + dt)) * (v_raw - vel_f_);
        // SimpleFOC angle mode: P_angle -> velocity setpoint (limit 20) -> PI velocity
        double vsp = 20.0 * (target - shaft);
        vsp = std::fmax(-20.0, std::fmin(20.0, vsp));
        const double e = vsp - vel_f_;
        vint_ += 10.0 * e * dt;
        vint_ = std::fmax(-4.5, std::fmin(4.5, vint_));
        double u = 0.5 * e + vint_;
        u = std::fmax(-4.5, std::fmin(4.5, u));
        // commutate with the candidate offsets
        const double el = cl_dir_ * p_.pp * sens_unw_ - cl_zero_;
        setVoltage((float)u, 0.0f, (float)el);
        vq = (float)u; vel = (float)vel_f_; th = (float)shaft;
        return true;
    }
    void release() override { uq_ = ud_ = 0; released_ = true; }
    bool released() const { return released_; }
    long ticks() const { return ticks_; }

private:
    bool dead() const { return p_.enc_fail_after >= 0 && ticks_ >= p_.enc_fail_after; }
    double sensor_raw() const {
        double s = std::fmod(p_.enc_dir * th_ + p_.enc_off, 2 * kPi);
        if (s < 0) s += 2 * kPi;
        return std::floor(s / (2 * kPi) * 4096.0) * (2 * kPi / 4096.0);  // 12-bit
    }
    void track_sensor() {  // SimpleFOC Sensor::update full-rotation tracking
        const double s = sensor_raw();
        double d = s - sens_prev_;
        if (d > 0.8 * 2 * kPi) d -= 2 * kPi;
        if (d < -0.8 * 2 * kPi) d += 2 * kPi;
        sens_unw_ += d;
        sens_prev_ = s;
    }
    void integrate(double h) {
        const double k  = p_.vbus / p_.v_cfg;
        const double va = k * (ud_ * std::cos(ang_) - uq_ * std::sin(ang_));
        const double vb = k * (ud_ * std::sin(ang_) + uq_ * std::cos(ang_));
        const double te = p_.pp * th_;
        const double vd =  va * std::cos(te) + vb * std::sin(te);
        const double vq = -va * std::sin(te) + vb * std::cos(te);
        const double R  = r_eff();
        const double id = vd / R;
        const double iq = (vq - p_.ke * w_) / R;
        idc_ = 1.5 * (vd * id + vq * iq) / p_.vbus;
        const double tau_m   = kt() * iq;
        const double tau_g   = -p_.tau_grav * std::sin(th_ + p_.grav_ph);
        const double tau_cog = p_.cog_amp * std::sin(12.0 * te + p_.cog_ph);
        const double tc      = p_.tau_c + p_.stall_tau;
        const double drive   = tau_m + tau_g + tau_cog;
        double acc;
        if (std::fabs(w_) < 1e-3 && std::fabs(drive) <= tc) { w_ = 0; acc = 0; }   // stiction
        else acc = (drive - p_.b * w_ - tc * std::tanh(w_ / 1e-3)) / p_.J;
        w_  += acc * h;
        th_ += w_ * h;
        th_min_ = std::fmin(th_min_, th_);
        th_max_ = std::fmax(th_max_, th_);
    }

    Plant  p_;
    double th_, w_ = 0;
    double th_min_ = 1e9, th_max_ = -1e9;
    double uq_ = 0, ud_ = 0, ang_ = 0, idc_ = 0;
    double sens_prev_ = 0, sens_unw_ = 0;
    double cl_zero_ = 0, vel_f_ = 0, vint_ = 0, prev_shaft_ = 0;
    int    cl_dir_ = 1;
    long   ticks_ = 0;
    bool   released_ = false;
    std::mt19937 rng_;
    std::normal_distribution<double> noise_;
};

static SeqConfig cfg_for(const Plant& p) {
    SeqConfig c;
    c.v_supply_cfg = (float)p.v_cfg;
    c.pole_pairs   = MOTOR_POLE_PAIRS;
    c.r_driver     = (float)p.r_driver;
    c.r_nominal    = 8.5f;
    c.ke_nominal   = 0.035f / 1.5f;
    c.temp_c       = 25.0f;
    return c;
}

// ===========================================================================
// Unit tests: conversions, estimators, record
// ===========================================================================
static void test_conversions() {
    // KV must reproduce SimpleFOC 2.3.4: voltage_bemf = w / (KV * _SQRT3) / _RPM_TO_RADS
    for (float ke : {0.01f, 0.0233f, 0.05f}) {
        const float kv = kv_from_ke(ke);
        const float w = 3.7f;
        const float bemf_sfoc = w / (kv * 1.73205080757f) / 0.10471975512f;
        NEAR(bemf_sfoc, ke * w, 1e-5);
        NEAR(ke_from_kv(kv), ke, 1e-7);
    }
    NEAR(kt_from_ke(0.02f), 0.03f, 1e-7);
    NEAR(ke_from_kt(MOTOR_KT), MOTOR_KT / 1.5f, 1e-7);
    CHECK(std::isnan(kv_from_ke(0.0f)) && std::isnan(kv_from_ke(-1.0f)));
    // copper tempco round trip and direction
    NEAR(r_from_ref_temp(r_to_ref_temp(9.0f, 60.0f), 60.0f), 9.0f, 1e-5);
    CHECK(r_to_ref_temp(9.0f, 60.0f) < 9.0f);
    NEAR(r_to_ref_temp(9.0f, NAN), 9.0f, 0);
    NEAR(r_to_ref_temp(10.0f, 125.0f), 10.0f / 1.393f, 1e-4);
    // wraps
    for (int i = -500; i <= 500; ++i) {
        const float a = (float)i * 0.0371f;
        const float w2 = wrap_2pi(a), w1 = wrap_pi(a);
        CHECK(w2 >= 0.0f && w2 < TWO_PI_F);
        CHECK(w1 > -PI_F - 1e-6f && w1 <= PI_F + 1e-6f);
        NEAR(adiff(w2, a), 0.0, 1e-4);
        NEAR(adiff(w1, a), 0.0, 1e-4);
    }
    CHECK(std::isnan(wrap_2pi(NAN)) && std::isnan(wrap_pi(INFINITY)));
    // cogging window: 12N14P -> 84 periods / rev
    NEAR(cogging_window(12, 7, 1), 2 * kPi / 84, 1e-6);
    NEAR(cogging_window(12, 7, 6), 6 * 2 * kPi / 84, 1e-6);
    CHECK(std::isnan(cogging_window(0, 7, 1)));
    char buf[96];
    flags_str(F_OFFSET_OK | F_R_NO_SENSE | F_KV_OK, buf, sizeof buf);
    CHECK(std::strcmp(buf, "OFFSET|KV|R_NO_SENSE") == 0);
    flags_str(0xFFFFFFFFu, buf, 16);   // truncation must stay terminated
    CHECK(std::strlen(buf) < 16);
}

static void test_resistance_estimator() {
    ResistanceLimits lim;
    // exact data incl. quiescent power
    {
        ResistanceEstimator e;
        const double R = 8.85, P0 = 0.09;
        for (double v : {0.0, 1.2, 1.8, 2.6}) for (int a = 0; a < 3; ++a) {
            const double vb = 7.9;
            e.add((float)v, (float)vb, (float)((1.5 * v * v / R + P0) / vb));
        }
        ResistanceResult r;
        CHECK(e.solve(0.35f, lim, r));
        NEAR(r.r_eff, R, 1e-3);
        NEAR(r.r_phase, R - 0.35, 1e-3);
        NEAR(r.p0, P0, 1e-4);
        CHECK(r.r2 > 0.9999f);
    }
    // one amplitude only (no spread in x) -> refuse
    {
        ResistanceEstimator e;
        for (int i = 0; i < 5; ++i) e.add(2.0f, 7.4f, 0.1f);
        ResistanceResult r;
        CHECK(!e.solve(0.0f, lim, r));
        CHECK(r.flags & F_R_POOR_FIT);
    }
    // floating ADC (uncorrelated) -> poor fit
    {
        ResistanceEstimator e;
        std::mt19937 g(7);
        std::uniform_real_distribution<double> u(0, 0.8);
        for (double v : {0.0, 1.2, 1.8, 2.6}) for (int a = 0; a < 3; ++a) e.add((float)v, 7.4f, (float)u(g));
        ResistanceResult r;
        CHECK(!e.solve(0.0f, lim, r));
    }
    // negative slope / too few points
    {
        ResistanceEstimator e;
        e.add(0.0f, 7.4f, 0.3f); e.add(1.5f, 7.4f, 0.2f); e.add(2.5f, 7.4f, 0.1f);
        ResistanceResult r;
        CHECK(!e.solve(0.0f, lim, r));
        ResistanceEstimator f;
        f.add(0.0f, 7.4f, 0.0f); f.add(2.0f, 7.4f, 0.1f);
        CHECK(!f.solve(0.0f, lim, r));
    }
    // non-finite input ignored
    {
        ResistanceEstimator e;
        e.add(NAN, 7.4f, 0.1f); e.add(1.0f, 0.0f, 0.1f); e.add(1.0f, 7.4f, INFINITY);
        CHECK(e.size() == 0);
    }
}

static void test_bemf_estimator() {
    BemfLimits lim;
    {
        BemfEstimator e;
        const double ke = 0.0241, vc = 0.061, vg = 0.12;   // Coulomb and gravity voltages
        const double sp[] = {1.0, 2.0, 3.0, 4.0};
        for (int k = 0; k < 4; ++k) for (int i = 0; i < 200; ++i) {
            const double cog = 0.05 * std::sin(i * 2 * kPi / 50.0);   // averages out over the window
            e.add(k, +1, (float)(ke * sp[k] + vc + vg + cog), (float)sp[k]);
            e.add(k, -1, (float)(-ke * sp[k] - vc + vg + cog), (float)-sp[k]);
        }
        BemfResult b;
        CHECK(e.solve(lim, b));
        NEAR(b.ke, ke, 1e-6);
        NEAR(b.v_coulomb, vc, 1e-6);
        NEAR(b.v_static, vg, 1e-6);
        NEAR(b.kt, 1.5 * ke, 1e-6);
        NEAR(b.kv_rpm_v, 1.0 / (std::sqrt(3.0) * ke * 0.10471975512), 0.01);
        CHECK(b.speeds == 4);
    }
    {   // only two speeds -> below min_speeds
        BemfEstimator e;
        for (int k = 0; k < 2; ++k) for (int i = 0; i < 50; ++i) { e.add(k, 1, 0.1f * (k + 1), 1.0f * (k + 1)); e.add(k, -1, -0.1f * (k + 1), -1.0f * (k + 1)); }
        BemfResult b;
        CHECK(!e.solve(lim, b));
        CHECK(b.flags & F_KV_POOR_FIT);
    }
    {   // asymmetric window coverage drops the speed
        BemfEstimator e;
        for (int k = 0; k < 3; ++k) for (int i = 0; i < 100; ++i) {
            e.add(k, 1, 0.02f * (k + 1), 1.0f * (k + 1));
            if (k != 1 || i < 40) e.add(k, -1, -0.02f * (k + 1), -1.0f * (k + 1));
        }
        BemfResult b;
        CHECK(!e.solve(lim, b));
        CHECK(b.speeds == 2);
    }
    {   // axis did not move backwards -> bin rejected
        BemfEstimator e;
        for (int k = 0; k < 3; ++k) for (int i = 0; i < 100; ++i) { e.add(k, 1, 0.1f, 1.0f); e.add(k, -1, 0.1f, 0.0f); }
        BemfResult b;
        CHECK(!e.solve(lim, b));
        CHECK(b.speeds == 0);
    }
}

static void test_offset_estimator_synthetic() {
    // Points generated from eq. (1) plus friction hysteresis and a static load.
    for (int dir : {1, -1}) for (double zero : {0.2, 3.0, 6.1}) {
        OffsetEstimator e;
        const int pp = 7;
        const double c = 0.25;          // V: tau_L R / Kt
        const double h = 0.06;          // friction hysteresis, rad elec
        const double V[] = {1.2, 1.8, 2.6};
        for (int l = 0; l < 3; ++l) for (int i = 0; i < 120; ++i) {
            const double ph = 2 * kPi * i / 120.0;
            const double a  = kPi * std::sin(ph);
            const int    sd = std::cos(ph) >= 0 ? 1 : -1;
            const double delta = std::asin(c / V[l]) - sd * h;   // rotor el. angle = a + delta
            const double el = a + delta;
            const double m  = (el + zero) / (dir * pp);           // eq. (1) solved for theta_m
            e.add(l, (float)V[l], (float)a, (float)m, sd);
        }
        OffsetResult o;
        CHECK(e.solve(pp, OffsetLimits{}, o));
        CHECK(o.direction == dir);
        NEAR(o.pole_pairs_meas, 7.0, 0.05);
        NEAR(adiff(o.zero, zero), 0.0, 1e-3);                   // load extrapolated away
        NEAR(adiff(o.zero_raw, zero + std::asin(c / 2.6)), 0.0, 1e-3);
        NEAR(o.load_angle, std::asin(c / 2.6), 1e-3);
        NEAR(o.load_coeff, c, 1e-3);
        NEAR(o.hysteresis, h, 1e-3);
        CHECK(o.residual_rms < 1e-3f);
    }
    {   // too few points / no motion
        OffsetEstimator e;
        for (int i = 0; i < 20; ++i) e.add(0, 2.0f, 0.1f * (float)i, 0.5f, i % 2 ? 1 : -1);
        OffsetResult o;
        CHECK(!e.solve(7, OffsetLimits{}, o));
        CHECK(o.flags & F_STALL);
    }
    {   // wrong pole pairs: a 6 pp motor read as 7
        OffsetEstimator e;
        for (int i = 0; i < 100; ++i) {
            const double a = kPi * std::sin(2 * kPi * i / 100.0);
            e.add(0, 2.0f, (float)a, (float)(a / 6.0), std::cos(2 * kPi * i / 100.0) >= 0 ? 1 : -1);
        }
        OffsetResult o;
        CHECK(!e.solve(7, OffsetLimits{}, o));
        CHECK(o.flags & F_POLE_PAIRS);
        NEAR(o.pole_pairs_meas, 6.0, 0.01);
    }
    {   // invalid input rejected
        OffsetEstimator e;
        CHECK(!e.add(-1, 1.0f, 0.0f, 0.0f, 1));
        CHECK(!e.add(OffsetEstimator::MAX_LEVELS, 1.0f, 0.0f, 0.0f, 1));
        CHECK(!e.add(0, 0.0f, 0.0f, 0.0f, 1));
        CHECK(!e.add(0, 1.0f, NAN, 0.0f, 1));
        CHECK(!e.add(0, 1.0f, 0.0f, 0.0f, 0));
        CHECK(e.size() == 0);
    }
}

static void test_record() {
    CHECK(crc32("123456789", 9) == 0xCBF43926u);   // CRC-32/ISO-HDLC check value
    Record r;
    record_init(r);
    CHECK(record_valid(r));
    r.axis[1] = AxisRecord{1.5f, -1, 7, 7, 0, 8.9f, 8.55f, 0.0235f, 0.05f};
    CHECK(!record_valid(r));            // crc stale
    record_seal(r);
    CHECK(record_valid(r));
    Record bad = r;
    reinterpret_cast<uint8_t*>(&bad)[20] ^= 0x01;
    CHECK(!record_valid(bad));          // single-bit flip
    bad = r; bad.version = 2; record_seal(bad); bad.version = 2;
    bad.crc = crc32(&bad, offsetof(Record, crc));
    CHECK(!record_valid(bad));          // version mismatch
    bad = r; bad.axis[1].zero = 7.0f; record_seal(bad);
    CHECK(!record_valid(bad));          // out-of-range zero with valid crc
    bad = r; bad.axis[1].direction = 0; record_seal(bad);
    CHECK(!record_valid(bad));
    bad = r; bad.axis[1].ke = NAN; record_seal(bad);
    CHECK(!record_valid(bad));

    AxisCal c;
    AxisRecord a;
    CHECK(!axis_record_from(c, a));     // no offset -> nothing to store
    c.flags = F_OFFSET_OK | F_R_OK | F_KV_OK;
    c.zero = 2.0f; c.direction = 1; c.pole_pairs_meas = 6.98f;
    c.r_eff = 9.2f; c.r_phase = 8.85f; c.r_eff_25 = 9.0f; c.ke = 0.024f; c.v_coulomb = 0.05f;
    CHECK(axis_record_from(c, a));
    CHECK(a.valid == 7 && a.pole_pairs == 7 && a.direction == 1);
    NEAR(a.r_phase_25, 9.0f - 0.35f, 1e-5);
    AxisRecord b = a;
    CHECK(!record_differs(a, b));
    b.zero = a.zero + 0.01f;   CHECK(!record_differs(a, b));
    b.zero = a.zero + 0.05f;   CHECK(record_differs(a, b));
    b = a; b.r_eff_25 *= 1.02f; CHECK(!record_differs(a, b));
    b = a; b.r_eff_25 *= 1.05f; CHECK(record_differs(a, b));
    b = a; b.ke *= 1.05f;       CHECK(record_differs(a, b));
    b = a; b.direction = -1;    CHECK(record_differs(a, b));
}

static void test_reconcile() {
    AxisRecord st{2.0f, 1, 7, 7, 0, 9.0f, 8.65f, 0.024f, 0.05f};
    ReconcileLimits lim;
    AxisCal v;
    v.flags = F_OFFSET_OK | F_R_OK | F_KV_SKIPPED;
    v.direction = 1; v.zero = v.zero_raw = 2.0f + 0.2f;   // load angle inside tolerance
    v.r_eff_25 = 9.3f;
    CHECK(reconcile(st, lim, v));
    NEAR(v.zero, 2.0f, 0);                       // stored (load-free) offset wins
    CHECK(v.kv_ok());
    NEAR(v.ke, 0.024f, 0);
    CHECK(!(v.flags & F_R_DRIFT));
    v.r_eff_25 = 12.0f;
    CHECK(reconcile(st, lim, v));
    CHECK(v.flags & F_R_DRIFT);                  // +33 % -> connector / winding warning

    AxisCal s = AxisCal{}; s.flags = F_OFFSET_OK; s.direction = 1; s.zero_raw = 2.0f + 0.6f;
    CHECK(!reconcile(st, lim, s));
    CHECK(s.flags & F_ZERO_SHIFTED);
    AxisCal w = AxisCal{}; w.flags = F_OFFSET_OK; w.direction = -1; w.zero_raw = 2.0f;
    CHECK(!reconcile(st, lim, w));
    CHECK(w.flags & F_DIR_CHANGED);
    AxisCal z = AxisCal{}; z.flags = F_OFFSET_OK; z.direction = 1; z.zero_raw = TWO_PI_F - 0.1f;
    AxisRecord st0 = st; st0.zero = 0.05f;
    CHECK(reconcile(st0, lim, z));               // wrap-around is not a shift
    AxisRecord none{}; none.valid = 0;
    AxisCal n = AxisCal{}; n.flags = F_OFFSET_OK; n.direction = 1;
    CHECK(!reconcile(none, lim, n));
}

// ===========================================================================
// End-to-end: sequencer against the plant model
// ===========================================================================
static void print_cal(const char* tag, const AxisCal& c, const SimAxis& s) {
    char fb[200];
    std::printf("  [%s] flags=%s zero=%.4f (true %.4f, raw %.4f) dir=%d pp=%.3f load=%.1fdeg hyst=%.1fdeg res=%.1fdeg\n"
                "        R_eff=%.3f (true %.3f) R_ph=%.3f r2=%.5f  Ke=%.5f (true %.5f) KV=%.1f Kt=%.4f Vc=%.4f r2=%.4f  t=%.1fs exc=%.1fdeg\n",
                tag, flags_str(c.flags, fb, sizeof fb), c.zero, s.true_zero(), c.zero_raw, c.direction,
                c.pole_pairs_meas, c.load_angle * 57.2958, c.hysteresis * 57.2958, c.residual_rms * 57.2958,
                c.r_eff, s.r_eff(), c.r_phase, c.r_r2, c.ke, s.kt() / 1.5, c.kv_rpm_v, c.kt, c.v_coulomb, c.kv_r2,
                c.duration_s, s.max_excursion() * 57.2958);
}

static void test_e2e_full_nominal() {
    Plant p;
    SimAxis s(p);
    AxisCal c;
    CHECK(run(s, cfg_for(p), Mode::FULL, c));
    print_cal("full/nominal", c, s);
    CHECK(c.offset_ok() && c.r_ok() && c.kv_ok());
    CHECK(c.direction == 1);
    NEAR(c.pole_pairs_meas, 7.0, 0.2);
    // Load-extrapolated zero within 2 deg electrical of the truth...
    NEAR(adiff(c.zero, s.true_zero()), 0.0, 2.0 * kPi / 180);
    // ...while the single-amplitude (SimpleFOC-style) offset carries the gravity load angle.
    CHECK(std::fabs(adiff(c.zero_raw, s.true_zero())) > 3.0 * std::fabs(adiff(c.zero, s.true_zero())));
    // R within 3 % despite 2 % sense gain error, ADC offset, quiescent current, Vbus 7.9 vs 7.4 cfg
    NEAR(c.r_eff / s.r_eff(), 1.0, 0.03);
    NEAR(c.r_phase, p.r_phase, 0.3);
    // Ke within 3 %; KV/Kt consistent
    NEAR(c.ke / p.ke, 1.0, 0.12);
    NEAR(c.kt, 1.5 * c.ke, 1e-6);
    NEAR(c.kv_rpm_v, kv_from_ke(c.ke), 1e-3);
    // Coulomb friction voltage R*tau_c/Kt
    NEAR(c.v_coulomb, s.r_eff() * p.tau_c / s.kt(), 0.35 * s.r_eff() * p.tau_c / s.kt());
    CHECK(s.released());
    CHECK(s.max_excursion() < 1.7 + 0.5);   // travel budget (+ initial snap) respected
}

static void test_e2e_full_reversed_variant() {
    Plant p;
    p.enc_dir = -1; p.enc_off = 4.4; p.r_phase = 11.0; p.ke = 0.030; p.vbus = 6.6;
    p.tau_grav = 3.0e-3; p.grav_ph = -1.0; p.cog_amp = 1.2e-3;
    SimAxis s(p, -0.4, 3);
    SeqConfig cfg = cfg_for(p);
    AxisCal c;
    CHECK(run(s, cfg, Mode::FULL, c));
    print_cal("full/reversed", c, s);
    CHECK(c.offset_ok() && c.r_ok() && c.kv_ok());
    CHECK(c.direction == -1);
    NEAR(adiff(c.zero, s.true_zero()), 0.0, 2.5 * kPi / 180);
    NEAR(c.r_eff / s.r_eff(), 1.0, 0.03);
    NEAR(c.ke / p.ke, 1.0, 0.12);
}

static void test_e2e_no_sense_and_floating() {
    {
        Plant p; p.has_sense = false;
        SimAxis s(p);
        AxisCal c;
        CHECK(run(s, cfg_for(p), Mode::FULL, c));
        CHECK(c.offset_ok() && !c.r_ok() && c.kv_ok());   // KV needs no current sense
        CHECK(c.flags & F_R_NO_SENSE);
    }
    {
        Plant p; p.floating = true;
        SimAxis s(p);
        AxisCal c;
        CHECK(run(s, cfg_for(p), Mode::FULL, c));
        CHECK(c.offset_ok() && !c.r_ok());
        CHECK(c.flags & F_R_POOR_FIT);
    }
}

static void test_e2e_faults() {
    {   // shorted turns: R at 35 % of nominal, confidently measured -> flagged out of range
        Plant p; p.r_phase = 3.0;
        SimAxis s(p);
        AxisCal c;
        run(s, cfg_for(p), Mode::FULL, c);
        CHECK(c.flags & F_R_OUT_OF_RANGE);
        CHECK(!c.r_ok());
    }
    {   // wrong motor (6 pole pairs) -> offset refused, axis must stay off
        Plant p; p.pp = 6;
        SimAxis s(p);
        AxisCal c;
        CHECK(!run(s, cfg_for(p), Mode::FULL, c));
        CHECK(c.flags & F_POLE_PAIRS);
        CHECK(s.released());
    }
    {   // seized bearing: rotor cannot follow the vector
        Plant p; p.stall_tau = 0.05;
        SimAxis s(p);
        AxisCal c;
        CHECK(!run(s, cfg_for(p), Mode::FULL, c));
        CHECK(c.flags & F_STALL);
    }
    {   // encoder dies mid-sweep
        Plant p; p.enc_fail_after = 3000;
        SimAxis s(p);
        AxisCal c;
        CHECK(!run(s, cfg_for(p), Mode::FULL, c));
        CHECK((c.flags & F_NO_SENSOR) && (c.flags & F_ABORTED));
        CHECK(s.released());
        CHECK(s.ticks() < 3100);   // aborted promptly
    }
    {   // encoder dies during the closed-loop KV stage
        Plant p; p.enc_fail_after = 22000;
        SimAxis s(p);
        AxisCal c;
        CHECK(!run(s, cfg_for(p), Mode::FULL, c));
        CHECK(c.flags & F_ABORTED);
    }
    {   // travel budget too small for any speed -> KV unavailable but axis OK
        Plant p;
        SimAxis s(p);
        SeqConfig cfg = cfg_for(p);
        cfg.kv_max_travel = 0.3f;
        AxisCal c;
        CHECK(run(s, cfg, Mode::FULL, c));
        CHECK(c.offset_ok() && !c.kv_ok());
        CHECK(c.flags & F_KV_TRAVEL);
        CHECK(s.max_excursion() < 0.3 + 1.0);
    }
}

static void test_e2e_verify_cycle() {
    Plant p;
    // First boot: FULL, store
    SimAxis s1(p);
    AxisCal full;
    CHECK(run(s1, cfg_for(p), Mode::FULL, full));
    AxisRecord rec;
    CHECK(axis_record_from(full, rec));
    // Next boot, warmer winding: VERIFY (single amplitude, no KV) is much shorter
    Plant warm = p; warm.r_phase = p.r_phase * (1.0 + CU_ALPHA * 15.0);
    SimAxis s2(warm, 0.9, 5);
    SeqConfig cfg = cfg_for(warm);
    cfg.temp_c = 40.0f;
    AxisCal v;
    CHECK(run(s2, cfg, Mode::VERIFY, v));
    print_cal("verify", v, s2);
    CHECK(v.duration_s < 0.35f * full.duration_s);
    CHECK(v.flags & F_KV_SKIPPED);
    CHECK(reconcile(rec, ReconcileLimits{}, v));
    NEAR(v.zero, full.zero, 0);
    CHECK(v.kv_ok());
    CHECK(!(v.flags & F_R_DRIFT));     // temperature-normalised R matches the stored one
    NEAR(v.r_eff_25 / rec.r_eff_25, 1.0, 0.05);
    // Magnet slipped by 6 deg mech (42 deg elec) since the record was written
    Plant slip = p; slip.enc_off = p.enc_off + 6.0 * kPi / 180;
    SimAxis s3(slip, 0.2, 9);
    AxisCal v3;
    CHECK(run(s3, cfg_for(slip), Mode::VERIFY, v3));
    CHECK(!reconcile(rec, ReconcileLimits{}, v3));
    CHECK(v3.flags & F_ZERO_SHIFTED);
}

int main() {
    std::printf("motor_cal: conversions\n");               test_conversions();
    std::printf("motor_cal: resistance estimator\n");      test_resistance_estimator();
    std::printf("motor_cal: back-EMF estimator\n");        test_bemf_estimator();
    std::printf("motor_cal: offset estimator\n");          test_offset_estimator_synthetic();
    std::printf("motor_cal: NVS record\n");                test_record();
    std::printf("motor_cal: reconcile\n");                 test_reconcile();
    std::printf("motor_cal: e2e FULL nominal\n");          test_e2e_full_nominal();
    std::printf("motor_cal: e2e FULL reversed/variant\n"); test_e2e_full_reversed_variant();
    std::printf("motor_cal: e2e no/floating sense\n");     test_e2e_no_sense_and_floating();
    std::printf("motor_cal: e2e faults\n");                test_e2e_faults();
    std::printf("motor_cal: e2e VERIFY cycle\n");          test_e2e_verify_cycle();
    std::printf("motor_cal: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
