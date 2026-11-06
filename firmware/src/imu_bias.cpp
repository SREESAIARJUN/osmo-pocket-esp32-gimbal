// Gyro temperature-bias compensation — Revision 57.0. See imu_bias.h.
#include "imu_bias.h"
#include <math.h>
#include <string.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// GyroTempModel persistence
// ---------------------------------------------------------------------------
uint32_t GyroTempModel::computeCrc() const {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(this);
    const size_t n = offsetof(GyroTempModel, crc);
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

bool GyroTempModel::valid() const {
    if (magic != MAGIC || crc != computeCrc()) return false;
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(b0[i]) || !isfinite(b1[i]) || !(p00[i] > 0.0f) || !(p11[i] > 0.0f)) return false;
        if (p01[i] * p01[i] > p00[i] * p11[i]) return false;  // covariance must be PSD
    }
    return true;
}

// ---------------------------------------------------------------------------
// StillnessDetector
// ---------------------------------------------------------------------------
void StillnessDetector::reset() {
    n_ = 0;
    for (int i = 0; i < 3; ++i) gs_[i] = gss_[i] = as_[i] = ass_[i] = ah_[i] = 0.0f;
    ts_ = 0.0f;
}

bool StillnessDetector::push(const float gyro[3], const float accel[3], float temp_c, Window& out) {
    if (n_ == 0) {
        for (int i = 0; i < 3; ++i) { g_ref_[i] = gyro[i]; a_ref_[i] = accel[i]; }
    }
    for (int i = 0; i < 3; ++i) {
        const float dg = gyro[i] - g_ref_[i];
        const float da = accel[i] - a_ref_[i];
        gs_[i] += dg; gss_[i] += dg * dg;
        as_[i] += da; ass_[i] += da * da;
    }
    ts_ += temp_c;
    ++n_;
    const uint16_t half = static_cast<uint16_t>(cfg_.window / 2);
    if (n_ == half) {
        for (int i = 0; i < 3; ++i) ah_[i] = as_[i];
    }
    if (n_ < cfg_.window) return false;

    const float n  = static_cast<float>(n_);
    const float nh = static_cast<float>(half);
    bool still = true;
    float am[3], a1[3], a2[3];
    for (int i = 0; i < 3; ++i) {
        const float gm = gs_[i] / n;                     // shifted mean
        float gv = gss_[i] / n - gm * gm;
        if (gv < 0.0f) gv = 0.0f;
        const float amn = as_[i] / n;
        float av = ass_[i] / n - amn * amn;
        if (av < 0.0f) av = 0.0f;
        out.gyro_mean[i] = g_ref_[i] + gm;
        out.gyro_var[i]  = gv;
        am[i] = a_ref_[i] + amn;
        a1[i] = a_ref_[i] + ah_[i] / nh;
        a2[i] = a_ref_[i] + (as_[i] - ah_[i]) / (n - nh);
        if (gv > cfg_.gyro_std_max * cfg_.gyro_std_max) still = false;
        if (av > cfg_.accel_std_max * cfg_.accel_std_max) still = false;
        if (fabsf(out.gyro_mean[i]) > cfg_.mean_rate_max) still = false;
    }
    const float anorm = sqrtf(am[0] * am[0] + am[1] * am[1] + am[2] * am[2]);
    if (fabsf(anorm - 1.0f) > cfg_.accel_norm_tol) still = false;

    // Gravity rotation between halves: |a1 x a2| / (|a1||a2|) = sin(angle)
    const float cx = a1[1] * a2[2] - a1[2] * a2[1];
    const float cy = a1[2] * a2[0] - a1[0] * a2[2];
    const float cz = a1[0] * a2[1] - a1[1] * a2[0];
    const float n1 = sqrtf(a1[0] * a1[0] + a1[1] * a1[1] + a1[2] * a1[2]);
    const float n2 = sqrtf(a2[0] * a2[0] + a2[1] * a2[1] + a2[2] * a2[2]);
    if (n1 > 0.0f && n2 > 0.0f) {
        if (sqrtf(cx * cx + cy * cy + cz * cz) / (n1 * n2) > cfg_.tilt_change_max) still = false;
    } else {
        still = false;
    }

    out.temp_mean = ts_ / n;
    out.n = n_;
    last_still_ = still;
    reset();
    return still;
}

// ---------------------------------------------------------------------------
// GyroTempBias
// ---------------------------------------------------------------------------
void GyroTempBias::reset() {
    for (int i = 0; i < 3; ++i) {
        b0_[i] = 0.0f;
        b1_[i] = 0.0f;
        p00_[i] = cfg_.b0_sigma0 * cfg_.b0_sigma0;
        p01_[i] = 0.0f;
        p11_[i] = cfg_.b1_sigma0 * cfg_.b1_sigma0;
    }
    t_init_ = false;
    seen_ = false;
    updates_ = rejected_ = reanchors_ = 0;
    for (int i = 0; i < 3; ++i) { rej_run_[i] = 0; rej_first_[i] = 0.0f; }
}

float GyroTempBias::filterTemp(float temp_c, float dt) {
    if (!t_init_ || !isfinite(t_filt_)) {
        t_filt_ = temp_c;
        t_init_ = true;
    } else if (dt > 0.0f) {
        const float a = dt / (cfg_.temp_tau_s + dt);
        t_filt_ += a * (temp_c - t_filt_);
    }
    return t_filt_;
}

void GyroTempBias::bias(float temp_c, float out[3]) const {
    const float d = temp_c - imubias::T_REF_C;
    for (int i = 0; i < 3; ++i) out[i] = b0_[i] + b1_[i] * d;
}

void GyroTempBias::predict(float dt) {
    if (!(dt > 0.0f)) return;
    const float c0 = cfg_.b0_sigma0 * cfg_.b0_sigma0, c1 = cfg_.b1_sigma0 * cfg_.b1_sigma0;
    for (int i = 0; i < 3; ++i) {
        p00_[i] += cfg_.q_b0 * dt;
        p11_[i] += cfg_.q_b1 * dt;
        // Never become less certain than the factory prior (long idle periods).
        if (p00_[i] > c0) p00_[i] = c0;
        if (p11_[i] > c1) p11_[i] = c1;
        const float lim = sqrtf(p00_[i] * p11_[i]);
        if (p01_[i] > lim) p01_[i] = lim;
        if (p01_[i] < -lim) p01_[i] = -lim;
    }
}

int GyroTempBias::update(const StillnessDetector::Window& w) {
    if (w.n == 0 || !isfinite(w.temp_mean)) return 0;
    const float d = w.temp_mean - imubias::T_REF_C;
    int accepted = 0;
    for (int i = 0; i < 3; ++i) {
        const float r = w.gyro_var[i] / static_cast<float>(w.n) + cfg_.r_floor;
        // H = [1 d];  PH^T = [p00 + d p01, p01 + d p11];  S = H P H^T + R
        float ph0 = p00_[i] + d * p01_[i];
        float ph1 = p01_[i] + d * p11_[i];
        float s   = ph0 + d * ph1 + r;
        const float y = w.gyro_mean[i] - (b0_[i] + b1_[i] * d);
        if (!isfinite(y) || !(s > 0.0f)) { ++rejected_; continue; }
        if (y * y > cfg_.gate_sigma * cfg_.gate_sigma * s) {
            ++rejected_;
            // Consistent run of rejections = the bias really moved, not an outlier.
            const float spread = cfg_.gate_sigma * sqrtf(r);
            const bool consistent = rej_run_[i] > 0 && (y > 0.0f) == (rej_first_[i] > 0.0f) &&
                                    fabsf(y - rej_first_[i]) < spread;
            if (!consistent) { rej_run_[i] = 1; rej_first_[i] = y; continue; }
            if (++rej_run_[i] < cfg_.reanchor_windows) continue;
            // Re-anchor: make the offset as uncertain as the observed jump.
            p00_[i] += y * y;
            ph0 = p00_[i] + d * p01_[i];
            s   = ph0 + d * ph1 + r;
            ++reanchors_;
        }
        rej_run_[i] = 0;
        const float k0 = ph0 / s, k1 = ph1 / s;
        b0_[i] += k0 * y;
        b1_[i] += k1 * y;
        // P <- P - K S K^T  (symmetric form)
        p00_[i] -= k0 * ph0;
        p01_[i] -= k0 * ph1;
        p11_[i] -= k1 * ph1;
        if (p00_[i] < 1e-14f) p00_[i] = 1e-14f;
        if (p11_[i] < 1e-18f) p11_[i] = 1e-18f;
        ++accepted;
    }
    if (accepted) {
        if (!seen_) { t_min_ = t_max_ = w.temp_mean; seen_ = true; }
        if (w.temp_mean < t_min_) t_min_ = w.temp_mean;
        if (w.temp_mean > t_max_) t_max_ = w.temp_mean;
        ++updates_;
    }
    return accepted;
}

float GyroTempBias::b0Sigma(int i) const { return sqrtf(p00_[i]); }
float GyroTempBias::b1Sigma(int i) const { return sqrtf(p11_[i]); }

void GyroTempBias::exportModel(GyroTempModel& m) const {
    m = GyroTempModel{};
    for (int i = 0; i < 3; ++i) {
        m.b0[i] = b0_[i]; m.b1[i] = b1_[i];
        m.p00[i] = p00_[i]; m.p01[i] = p01_[i]; m.p11[i] = p11_[i];
    }
    m.t_min = t_min_;
    m.t_max = t_max_;
    m.updates = updates_;
    m.seal();
}

bool GyroTempBias::importModel(const GyroTempModel& m, float age_s) {
    if (!m.valid()) return false;
    for (int i = 0; i < 3; ++i) {
        b0_[i] = m.b0[i]; b1_[i] = m.b1[i];
        p00_[i] = m.p00[i]; p01_[i] = m.p01[i]; p11_[i] = m.p11[i];
    }
    t_min_ = m.t_min;
    t_max_ = m.t_max;
    seen_ = m.updates > 0;
    updates_ = m.updates;
    // Bias also drifts with age, and turn-on repeatability is far larger than
    // the in-run random walk: floor the offset sigma so the first still window
    // after boot re-anchors it instead of being gated out (Rev 57 review).
    predict(age_s);
    const float f = cfg_.import_b0_sigma * cfg_.import_b0_sigma;
    for (int i = 0; i < 3; ++i) {
        if (p00_[i] < f) p00_[i] = f;
        rej_run_[i] = 0;
    }
    return true;
}
