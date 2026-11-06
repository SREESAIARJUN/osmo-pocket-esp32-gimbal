// Motor auto-calibration — Revision 58.0 / 150
//
// Start-up identification, per axis, of:
//   1. encoder zero-electrical-offset, sensor direction and pole-pair check
//   2. phase resistance R           (needs the Rev 58 DC-link current sense)
//   3. back-EMF constant Ke -> KV, and Kt = 1.5 * Ke
//
// Everything in this module is hardware-independent: the sequencer talks to
// the motor through the small `motorcal::Io` interface (implemented over
// SimpleFOC in main.cpp and over a physics model in the host tests), and the
// estimators are pure math. Single-precision float on the hot paths (ESP32-S3
// has a single-precision FPU only); regressions accumulate in double because
// they run once per boot, not per FOC cycle.
//
// ---------------------------------------------------------------------------
// Conventions (identical to SimpleFOC 2.3.x)
//   electrical angle  theta_e = wrap(dir * pp * theta_m - zero_electric_angle)
//   setPhaseVoltage(Uq, Ud, a): voltage vector at angle a + atan2(Uq, Ud) in
//   the alpha/beta frame, amplitude-invariant Clarke (Uq, Ud = phase peak).
//   A pure d-axis vector (Ud = V, Uq = 0) at angle a pulls the rotor d-axis to
//   electrical angle a, so a settled rotor gives
//       zero = dir * pp * theta_m - a                                    (1)
//
// ---------------------------------------------------------------------------
// 1. Zero offset (OffsetEstimator)
//   The d-axis vector is swept sinusoidally, a(t) = a0 + A sin(2 pi t / T),
//   A = pi (one full electrical revolution), at several amplitudes V_j.
//   For every sample  e = dir*pp*theta_m - a  =  zero + delta  where delta
//   is the rotor's displacement from the vector:
//     * friction / back-EMF damping: delta has the sign of the sweep velocity
//       -> forward and reverse circular means are averaged (hysteresis cancels)
//     * a static load tau_L (gravity on the camera, cable drag):
//           sin(delta) = tau_L * R / (Kt * V)       (alignment stiffness ~ V)
//       -> the per-amplitude offsets phi_j are fitted with
//           sin(phi_j - zero) = c / V_j             (Gauss-Newton, 2 params)
//       and the intercept "zero" is the load-free offset. SimpleFOC's
//       single-point alignment keeps the full delta (~14 deg electrical for a
//       2 mN*m camera imbalance at 2 V) as a permanent torque-angle error.
//   Direction = sign of d(theta_m)/d(a) (regression), and pp_meas =
//   1/|slope| must match MOTOR_POLE_PAIRS (wrong motor / stall / binding).
//
// 2. Phase resistance (ResistanceEstimator)
//   With a static vector of amplitude V (rotor aligned, stationary) the phase
//   voltages are V*cos(a - k*2pi/3); in a wye winding of per-phase resistance
//   R_eff (winding + driver Rds(on) + cabling) the input power is
//       P = Vbus * I_dc = 1.5 * V^2 / R_eff + P0                         (2)
//   independent of the angle a and of the common-mode/centering used by the
//   modulator. P0 is driver quiescent + current-sense offset. A line fit of P
//   against x = 1.5 V^2 over a zero-vector point and several amplitudes and
//   angles gives slope 1/R_eff; the intercept absorbs P0 and ADC offset.
//   V is the ACTUAL amplitude: SimpleFOC computes duty = U / V_supply_cfg, so
//   V_actual = V_cmd * Vbus_measured / V_supply_cfg (2S pack: 6.0 .. 8.4 V).
//
// 3. Back-EMF constant (BemfEstimator)
//   Closed-loop (after initFOC with the offsets from 1.), the angle target is
//   ramped at constant speeds +-w_k through the SAME position window, whose
//   width is an integer number of cogging periods. At constant speed
//       Vq = Ke*w + (R/Kt)*(tau_c*sgn(w) + b*w + tau_grav(theta) + tau_cog(theta))
//   The window mean of tau_cog is 0, tau_grav is identical in both directions,
//   so per speed  D_k = (Vq+ - Vq-)/2 = (Ke + R*b/Kt) * w_k + R*tau_c/Kt.
//   A line fit over the speeds gives Ke (slope; R*b/Kt < 1 % for gimbal
//   bearings) and the Coulomb-friction voltage (intercept). Then
//       Kt = 1.5 * Ke   [N*m per A of phase-peak q current]
//       KV = 1 / (sqrt(3) * Ke * RPM_TO_RADS)  [rpm/V line-line peak]
//   which is exactly the KV that makes SimpleFOC's own
//   voltage_bemf = w / (KV * sqrt3) / RPM_TO_RADS reproduce the measured Ke.
// ---------------------------------------------------------------------------
#ifndef MOTOR_CAL_H
#define MOTOR_CAL_H

#include <stdint.h>
#include <stddef.h>

namespace motorcal {

constexpr float PI_F        = 3.14159265358979323846f;
constexpr float TWO_PI_F    = 6.28318530717958647692f;
constexpr float SQRT3_F     = 1.73205080756887729353f;
constexpr float RPM_TO_RADS = 0.10471975512f;   // == SimpleFOC _RPM_TO_RADS
constexpr float CU_ALPHA    = 0.00393f;         // copper resistance tempco, 1/K
constexpr float R_REF_TEMP_C = 25.0f;

float wrap_2pi(float a);   // [0, 2pi); NaN for non-finite input
float wrap_pi(float a);    // (-pi, pi]; NaN for non-finite input

// Ke [V*s/rad, SimpleFOC Uq units] <-> KV [rpm/V] and Kt [N*m/A]
float kv_from_ke(float ke);
float ke_from_kv(float kv);
inline float kt_from_ke(float ke) { return 1.5f * ke; }
inline float ke_from_kt(float kt) { return kt / 1.5f; }

// Copper temperature normalisation: R(T) -> R(25 degC). Non-finite T returns r.
float r_to_ref_temp(float r, float temp_c);
float r_from_ref_temp(float r25, float temp_c);

// ===========================================================================
// Status codes (bit flags, combined per axis)
// ===========================================================================
enum Flags : uint32_t {
    F_OFFSET_OK        = 1u << 0,
    F_R_OK             = 1u << 1,
    F_KV_OK            = 1u << 2,
    // offset stage
    F_NO_SENSOR        = 1u << 4,   // encoder unhealthy/stale during calibration
    F_STALL            = 1u << 5,   // rotor did not follow the vector (|slope| far too small)
    F_POLE_PAIRS       = 1u << 6,   // measured pp != configured pp
    F_OFFSET_NOISY     = 1u << 7,   // residual RMS too large (magnet/encoder/binding)
    F_HEAVY_LOAD       = 1u << 8,   // static load angle beyond limit -> fit unreliable
    F_ZERO_SHIFTED     = 1u << 9,   // VERIFY: zero differs from stored record
    F_DIR_CHANGED      = 1u << 10,  // VERIFY: direction differs from stored record
    // resistance stage
    F_R_NO_SENSE       = 1u << 12,  // no DC-link current sense on this build/board
    F_R_POOR_FIT       = 1u << 13,  // r^2 / slope sign check failed (floating ADC, noise)
    F_R_OUT_OF_RANGE   = 1u << 14,  // confident measurement but outside [min,max]*nominal
    F_R_DRIFT          = 1u << 15,  // R(25C) changed vs stored record (connector/winding)
    // KV stage
    F_KV_SKIPPED       = 1u << 16,  // VERIFY mode: KV taken from the stored record
    F_KV_POOR_FIT      = 1u << 17,
    F_KV_OUT_OF_RANGE  = 1u << 18,
    F_KV_TRAVEL        = 1u << 19,  // not enough speeds fit inside the travel limit
    F_CLOSED_LOOP_FAIL = 1u << 20,  // initFOC with the measured offsets failed
    F_ABORTED          = 1u << 21,  // Io reported a fault mid-run
};

const char* flags_str(uint32_t flags, char* buf, size_t len);

// ===========================================================================
// 1. Zero-electrical-offset / direction / pole-pair estimator
// ===========================================================================
struct OffsetLimits {
    float pp_tolerance      = 0.45f;   // |pp_meas - pp| (absolute): must round to the configured pp
    float stall_fraction    = 0.5f;    // |slope| < stall_fraction / pp -> stall
    float max_residual_rms  = 0.35f;   // rad electrical (20 deg)
    float max_load_angle    = 0.70f;   // rad electrical (40 deg) at the highest amplitude
};

struct OffsetResult {
    bool    ok = false;
    uint32_t flags = 0;
    int8_t  direction = 0;          // +1 (CW) / -1 (CCW), SimpleFOC Direction values
    float   pole_pairs_meas = 0;    // 1 / |slope|
    float   zero = 0;               // load-extrapolated zero_electric_angle, [0, 2pi)
    float   zero_raw = 0;           // highest-amplitude offset (what a single-point align gives)
    float   load_angle = 0;         // rad elec at the highest amplitude: asin(c / Vmax)
    float   load_coeff = 0;         // c = tau_L * R / Kt  [V]
    float   hysteresis = 0;         // rad elec, half the fwd/rev split (friction)
    float   residual_rms = 0;       // rad elec
    uint8_t levels = 0;
    uint16_t points = 0;
};

class OffsetEstimator {
public:
    static constexpr int MAX_LEVELS = 4;
    static constexpr int MAX_POINTS = 512;

    void reset();
    // level: amplitude index (0..MAX_LEVELS-1); v_amp: ACTUAL vector amplitude, V;
    // a_cmd: commanded electrical angle (unwrapped, rad); theta_m: unwrapped
    // mechanical angle (rad); sweep_dir: sign of d(a_cmd)/dt (+1 / -1).
    bool add(int level, float v_amp, float a_cmd, float theta_m, int sweep_dir);
    bool solve(int pole_pairs, const OffsetLimits& lim, OffsetResult& out) const;
    int  size() const { return n_; }

private:
    float  a_[MAX_POINTS];
    float  m_[MAX_POINTS];
    int8_t lvl_[MAX_POINTS];
    int8_t dir_[MAX_POINTS];
    int    n_ = 0;
    float  v_sum_[MAX_LEVELS] = {};
    int    v_n_[MAX_LEVELS] = {};
};

// ===========================================================================
// 2. Phase-resistance estimator (DC-link power balance, eq. 2)
// ===========================================================================
struct ResistanceLimits {
    float min_r2        = 0.98f;
    float min_x_std     = 0.5f;    // V^2: required std-dev of x = 1.5*V^2 (needs >= 2 distinct amplitudes)
};

struct ResistanceResult {
    bool  ok = false;
    uint32_t flags = 0;
    float r_eff = 0;      // Ohm, per phase as seen by Vq (winding + Rds(on) + cable)
    float r_phase = 0;    // Ohm, r_eff - r_driver (winding + cable)
    float p0 = 0;         // W, zero-vector input power (quiescent + offset)
    float r2 = 0;
    uint16_t n = 0;
};

class ResistanceEstimator {
public:
    void reset();
    // v_amp: ACTUAL static-vector amplitude (0 for the zero-vector point), V;
    // vbus: measured bus voltage, V; i_dc: mean DC-link current, A.
    void add(float v_amp, float vbus, float i_dc);
    bool solve(float r_driver, const ResistanceLimits& lim, ResistanceResult& out) const;
    int  size() const { return (int)n_; }

private:
    double sx_ = 0, sy_ = 0, sxx_ = 0, sxy_ = 0, syy_ = 0;
    uint32_t n_ = 0;
};

// ===========================================================================
// 3. Back-EMF estimator (bidirectional constant-speed sweeps)
// ===========================================================================
struct BemfLimits {
    float min_r2      = 0.95f;
    int   min_speeds  = 3;
    int   min_samples = 72;    // per (speed, direction) bin: >= 2 samples per position bin
    float max_count_asym = 0.25f;  // |n+ - n-| / max(n+, n-): unequal window coverage -> drop speed
};

struct BemfResult {
    bool  ok = false;
    uint32_t flags = 0;
    float ke = 0;          // V*s/rad (mechanical), Uq units
    float kv_rpm_v = 0;    // rpm/V (line-line peak; SimpleFOC KV_rating convention)
    float kt = 0;          // N*m/A
    float v_coulomb = 0;   // V, Coulomb friction voltage R*tau_c/Kt (intercept)
    float v_static = 0;    // V, direction-independent mean load (gravity etc.)
    float r2 = 0;
    uint8_t speeds = 0;
};

class BemfEstimator {
public:
    static constexpr int MAX_SPEEDS = 8;
    void reset();
    // vq: ACTUAL q-axis voltage (V); omega: measured mechanical velocity (rad/s);
    // weight: the sample's position increment |d theta| (rad) so the bin means
    // are path averages over the window (see motor_cal.cpp cl_move()).
    void add(int speed_idx, int dir, float vq, float omega, float weight = 1.0f);
    bool solve(const BemfLimits& lim, BemfResult& out) const;
    uint32_t count(int speed_idx, int dir) const;

    // Position-binned sweep (used by the sequencer). Samples are binned by the
    // MEASURED angle into SWEEP_BINS equal bins across [wlo, whi]; end_sweep()
    // adds the equal-weight mean of the bin means, i.e. a path average over
    // theta rather than a time average. Time-averaging is biased whenever the
    // speed ripples with position (cogging at low speed): the rotor dwells
    // longer where the load opposes motion. With 6 bins per cogging period
    // the 12th-harmonic cogging and any smooth load cancel exactly.
    // Returns false (sweep discarded) unless every bin received a sample.
    static constexpr int SWEEP_BINS = 36;
    void begin_sweep(int speed_idx, int dir, float wlo, float whi);
    void sample(float theta, float vq, float omega);
    bool end_sweep();

private:
    struct Sweep {
        int   k = -1, dir = 0;
        float lo = 0, hi = 0;
        float vq[SWEEP_BINS], w[SWEEP_BINS];
        uint16_t n[SWEEP_BINS];
        uint32_t total = 0;
    } sw_cur_;
    double   svq_[MAX_SPEEDS][2] = {};
    double   sw_[MAX_SPEEDS][2]  = {};
    double   swt_[MAX_SPEEDS][2] = {};
    uint32_t n_[MAX_SPEEDS][2]   = {};
};

// ===========================================================================
// Hardware abstraction used by the sequencer
// ===========================================================================
class Io {
public:
    virtual ~Io() = default;
    // Open-loop voltage vector (SimpleFOC setPhaseVoltage semantics, COMMANDED volts).
    virtual void  setVoltage(float uq, float ud, float angle_el) = 0;
    // Refresh the sensor and return the unwrapped mechanical angle (rad).
    // false = encoder unhealthy / sample stale -> calibration aborts.
    virtual bool  readAngle(float& theta_m) = 0;
    // Mean DC-link current (A). false = no current sense fitted/configured.
    virtual bool  readCurrent(float& amps) = 0;
    // Measured motor bus voltage (V). Return the configured supply if not measurable.
    virtual float busVoltage() = 0;
    // Block until the next control tick (period = SeqConfig::dt).
    virtual void  tick() = 0;
    // Closed loop (SimpleFOC angle mode) with the given offsets: initFOC().
    // theta0 receives the shaft angle (direction-corrected SimpleFOC frame).
    virtual bool  closedLoopBegin(float zero_electric_angle, int direction, float& theta0) = 0;
    // One closed-loop step towards target (rad): loopFOC()+move(). Returns the
    // COMMANDED Vq that move() produced, the filtered velocity and the angle.
    virtual bool  closedLoopStep(float target, float& vq_cmd, float& velocity, float& theta_m) = 0;
    // De-energise the axis (outputs off).
    virtual void  release() = 0;
};

// ===========================================================================
// Sequencer configuration and per-axis result
// ===========================================================================
enum class Mode : uint8_t { FULL = 0, VERIFY = 1 };

struct SeqConfig {
    float dt              = 0.0005f;  // control tick, s
    float v_supply_cfg    = 7.4f;     // driver.voltage_power_supply used for duty scaling
    int   pole_pairs      = 7;
    // offset + R stage
    float v_levels[OffsetEstimator::MAX_LEVELS] = {1.2f, 1.8f, 2.6f, 0.0f};
    int   n_levels_full   = 3;
    int   n_levels_verify = 1;        // uses v_levels[n_levels_full-1] (highest)
    float ramp_s          = 0.25f;    // amplitude ramp at fixed angle
    float sweep_period_s  = 1.2f;     // sinusoidal sweep period (A = pi)
    float sweep_min_speed_frac = 0.25f; // ignore samples where |da/dt| < frac * peak (stick-slip)
    float settle_s        = 0.12f;
    float dwell_s         = 0.08f;
    float move_s          = 0.25f;    // cosine move between R dwell angles
    // KV stage
    float kv_speeds[BemfEstimator::MAX_SPEEDS] = {1.0f, 2.0f, 3.0f, 4.0f};
    int   n_kv_speeds     = 4;
    float kv_window_rad   = 0.0f;     // 0 -> 6 cogging periods (computed from slots/pp)
    int   stator_slots    = 12;
    float kv_accel        = 40.0f;    // rad/s^2 target acceleration
    float kv_lead_s       = 0.06f;    // constant-speed time before the window opens
    float kv_margin_rad   = 0.03f;    // extra run-out for closed-loop tracking lag
    float kv_max_travel   = 1.6f;     // rad mech, total excursion budget per sweep
    float kv_reposition_speed = 1.0f; // rad/s mech, slow moves between sweeps
    // physics / acceptance
    float r_driver        = 0.0f;     // Ohm, driver Rds(on) per phase (HS ~= LS)
    float r_nominal       = 8.5f;
    float ke_nominal      = 0.035f / 1.5f;
    float r_min_frac      = 0.5f, r_max_frac = 1.8f;
    float ke_min_frac     = 0.6f, ke_max_frac = 1.6f;
    float temp_c          = 25.0f;    // winding temperature estimate (board/IMU temp), NaN = unknown
    OffsetLimits     offset_lim;
    ResistanceLimits r_lim;
    BemfLimits       kv_lim;
};

struct AxisCal {
    uint32_t flags = 0;
    // offset
    float  zero = 0;
    int8_t direction = 0;
    float  pole_pairs_meas = 0;
    float  zero_raw = 0, load_angle = 0, hysteresis = 0, residual_rms = 0;
    // resistance
    float  r_eff = 0, r_phase = 0, r_eff_25 = 0, p0 = 0, r_r2 = 0;
    // back-EMF
    float  ke = 0, kv_rpm_v = 0, kt = 0, v_coulomb = 0, v_static = 0, kv_r2 = 0;
    float  duration_s = 0;
    bool offset_ok() const { return flags & F_OFFSET_OK; }
    bool r_ok()      const { return flags & F_R_OK; }
    bool kv_ok()     const { return flags & F_KV_OK; }
};

// Run the start-up calibration on one axis. Only the axis behind `io` may be
// energised while this runs (the DC-link current sense is shared by all three
// drivers). Returns true if the offset stage succeeded (the axis may run FOC);
// R/KV failures fall back to nominal/stored values and are reported in flags.
// The axis is left released (de-energised).
bool run(Io& io, const SeqConfig& cfg, Mode mode, AxisCal& out);

// Cogging-period window: n_periods * 2pi / lcm(slots, 2pp) mechanical rad.
float cogging_window(int slots, int pole_pairs, int n_periods);

// ===========================================================================
// Persistent record (NVS blob) and boot-time reconciliation
// ===========================================================================
constexpr uint32_t RECORD_MAGIC   = 0x4C41434Du;  // "MCAL"
constexpr uint16_t RECORD_VERSION = 1;
constexpr int      RECORD_AXES    = 3;

struct AxisRecord {
    float   zero;          // rad
    int8_t  direction;     // +1 / -1
    uint8_t pole_pairs;
    uint8_t valid;         // bit0 offset, bit1 R, bit2 KV
    uint8_t reserved;
    float   r_eff_25;      // Ohm @ 25 degC
    float   r_phase_25;    // Ohm @ 25 degC
    float   ke;            // V*s/rad
    float   v_coulomb;     // V
};

struct Record {
    uint32_t   magic;
    uint16_t   version;
    uint16_t   size;
    AxisRecord axis[RECORD_AXES];
    uint32_t   crc;
};

uint32_t crc32(const void* data, size_t len);
void     record_init(Record& r);
void     record_seal(Record& r);          // fill magic/version/size/crc
bool     record_valid(const Record& r);   // magic, version, size, crc, finite fields
bool     axis_record_from(const AxisCal& c, AxisRecord& rec);   // false if offset not OK

struct ReconcileLimits {
    float zero_tol     = 0.35f;   // rad elec: VERIFY single-level offset vs stored
    float r_drift_frac = 0.25f;   // |R25 - stored| / stored -> F_R_DRIFT
};

// VERIFY-mode merge: stored offsets/Ke are authoritative unless the fresh
// measurement disagrees. Returns false when the axis must be re-run in FULL
// mode (zero shifted / direction changed / no stored offset).
bool reconcile(const AxisRecord& stored, const ReconcileLimits& lim, AxisCal& cal);

// Has anything changed enough to justify a flash write? (wear limiting)
bool record_differs(const AxisRecord& a, const AxisRecord& b);

}  // namespace motorcal

#endif  // MOTOR_CAL_H
