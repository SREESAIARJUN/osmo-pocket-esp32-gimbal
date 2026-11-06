// joystick_shaper.cpp — Rev 59.0 joystick command shaping (see joystick_shaper.h)
#include "joystick_shaper.h"

#include <math.h>

namespace joy {

namespace {
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool finite(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }
// Coefficient of a 1-pole low-pass with time constant tau sampled at dt.
inline float lp_alpha(float dt, float tau) { return tau > 0.0f ? 1.0f - expf(-dt / tau) : 1.0f; }
}  // namespace

// ---------------------------------------------------------------------------
// Curves
// ---------------------------------------------------------------------------
float expo_curve(float x, float k) {
    if (!finite(x)) return 0.0f;
    const float ax = fminf(fabsf(x), 1.0f);
    float y;
    if (fabsf(k) < 1e-3f) {
        y = ax;  // limit k -> 0 of the exponential family is the identity
    } else {
        y = expm1f(k * ax) / expm1f(k);  // expm1f: no cancellation for small k*ax
    }
    return copysignf(y, x);
}

float expo_center_gain(float k) {
    return fabsf(k) < 1e-3f ? 1.0f : k / expm1f(k);
}

void radial_deadband(const float in[AXES], float r, float out[AXES]) {
    r = clampf(r, 0.0f, 0.95f);
    const float m = sqrtf(in[0] * in[0] + in[1] * in[1]);
    if (!(m > r)) {  // also catches NaN
        out[0] = out[1] = 0.0f;
        return;
    }
    const float g = (m - r) / ((1.0f - r) * m);
    for (int a = 0; a < AXES; ++a) out[a] = clampf(in[a] * g, -1.0f, 1.0f);
}

float soft_limit_rate(float rate, float pos_rel, float limit, float zone) {
    if (!(limit > 0.0f)) return rate;
    const bool toward = (rate > 0.0f && pos_rel > 0.0f) || (rate < 0.0f && pos_rel < 0.0f);
    if (!toward) return rate;
    const float room = limit - fabsf(pos_rel);
    if (!(zone > 0.0f)) return room > 0.0f ? rate : 0.0f;
    return rate * clampf(room / zone, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Boot centre calibration
// ---------------------------------------------------------------------------
void CenterCalibrator::reset() {
    n_ = 0;
    for (int a = 0; a < AXES; ++a) mean_[a] = m2_[a] = 0.0f;
}

void CenterCalibrator::add(float x_mv, float y_mv) {
    if (n_ == 0xFFFFu) return;
    const float v[AXES] = {x_mv, y_mv};
    ++n_;
    for (int a = 0; a < AXES; ++a) {
        const float d = v[a] - mean_[a];
        mean_[a] += d / (float)n_;
        m2_[a] += d * (v[a] - mean_[a]);
    }
}

float CenterCalibrator::stddev(int axis) const {
    return n_ > 1 ? sqrtf(m2_[axis] / (float)(n_ - 1)) : 0.0f;
}

bool CenterCalibrator::result(const Config& cfg, const Limits& lim, AxisCal cal[AXES]) const {
    bool ok = n_ >= lim.min_samples;
    for (int a = 0; a < AXES; ++a) {
        ok = ok && finite(mean_[a]) && fabsf(mean_[a] - lim.nominal_mv) <= lim.max_offset_mv &&
             stddev(a) <= lim.max_std_mv;
    }
    for (int a = 0; a < AXES; ++a) {
        cal[a].center_mv   = ok ? mean_[a] : lim.nominal_mv;
        cal[a].span_neg_mv = cfg.span_default_mv;
        cal[a].span_pos_mv = cfg.span_default_mv;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Shaper
// ---------------------------------------------------------------------------
void Shaper::configure(const Config& cfg, const AxisCal cal[AXES], bool center_defaulted) {
    cfg_ = cfg;
    center_defaulted_ = center_defaulted;
    const float var0 = cfg.noise_sigma_init * cfg.noise_sigma_init;
    for (int a = 0; a < AXES; ++a) {
        cal_[a] = cal[a];
        if (!(cal_[a].span_neg_mv > 1.0f)) cal_[a].span_neg_mv = cfg.span_default_mv;
        if (!(cal_[a].span_pos_mv > 1.0f)) cal_[a].span_pos_mv = cfg.span_default_mv;
        boot_center_[a] = cal_[a].center_mv;
        u1_[a] = u2_[a] = 0.0f;
        var_[a] = var0;
        z1_[a] = z2_[a] = y_[a] = 0.0f;
    }
    hist_ = 0;
    m_prev_ = 0.0f;
    release_extra_ = 0.0f;
    quiet_s_ = 0.0f;
    ulp_[0] = ulp_[1] = 0.0f;
    fault_ = false;
    fault_ok_s_ = 0.0f;
    out_ = Command{};
}

void Shaper::smooth_axis(int a, float target, float dt) {
    // Two cascaded 1-pole sections (critically damped, step response monotone)
    // followed by a slope clamp on the OUTPUT (= angular acceleration limit).
    // The clamp only ever moves y toward z2, and z2 never passes the target, so
    // the cascade cannot overshoot a step in either direction.
    const float al = lp_alpha(dt, cfg_.smooth_tau_s);
    z1_[a] += al * (target - z1_[a]);
    z2_[a] += al * (z1_[a] - z2_[a]);
    const float dv = cfg_.max_accel[a] > 0.0f ? cfg_.max_accel[a] * dt : 1e30f;
    y_[a] += clampf(z2_[a] - y_[a], -dv, dv);
}

const Command& Shaper::update(float x_mv, float y_mv, float dt, bool precision) {
    // dt sanity: a stalled task must not produce a huge step; a duplicate tick
    // (dt = 0) must not divide by zero.
    if (!(dt > 0.0f)) dt = 1e-4f;
    dt = clampf(dt, 1e-4f, 0.05f);

    const float raw[AXES] = {x_mv, y_mv};
    uint32_t flags = 0;

    // ---- 1. plausibility gate (open wiper / broken GND or VCC -> rail)
    bool plausible = true;
    for (int a = 0; a < AXES; ++a)
        plausible = plausible && finite(raw[a]) && raw[a] >= cfg_.adc_min_valid_mv &&
                    raw[a] <= cfg_.adc_max_valid_mv;
    if (!plausible) {
        fault_ = true;
        fault_ok_s_ = 0.0f;
    } else if (fault_) {
        fault_ok_s_ += dt;
        if (fault_ok_s_ >= cfg_.fault_clear_s) {
            fault_ = false;
            hist_ = 0;  // restart the noise differencer on clean data
        }
    }

    float u[AXES] = {0.0f, 0.0f};
    float shaped[AXES] = {0.0f, 0.0f};
    float radius = cfg_.db_max;

    if (fault_) {
        flags |= FLAG_FAULT;
        quiet_s_ = 0.0f;
        m_prev_ = 0.0f;
        ulp_[0] = ulp_[1] = 0.0f;
    } else {
        // ---- 2. normalise with asymmetric spans; learn spans outward only
        for (int a = 0; a < AXES; ++a) {
            const float e = raw[a] - cal_[a].center_mv;
            float& span = e >= 0.0f ? cal_[a].span_pos_mv : cal_[a].span_neg_mv;
            if (cfg_.learn_span && fabsf(e) > span) span = fminf(fabsf(e), cfg_.span_max_mv);
            u[a] = clampf(e / span, -1.0f, 1.0f);
        }
        const float m = sqrtf(u[0] * u[0] + u[1] * u[1]);

        // ---- 3. noise estimate. Second difference cancels any linear ramp,
        // so a slow deliberate push is not mistaken for noise; for white noise
        // var(d2) = 6 sigma^2. Only updated near the centre (where the deadband
        // matters and the stick is spring-held) and winsorised at 4 sigma(d2),
        // so a thumb flick moves the estimate by a bounded amount while a real
        // noise increase still raises it within a few time constants.
        const float sig_max2 = cfg_.noise_sigma_max * cfg_.noise_sigma_max;
        if (hist_ >= 2 && m < cfg_.db_max) {
            const float al = lp_alpha(dt, cfg_.noise_tau_s);
            for (int a = 0; a < AXES; ++a) {
                const float d2   = u[a] - 2.0f * u1_[a] + u2_[a];
                const float gate = 4.0f * sqrtf(6.0f * var_[a]);
                const float dc   = clampf(d2, -gate, gate);
                var_[a] += al * (dc * dc / 6.0f - var_[a]);
                var_[a] = clampf(var_[a], 1e-10f, sig_max2);
            }
        }
        for (int a = 0; a < AXES; ++a) {
            u2_[a] = u1_[a];
            u1_[a] = u[a];
        }
        if (hist_ < 2) ++hist_;
        const float sigma = sqrtf(var_[0] > var_[1] ? var_[0] : var_[1]);

        // ---- 4. dynamic radius + release guard
        const float r_noise = clampf(cfg_.db_base + cfg_.db_noise_k * sigma, cfg_.db_min, cfg_.db_max);
        const float inward_rate = (m_prev_ - m) / dt;
        if (m < cfg_.release_radius && m_prev_ > r_noise && inward_rate > cfg_.release_rate_per_s) {
            const float want = cfg_.release_radius - r_noise;
            if (want > release_extra_) release_extra_ = want;
        }
        release_extra_ *= expf(-dt / (cfg_.release_tau_s > 1e-4f ? cfg_.release_tau_s : 1e-4f));
        if (release_extra_ < 1e-4f) release_extra_ = 0.0f;
        else flags |= FLAG_RELEASE_GUARD;
        radius = clampf(r_noise + release_extra_, 0.0f, 0.5f);
        m_prev_ = m;

        // ---- 5. centre tracking: stick resting deep inside the deadband.
        // "Resting" is judged on a 0.1 s low-pass of u: testing the raw |u|
        // against r/2 lets a noisy stick reset the hold timer every few ticks,
        // so the centre was never tracked exactly when it mattered most
        // (noise + offset). The instantaneous m < r keeps any real push out.
        const float al_rest = lp_alpha(dt, cfg_.rest_lp_tau_s);
        for (int a = 0; a < AXES; ++a) ulp_[a] += al_rest * (u[a] - ulp_[a]);
        const float m_lp = sqrtf(ulp_[0] * ulp_[0] + ulp_[1] * ulp_[1]);
        if (m_lp < 0.5f * radius && m < radius && release_extra_ == 0.0f) {
            quiet_s_ += dt;
        } else {
            quiet_s_ = 0.0f;
        }
        if (quiet_s_ >= cfg_.center_track_hold_s) {
            flags |= FLAG_CENTER_TRACKING;
            const float al = lp_alpha(dt, cfg_.center_track_tau_s);
            for (int a = 0; a < AXES; ++a) {
                const float c = cal_[a].center_mv + al * (raw[a] - cal_[a].center_mv);
                cal_[a].center_mv = clampf(c, boot_center_[a] - cfg_.center_track_max_mv,
                                           boot_center_[a] + cfg_.center_track_max_mv);
            }
        }

        // ---- 6. radial deadband + 7. exponential curve
        radial_deadband(u, radius, shaped);
        if (shaped[0] == 0.0f && shaped[1] == 0.0f) flags |= FLAG_IN_DEADBAND;
        for (int a = 0; a < AXES; ++a) shaped[a] = expo_curve(shaped[a], cfg_.expo_k[a]);
    }

    // ---- 8. scale + 9. smooth (also runs during a fault: the command eases to 0)
    const float scale = precision ? cfg_.precision_scale : 1.0f;
    if (precision) flags |= FLAG_PRECISION;
    bool settled = true;
    for (int a = 0; a < AXES; ++a) {
        const float target = cfg_.sign[a] * shaped[a] * cfg_.max_rate[a] * scale;
        smooth_axis(a, target, dt);
        // Exponential decay never reaches 0; integrated into a position target
        // that would be a perpetual creep. Snap once everything is negligible.
        if (target == 0.0f && fabsf(y_[a]) < cfg_.settle_eps && fabsf(z1_[a]) < cfg_.settle_eps &&
            fabsf(z2_[a]) < cfg_.settle_eps) {
            z1_[a] = z2_[a] = y_[a] = 0.0f;
        }
        settled = settled && y_[a] == 0.0f && target == 0.0f;
    }
    if (settled) flags |= FLAG_SETTLED;
    if (center_defaulted_) flags |= FLAG_CENTER_DEFAULTED;

    for (int a = 0; a < AXES; ++a) {
        out_.rate[a]      = y_[a];
        out_.shaped[a]    = shaped[a];
        out_.norm[a]      = u[a];
        out_.sigma[a]     = sqrtf(var_[a]);
        out_.center_mv[a] = cal_[a].center_mv;
    }
    out_.radius = radius;
    out_.flags  = flags;
    return out_;
}

}  // namespace joy
