/**
 * joystick_shaper.h — Thumb-joystick command shaping for handheld cinematography
 *                     (Revision 59.0)
 *
 * Converts two raw ADC readings (mV) from a spring-centred analog thumbstick
 * into smooth pan / pitch RATE commands (rad/s). The pipeline, per 200 Hz tick:
 *
 *   x_mv,y_mv ─► plausibility / open-wire fault gate
 *            ─► normalise: u = (x - c) / span±          (asymmetric spans, learned outward)
 *            ─► noise estimate sigma (2nd difference, winsorised EWMA, gated near centre)
 *            ─► DYNAMIC radial deadband r = clamp(base + k*sigma) + release guard
 *            ─► scaled-radial deadband  s = u * (|u| - r) / ((1 - r) |u|)  (continuous, no cross)
 *            ─► exponential curve       f(s) = sign(s) (e^{k|s|} - 1) / (e^k - 1)
 *            ─► scale to max rate (precision mode x JOY_PRECISION_SCALE)
 *            ─► 2-pole critically-damped ease + acceleration limit (monotone, no overshoot)
 *            ─► snap-to-zero once settled at rest (no target creep when released)
 *
 * Why each stage exists (handheld cinematography failure modes):
 *  - Fixed deadbands are either too small for a worn/noisy stick (the gimbal
 *    "breathes" when nobody touches it) or too large for a good one (dead feel).
 *    The radius here follows the measured noise of THIS stick on THIS board,
 *    including motor-PWM coupling that only appears once the drivers run.
 *  - Per-axis deadbands produce a "cross": diagonal moves start one axis
 *    before the other. The radial scaled deadband starts both together and its
 *    output is continuous (0 at the edge), so leaving the deadband never kicks.
 *  - Spring-return sticks overshoot centre and ring for ~50-100 ms after
 *    release; that produced a small reverse "bounce" in the shot. A fast inward
 *    return to the centre region transiently widens the deadband (release
 *    guard) and the widening decays smoothly (no time discontinuity).
 *  - Pot centres drift with temperature and wear; when the stick has rested
 *    deep inside the deadband for JOY_CENTER_TRACK_HOLD_S the centre tracks
 *    slowly (bounded), so the deadband margin is preserved all day.
 *  - Linear stick response makes slow, precise framing moves impossible; the
 *    exponential curve gives slope k/(e^k - 1) at the centre (0.157 for k = 3)
 *    while still reaching full rate at full deflection.
 *  - Raw stick motion has unbounded jerk; the 2-pole ease plus acceleration
 *    clamp gives "fluid head" starts and stops that never overshoot.
 *
 * Pure single-precision math, no Arduino dependency: host-tested in
 * test/host/test_joystick.cpp with -Wconversion -Wdouble-promotion.
 */
#ifndef JOYSTICK_SHAPER_H
#define JOYSTICK_SHAPER_H

#include <stdint.h>

namespace joy {

constexpr int AXES = 2;   // 0 = X (pan), 1 = Y (pitch)

enum Flags : uint32_t {
    FLAG_FAULT            = 1u << 0,  // reading implausible (open wire / short); command forced to 0
    FLAG_IN_DEADBAND      = 1u << 1,
    FLAG_RELEASE_GUARD    = 1u << 2,  // deadband transiently widened after a spring release
    FLAG_CENTER_TRACKING  = 1u << 3,  // centre being re-estimated this tick
    FLAG_CENTER_DEFAULTED = 1u << 4,  // boot calibration rejected, factory centre in use
    FLAG_PRECISION        = 1u << 5,
    FLAG_SETTLED          = 1u << 6,  // smoother at exact zero (safe to treat as "no command")
};

struct AxisCal {
    float center_mv;
    float span_neg_mv;   // centre - reading at full negative deflection (> 0)
    float span_pos_mv;   // reading at full positive deflection - centre (> 0)
};

struct Config {
    // --- input plausibility (open wiper floats to a rail through the RC filter)
    float adc_min_valid_mv = 80.0f;
    float adc_max_valid_mv = 3220.0f;
    float fault_clear_s    = 0.25f;     // reading must be plausible this long to clear a fault
    // --- spans
    float span_default_mv = 1400.0f;    // half-travel for |u| = 1; < electrical travel => outer zone
    float span_max_mv     = 1700.0f;    // learned spans never exceed this
    bool  learn_span      = true;
    // --- dynamic deadband (normalised units)
    float db_base    = 0.025f;          // mechanical centring tolerance
    float db_noise_k = 5.0f;            // radius += k * sigma
    float db_min     = 0.03f;
    float db_max     = 0.20f;
    float noise_tau_s      = 1.5f;      // noise EWMA time constant
    float noise_sigma_init = 0.004f;
    float noise_sigma_max  = 0.05f;
    // --- release guard
    float release_rate_per_s = 4.0f;    // inward |u| speed that marks a spring release
    float release_radius     = 0.22f;   // radius the deadband is widened to
    float release_tau_s      = 0.12f;   // exponential decay of the widening
    // --- centre tracking
    float center_track_tau_s  = 3.0f;
    float center_track_hold_s = 0.75f;
    float center_track_max_mv = 120.0f; // bound around the boot-calibrated centre
    float rest_lp_tau_s       = 0.10f;  // low-pass on u for the "resting" test (noise-immune)
    // --- curve / output (per axis)
    float expo_k[AXES]    = {3.0f, 2.5f};      // 0 = linear, > 0 softer centre, < 0 sharper
    float max_rate[AXES]  = {1.5708f, 1.0472f}; // rad/s at full deflection (90 / 60 deg/s)
    float max_accel[AXES] = {3.1416f, 2.0944f}; // rad/s^2 (180 / 120 deg/s^2)
    float sign[AXES]      = {1.0f, 1.0f};       // -1 inverts an axis
    float smooth_tau_s    = 0.08f;              // per pole of the 2-pole ease
    float precision_scale = 0.30f;
    float settle_eps      = 1e-4f;              // rad/s: below this at zero target -> exact 0
};

struct Command {
    float    rate[AXES];     // rad/s, smoothed: the value to integrate into the axis target
    float    shaped[AXES];   // [-1, 1] after deadband + expo (before scale/smoothing)
    float    norm[AXES];     // [-1, 1] normalised raw deflection
    float    sigma[AXES];    // noise estimate, normalised units
    float    center_mv[AXES];
    float    radius;         // effective deadband radius this tick
    uint32_t flags;
    uint32_t t_us;           // stamped by the owning task
};

/** f(x) = sign(x) (e^{k|x|} - 1) / (e^k - 1), |x| clamped to 1. k ~ 0 -> identity. */
float expo_curve(float x, float k);

/** Slope of expo_curve at the origin: k / (e^k - 1) (1 for k = 0). */
float expo_center_gain(float k);

/**
 * Scaled radial deadband. Output = u (|u| - r) / ((1 - r) |u|) per axis,
 * then each axis clamped to [-1, 1]. Continuous everywhere (0 on the circle
 * |u| = r), full per-axis scale preserved on the axes and on diagonals.
 */
void radial_deadband(const float in[AXES], float r, float out[AXES]);

/**
 * Soft travel limit for an integrated rate command: rate is unchanged moving
 * away from a limit, and scaled by clamp((limit - |pos|) / zone, 0, 1) moving
 * toward it, so the axis decelerates into the stop instead of hitting it.
 * limit <= 0 disables the limit.
 */
float soft_limit_rate(float rate, float pos_rel, float limit, float zone);

/** Boot centre calibration: Welford mean/stdev of N samples per axis. */
class CenterCalibrator {
public:
    struct Limits {
        float nominal_mv    = 1650.0f;  // VDD/2 of the 3V3 pot
        float max_offset_mv = 350.0f;   // |mean - nominal| above: stick deflected or wiring fault
        float max_std_mv    = 20.0f;    // above: thumb on the stick during boot
        uint16_t min_samples = 32;
    };
    void reset();
    void add(float x_mv, float y_mv);
    uint16_t count() const { return n_; }
    float mean(int axis) const { return mean_[axis]; }
    float stddev(int axis) const;
    /** Fills @p cal (spans = cfg defaults). Returns false and the nominal centre if rejected. */
    bool result(const Config& cfg, const Limits& lim, AxisCal cal[AXES]) const;

private:
    uint16_t n_ = 0;
    float mean_[AXES] = {0, 0};
    float m2_[AXES]   = {0, 0};
};

class Shaper {
public:
    Shaper() = default;
    void configure(const Config& cfg, const AxisCal cal[AXES], bool center_defaulted);

    /** One tick. @p dt in seconds (sanitised internally). */
    const Command& update(float x_mv, float y_mv, float dt, bool precision);

    const Command& last() const { return out_; }
    const AxisCal& cal(int axis) const { return cal_[axis]; }
    const Config&  config() const { return cfg_; }

private:
    void smooth_axis(int a, float target, float dt);

    Config  cfg_{};
    AxisCal cal_[AXES] = {{1650.0f, 1400.0f, 1400.0f}, {1650.0f, 1400.0f, 1400.0f}};
    float   boot_center_[AXES] = {1650.0f, 1650.0f};
    bool    center_defaulted_ = false;

    // noise estimator
    float u1_[AXES] = {0, 0}, u2_[AXES] = {0, 0};
    uint8_t hist_ = 0;               // valid history depth (0..2)
    float var_[AXES] = {0, 0};

    // deadband state
    float m_prev_ = 0.0f;
    float release_extra_ = 0.0f;
    float quiet_s_ = 0.0f;
    float ulp_[AXES] = {0, 0};       // low-passed deflection for the resting test

    // fault
    bool  fault_ = false;
    float fault_ok_s_ = 0.0f;

    // smoother per axis
    float z1_[AXES] = {0, 0}, z2_[AXES] = {0, 0}, y_[AXES] = {0, 0};

    Command out_{};
};

}  // namespace joy

#endif  // JOYSTICK_SHAPER_H
