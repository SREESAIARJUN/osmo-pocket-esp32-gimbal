// Anti-cogging feed-forward — Revision 8.0 / 150. See anti_cogging.h for the model.
#include "anti_cogging.h"
#include "gimbal_config.h"
#include <math.h>

namespace cogging {

static_assert(fundamental_order(MOTOR_STATOR_SLOTS, MOTOR_POLE_PAIRS) * MOTOR_POLE_PAIRS ==
                  lcm(MOTOR_STATOR_SLOTS, 2 * MOTOR_POLE_PAIRS),
              "cogging fundamental must be an integer electrical order");
static_assert(ANTI_COGGING_ORDER < LUT_SIZE / 4,
              "highest retained harmonic needs >= 4 LUT points per period");

bool HarmonicSet::add(int order, float amp_nm, float phase_rad) {
    if (count >= MAX_HARMONICS || order <= 0 || order >= LUT_SIZE / 2 || order > 255 ||
        !isfinite(amp_nm) || !isfinite(phase_rad))
        return false;
    h[count++] = {static_cast<uint8_t>(order), amp_nm, phase_rad};
    return true;
}

float wrap_2pi(float a) {
    if (!isfinite(a)) return NAN;
    float w = fmodf(a, TWO_PI_F);
    if (w < 0.0f) w += TWO_PI_F;
    // fmodf(-tiny) + 2pi can round up to exactly 2pi in float.
    if (w >= TWO_PI_F) w = 0.0f;
    return w;
}

float electrical_angle(float theta_mech, int pole_pairs, int dir, float zero_electric_angle) {
    if ((dir != 1 && dir != -1) || pole_pairs <= 0) return NAN;
    // Wrap the mechanical angle first: keeps pp*theta small so float
    // resolution does not degrade after many turns (SimpleFOC passes the
    // single-turn angle, but be robust to multi-turn input too).
    const float m = wrap_2pi(theta_mech);
    return wrap_2pi((float)(dir * pole_pairs) * m - zero_electric_angle);
}

// ---------------------------------------------------------------------------
void Lut::clear() {
    for (int i = 0; i <= LUT_SIZE; ++i) lut_[i] = 0.0f;
    hs_.count = 0;
    peak_ = 0.0f;
    valid_ = false;
}

bool Lut::build(const HarmonicSet& hs) {
    if (hs.count == 0) return false;  // an empty set would silently zero the table
    for (uint8_t k = 0; k < hs.count; ++k) {
        const Harmonic& h = hs.h[k];
        if (h.order == 0 || h.order >= LUT_SIZE / 2 || !isfinite(h.amp_nm) || !isfinite(h.phase_rad))
            return false;  // keep the previous table
    }
    float peak = 0.0f;
    for (int i = 0; i < LUT_SIZE; ++i) {
        const double th = (double)i * (2.0 * M_PI) / (double)LUT_SIZE;
        double v = 0.0;
        for (uint8_t k = 0; k < hs.count; ++k)
            v += (double)hs.h[k].amp_nm * sin((double)hs.h[k].order * th + (double)hs.h[k].phase_rad);
        lut_[i] = (float)v;
        if (fabsf(lut_[i]) > peak) peak = fabsf(lut_[i]);
    }
    lut_[LUT_SIZE] = lut_[0];
    hs_ = hs;
    peak_ = peak;
    valid_ = true;
    return true;
}

float Lut::sample(float theta_e) const {
    const float w = wrap_2pi(theta_e);
    if (!valid_ || isnan(w)) return 0.0f;
    const float x = w * ((float)LUT_SIZE / TWO_PI_F);
    int i = (int)x;
    float f = x - (float)i;
    if (i >= LUT_SIZE) { i = LUT_SIZE - 1; f = 1.0f; }  // float edge: w*512/2pi rounding to 512
    return lut_[i] + f * (lut_[i + 1] - lut_[i]);
}

Harmonic extract_harmonic(const float* table, int n, uint8_t order) {
    // x[i] = A sin(k th_i + phi) = A cos(phi) sin(k th) + A sin(phi) cos(k th)
    double s = 0.0, c = 0.0;
    for (int i = 0; i < n; ++i) {
        const double a = (double)order * (double)i * (2.0 * M_PI) / (double)n;
        s += (double)table[i] * sin(a);
        c += (double)table[i] * cos(a);
    }
    s *= 2.0 / n;  // = A cos(phi)
    c *= 2.0 / n;  // = A sin(phi)
    return {order, (float)sqrt(s * s + c * c), (float)atan2(c, s)};
}

// ---------------------------------------------------------------------------
void Calibrator::reset() {
    for (int i = 0; i < LUT_SIZE; ++i) { sum_[i] = 0.0; n_[i] = 0; }
}

void Calibrator::accumulate(float theta_e, float vq_hold) {
    const float w = wrap_2pi(theta_e);
    if (isnan(w) || !isfinite(vq_hold)) return;
    // Nearest bin (round, not floor) so bin i is centred on theta_i like the LUT.
    const int b = (int)lroundf(w * ((float)LUT_SIZE / TWO_PI_F)) & LUT_MASK;
    sum_[b] += (double)vq_hold;
    ++n_[b];
}

float Calibrator::coverage() const {
    int filled = 0;
    for (int i = 0; i < LUT_SIZE; ++i) filled += n_[i] ? 1 : 0;
    return (float)filled / (float)LUT_SIZE;
}

bool Calibrator::fit(int fundamental, int max_order, float kt, float phase_r,
                     HarmonicSet& out, float min_coverage) const {
    out.count = 0;
    if (fundamental <= 0 || max_order < fundamental || max_order >= LUT_SIZE / 4) return false;
    if (!(kt > 0.0f) || !(phase_r > 0.0f) || coverage() < min_coverage) return false;

    // Bin means converted to cogging torque; empty bins are filled by circular
    // linear interpolation between the nearest populated neighbours so the
    // uniform-grid DFT stays unbiased for small gaps.
    static float tau[LUT_SIZE];  // static: 2 KiB off the (small) task stack; fit() is not re-entrant
    const double scale = -(double)kt / (double)phase_r;
    int first = -1;
    for (int i = 0; i < LUT_SIZE; ++i)
        if (n_[i]) { tau[i] = (float)(scale * sum_[i] / n_[i]); if (first < 0) first = i; }
    if (first < 0) return false;
    for (int i = 0; i < LUT_SIZE; ++i) {
        if (n_[i]) continue;
        int lo = i, hi = i, dl = 0, dh = 0;
        while (!n_[lo & LUT_MASK]) { --lo; ++dl; }
        while (!n_[hi & LUT_MASK]) { ++hi; ++dh; }
        const float a = tau[lo & LUT_MASK], b = tau[hi & LUT_MASK];
        tau[i] = a + (b - a) * (float)dl / (float)(dl + dh);
    }

    for (int k = fundamental; k <= max_order && out.count < MAX_HARMONICS; k += fundamental) {
        const Harmonic h = extract_harmonic(tau, LUT_SIZE, (uint8_t)k);
        out.add(h.order, h.amp_nm, h.phase_rad);
    }
    return out.count > 0;
}

// ---------------------------------------------------------------------------
float feedforward_vq(const Lut& lut, const FeedforwardParams& p,
                     float theta_e, float omega_e, float latency_s) {
    if (!lut.valid() || !isfinite(theta_e) || !isfinite(omega_e) || !(p.kt > 0.0f)) return 0.0f;
    const float sp = fabsf(omega_e);
    if (sp >= p.fade_end) return 0.0f;
    float gain = 1.0f;
    if (sp > p.fade_start && p.fade_end > p.fade_start)
        gain = (p.fade_end - sp) / (p.fade_end - p.fade_start);

    if (!(latency_s > 0.0f) || !isfinite(latency_s)) latency_s = 0.0f;
    const float tau_cog = lut.sample(theta_e + omega_e * latency_s);
    float vq = -tau_cog * p.phase_r / p.kt * gain;
    if (vq > p.vq_limit) vq = p.vq_limit;
    if (vq < -p.vq_limit) vq = -p.vq_limit;
    return vq;
}

}  // namespace cogging

// ===========================================================================
// Firmware-facing API
// ===========================================================================
static constexpr uint8_t kAxes = 3;
static cogging::Lut              g_lut[kAxes];
static cogging::FeedforwardParams g_ff;

void init_anti_cogging() {
    using namespace cogging;
    constexpr int k0 = fundamental_order(MOTOR_STATOR_SLOTS, MOTOR_POLE_PAIRS);
    static_assert(k0 == 12, "seed coefficients below are for the 12N14P 12th/24th electrical harmonics");
    static_assert(2 * k0 <= ANTI_COGGING_ORDER, "ANTI_COGGING_ORDER must retain the 2nd slot harmonic");

    g_ff.kt         = MOTOR_KT;
    g_ff.phase_r    = MOTOR_PHASE_R;
    g_ff.vq_limit   = ANTI_COGGING_VQ_LIMIT;
    g_ff.fade_start = ANTI_COGGING_FADE_START_RAD_S;
    g_ff.fade_end   = ANTI_COGGING_FADE_END_RAD_S;

    // Factory seed model (cogging torque, N*m) until a per-unit calibration is loaded.
    const float seed[kAxes][2] = {
        {COG_SEED_PAN_H1_NM,   COG_SEED_PAN_H2_NM},
        {COG_SEED_ROLL_H1_NM,  COG_SEED_ROLL_H2_NM},
        {COG_SEED_PITCH_H1_NM, COG_SEED_PITCH_H2_NM},
    };
    for (uint8_t a = 0; a < kAxes; ++a) {
        HarmonicSet hs;
        hs.add(k0,     seed[a][0], COG_SEED_PHASE_RAD);
        hs.add(2 * k0, seed[a][1], COG_SEED_PHASE_RAD);
        g_lut[a].build(hs);
    }
}

bool anti_cogging_load(uint8_t axis, const cogging::HarmonicSet& hs) {
    if (axis >= kAxes) return false;
    for (uint8_t k = 0; k < hs.count; ++k)
        if (hs.h[k].order > ANTI_COGGING_ORDER) return false;
    return g_lut[axis].build(hs);
}

const cogging::Lut* anti_cogging_lut(uint8_t axis) {
    return axis < kAxes ? &g_lut[axis] : nullptr;
}

float get_anti_cogging_voltage(uint8_t axis, float theta_e, float omega_e, float latency_s) {
    if (axis >= kAxes) return 0.0f;
    return cogging::feedforward_vq(g_lut[axis], g_ff, theta_e, omega_e, latency_s);
}
