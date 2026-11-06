// Motor auto-calibration — Revision 58.0 / 150. See motor_cal.h for the physics.
#include "motor_cal.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <new>

namespace motorcal {

// ===========================================================================
// Helpers
// ===========================================================================
float wrap_2pi(float a) {
    if (!isfinite(a)) return NAN;
    float r = fmodf(a, TWO_PI_F);
    if (r < 0.0f) r += TWO_PI_F;
    if (r >= TWO_PI_F) r = 0.0f;  // -tiny + 2pi rounds to 2pi
    return r;
}

float wrap_pi(float a) {
    if (!isfinite(a)) return NAN;
    float r = wrap_2pi(a + PI_F) - PI_F;
    if (r <= -PI_F) r += TWO_PI_F;
    return r;
}

float kv_from_ke(float ke) { return (ke > 0.0f && isfinite(ke)) ? 1.0f / (SQRT3_F * ke * RPM_TO_RADS) : NAN; }
float ke_from_kv(float kv) { return (kv > 0.0f && isfinite(kv)) ? 1.0f / (SQRT3_F * kv * RPM_TO_RADS) : NAN; }

float r_to_ref_temp(float r, float temp_c) {
    if (!isfinite(temp_c)) return r;
    return r / (1.0f + CU_ALPHA * (temp_c - R_REF_TEMP_C));
}
float r_from_ref_temp(float r25, float temp_c) {
    if (!isfinite(temp_c)) return r25;
    return r25 * (1.0f + CU_ALPHA * (temp_c - R_REF_TEMP_C));
}

static inline int sgn(float x) { return x > 0.0f ? 1 : (x < 0.0f ? -1 : 0); }

const char* flags_str(uint32_t flags, char* buf, size_t len) {
    static const struct { uint32_t bit; const char* name; } kNames[] = {
        {F_OFFSET_OK, "OFFSET"}, {F_R_OK, "R"}, {F_KV_OK, "KV"},
        {F_NO_SENSOR, "NO_SENSOR"}, {F_STALL, "STALL"}, {F_POLE_PAIRS, "POLE_PAIRS"},
        {F_OFFSET_NOISY, "OFFSET_NOISY"}, {F_HEAVY_LOAD, "HEAVY_LOAD"},
        {F_ZERO_SHIFTED, "ZERO_SHIFTED"}, {F_DIR_CHANGED, "DIR_CHANGED"},
        {F_R_NO_SENSE, "R_NO_SENSE"}, {F_R_POOR_FIT, "R_POOR_FIT"},
        {F_R_OUT_OF_RANGE, "R_OUT_OF_RANGE"}, {F_R_DRIFT, "R_DRIFT"},
        {F_KV_SKIPPED, "KV_FROM_NVS"}, {F_KV_POOR_FIT, "KV_POOR_FIT"},
        {F_KV_OUT_OF_RANGE, "KV_OUT_OF_RANGE"}, {F_KV_TRAVEL, "KV_TRAVEL"},
        {F_CLOSED_LOOP_FAIL, "CLOSED_LOOP_FAIL"}, {F_ABORTED, "ABORTED"},
    };
    if (!buf || len == 0) return "";
    buf[0] = '\0';
    size_t used = 0;
    for (const auto& n : kNames) {
        if (!(flags & n.bit)) continue;
        const int w = snprintf(buf + used, len - used, "%s%s", used ? "|" : "", n.name);
        if (w < 0 || (size_t)w >= len - used) break;
        used += (size_t)w;
    }
    return buf;
}

float cogging_window(int slots, int pole_pairs, int n_periods) {
    if (slots <= 0 || pole_pairs <= 0 || n_periods <= 0) return NAN;
    int a = slots, b = 2 * pole_pairs;
    while (b) { const int t = a % b; a = b; b = t; }
    const int lcm = slots / a * (2 * pole_pairs);
    return (float)n_periods * TWO_PI_F / (float)lcm;
}

// ===========================================================================
// 1. Offset estimator
// ===========================================================================
void OffsetEstimator::reset() {
    n_ = 0;
    for (int i = 0; i < MAX_LEVELS; ++i) { v_sum_[i] = 0.0f; v_n_[i] = 0; }
}

bool OffsetEstimator::add(int level, float v_amp, float a_cmd, float theta_m, int sweep_dir) {
    if (n_ >= MAX_POINTS || level < 0 || level >= MAX_LEVELS || sweep_dir == 0) return false;
    if (!isfinite(v_amp) || v_amp <= 0.0f || !isfinite(a_cmd) || !isfinite(theta_m)) return false;
    a_[n_] = a_cmd;
    m_[n_] = theta_m;
    lvl_[n_] = (int8_t)level;
    dir_[n_] = (int8_t)(sweep_dir > 0 ? 1 : -1);
    ++n_;
    v_sum_[level] += v_amp;
    ++v_n_[level];
    return true;
}

bool OffsetEstimator::solve(int pp, const OffsetLimits& lim, OffsetResult& out) const {
    out = OffsetResult{};
    out.points = (uint16_t)n_;
    if (pp <= 0 || n_ < 8) { out.flags |= F_STALL; return false; }

    // --- direction and pole pairs: regression theta_m = s * a + c over all points
    double ma = 0, mm = 0;
    for (int i = 0; i < n_; ++i) { ma += (double)a_[i]; mm += (double)m_[i]; }
    ma /= n_; mm /= n_;
    double saa = 0, sam = 0;
    for (int i = 0; i < n_; ++i) {
        const double da = (double)a_[i] - ma;
        saa += da * da;
        sam += da * ((double)m_[i] - mm);
    }
    if (saa < 1e-6) { out.flags |= F_STALL; return false; }
    const double slope = sam / saa;
    if (fabs(slope) < (double)lim.stall_fraction / pp) { out.flags |= F_STALL; return false; }
    out.direction = slope > 0 ? 1 : -1;
    const float dpp = (float)(out.direction * pp);

    // --- per-level forward/reverse circular means of e = dir*pp*theta_m - a
    double cs[MAX_LEVELS][2] = {}, sn[MAX_LEVELS][2] = {};
    int    cnt[MAX_LEVELS][2] = {};
    for (int i = 0; i < n_; ++i) {
        const float e = dpp * m_[i] - a_[i];
        const int d = dir_[i] > 0 ? 0 : 1;
        cs[lvl_[i]][d] += cos((double)e);
        sn[lvl_[i]][d] += sin((double)e);
        ++cnt[lvl_[i]][d];
    }
    float phi[MAX_LEVELS], hyst[MAX_LEVELS], volt[MAX_LEVELS];
    bool  used[MAX_LEVELS] = {};
    int   k = 0, ref = -1;
    for (int l = 0; l < MAX_LEVELS; ++l) {
        if (cnt[l][0] < 3 || cnt[l][1] < 3 || v_n_[l] == 0) continue;
        const float pf = (float)atan2(sn[l][0], cs[l][0]);
        const float pr = (float)atan2(sn[l][1], cs[l][1]);
        const float d  = wrap_pi(pf - pr);
        phi[l]  = wrap_2pi(pr + 0.5f * d);
        hyst[l] = 0.5f * d;
        volt[l] = v_sum_[l] / (float)v_n_[l];
        used[l] = true;
        ++k;
        if (ref < 0 || volt[l] > volt[ref]) ref = l;
#ifdef MOTORCAL_DEBUG
        printf("    lvl %d V=%.3f phi=%.4f hyst=%.4f nf=%d nr=%d\n", l, (double)volt[l], (double)phi[l], (double)hyst[l], cnt[l][0], cnt[l][1]);
#endif
    }
    if (k == 0) { out.flags |= F_STALL; return false; }
    out.levels = (uint8_t)k;

    // --- pole pairs. A position-dependent load (gravity gradient g = dtau/dtheta)
    // softens the vector spring k_V ~ V, so the apparent pp(V) = pp*(1 + g'/V):
    // fit pp_l against 1/V_l and take the intercept (same idea as the offset).
    {
        double ppl[MAX_LEVELS], xv[MAX_LEVELS];
        int m = 0;
        for (int l = 0; l < MAX_LEVELS; ++l) {
            if (!used[l]) continue;
            double a0 = 0, m0 = 0;
            int nl = 0;
            for (int i = 0; i < n_; ++i) if (lvl_[i] == l) { a0 += (double)a_[i]; m0 += (double)m_[i]; ++nl; }
            a0 /= nl; m0 /= nl;
            double s_aa = 0, s_am = 0;
            for (int i = 0; i < n_; ++i) if (lvl_[i] == l) {
                const double da = (double)a_[i] - a0;
                s_aa += da * da; s_am += da * ((double)m_[i] - m0);
            }
            if (s_aa < 1e-9 || fabs(s_am) < 1e-12) continue;
            ppl[m] = fabs(s_aa / s_am);
            xv[m]  = 1.0 / (double)volt[l];
            ++m;
        }
        double ppm = 1.0 / fabs(slope);
        if (m == 1) ppm = ppl[0];
        if (m >= 2) {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (int i = 0; i < m; ++i) { sx += xv[i]; sy += ppl[i]; sxx += xv[i] * xv[i]; sxy += xv[i] * ppl[i]; }
            const double det = m * sxx - sx * sx;
            if (det > 1e-12) ppm = (sy - (m * sxy - sx * sy) / det * sx) / m;
        }
        out.pole_pairs_meas = (float)ppm;
        if (!(fabsf(out.pole_pairs_meas - (float)pp) <= lim.pp_tolerance)) {
            out.flags |= F_POLE_PAIRS;
            return false;
        }
    }

    // --- static-load extrapolation: sin(u_l - z) = c / V_l  (u relative to the ref level)
    double z = 0.0, c = 0.0;
    if (k >= 2) {
        double x[MAX_LEVELS], u[MAX_LEVELS];
        int m = 0;
        for (int l = 0; l < MAX_LEVELS; ++l) {
            if (!used[l]) continue;
            x[m] = 1.0 / (double)volt[l];
            u[m] = wrap_pi(phi[l] - phi[ref]);
            ++m;
        }
        // Linear start: u = z + c*x
        double sx = 0, su = 0, sxx = 0, sxu = 0;
        for (int i = 0; i < m; ++i) { sx += x[i]; su += u[i]; sxx += x[i] * x[i]; sxu += x[i] * u[i]; }
        const double det = m * sxx - sx * sx;
        if (det > 1e-12) {
            c = (m * sxu - sx * su) / det;
            z = (su - c * sx) / m;
            // Gauss-Newton on r_i = sin(u_i - z) - c*x_i
            for (int it = 0; it < 12; ++it) {
                double jzz = 0, jzc = 0, jcc = 0, gz = 0, gc = 0;
                for (int i = 0; i < m; ++i) {
                    const double r  = sin(u[i] - z) - c * x[i];
                    const double dz = -cos(u[i] - z);
                    const double dc = -x[i];
                    jzz += dz * dz; jzc += dz * dc; jcc += dc * dc;
                    gz  += dz * r;  gc  += dc * r;
                }
                const double dd = jzz * jcc - jzc * jzc;
                if (fabs(dd) < 1e-18) break;
                const double stz = -( jcc * gz - jzc * gc) / dd;
                const double stc = -(-jzc * gz + jzz * gc) / dd;
                z += stz; c += stc;
                if (fabs(stz) < 1e-7 && fabs(stc) < 1e-9) break;
            }
            if (!isfinite(z) || !isfinite(c) || fabs(z) > 1.4) {  // > 80 deg: model invalid
                z = 0.0; c = 0.0;
                out.flags |= F_HEAVY_LOAD;
            }
        }
    }
    out.zero_raw   = phi[ref];
    out.zero       = wrap_2pi(phi[ref] + (float)z);
    out.load_angle = (float)-z;                 // rotor displacement at the highest amplitude
    out.load_coeff = (float)c;
    if (fabsf(out.load_angle) > lim.max_load_angle) out.flags |= F_HEAVY_LOAD;

    float hs = 0.0f;
    for (int l = 0; l < MAX_LEVELS; ++l) if (used[l]) hs += fabsf(hyst[l]);
    out.hysteresis = hs / (float)k;

    // --- residual: each point against its own level/direction mean
    double ss = 0;
    int    ns = 0;
    for (int i = 0; i < n_; ++i) {
        const int l = lvl_[i];
        if (!used[l]) continue;
        const float pred = phi[l] + (dir_[i] > 0 ? hyst[l] : -hyst[l]);
        const float r = wrap_pi(dpp * m_[i] - a_[i] - pred);
        ss += (double)r * (double)r;
        ++ns;
    }
    out.residual_rms = ns ? (float)sqrt(ss / ns) : 0.0f;
    if (out.residual_rms > lim.max_residual_rms) { out.flags |= F_OFFSET_NOISY; return false; }

    out.ok = true;
    out.flags |= F_OFFSET_OK;
    return true;
}

// ===========================================================================
// 2. Resistance estimator
// ===========================================================================
void ResistanceEstimator::reset() { sx_ = sy_ = sxx_ = sxy_ = syy_ = 0.0; n_ = 0; }

void ResistanceEstimator::add(float v_amp, float vbus, float i_dc) {
    if (!isfinite(v_amp) || !isfinite(vbus) || !isfinite(i_dc) || v_amp < 0.0f || vbus <= 0.0f) return;
    const double x = 1.5 * (double)v_amp * (double)v_amp;
    const double y = (double)vbus * (double)i_dc;
    sx_ += x; sy_ += y; sxx_ += x * x; sxy_ += x * y; syy_ += y * y;
    ++n_;
}

bool ResistanceEstimator::solve(float r_driver, const ResistanceLimits& lim, ResistanceResult& out) const {
    out = ResistanceResult{};
    out.n = (uint16_t)n_;
    if (n_ < 3) { out.flags |= F_R_POOR_FIT; return false; }
    const double n   = (double)n_;
    const double vxx = n * sxx_ - sx_ * sx_;
    const double vyy = n * syy_ - sy_ * sy_;
    const double vxy = n * sxy_ - sx_ * sy_;
    if (vxx <= 0.0 || sqrt(vxx) / n < (double)lim.min_x_std) { out.flags |= F_R_POOR_FIT; return false; }
    const double slope = vxy / vxx;
    out.p0 = (float)((sy_ - slope * sx_) / n);
    out.r2 = vyy > 0.0 ? (float)(vxy * vxy / (vxx * vyy)) : 0.0f;
    if (slope <= 0.0 || out.r2 < lim.min_r2) { out.flags |= F_R_POOR_FIT; return false; }
    out.r_eff   = (float)(1.0 / slope);
    out.r_phase = out.r_eff - r_driver;
    if (!isfinite(out.r_eff) || out.r_phase <= 0.0f) { out.flags |= F_R_POOR_FIT; return false; }
    out.ok = true;
    return true;
}

// ===========================================================================
// 3. Back-EMF estimator
// ===========================================================================
void BemfEstimator::reset() {
    memset(svq_, 0, sizeof svq_); memset(sw_, 0, sizeof sw_); memset(swt_, 0, sizeof swt_); memset(n_, 0, sizeof n_);
}

void BemfEstimator::add(int k, int dir, float vq, float omega, float weight) {
    if (k < 0 || k >= MAX_SPEEDS || dir == 0 || !isfinite(vq) || !isfinite(omega)) return;
    if (!isfinite(weight) || weight < 0.0f) return;
    const int d = dir > 0 ? 0 : 1;
    svq_[k][d] += (double)weight * (double)vq;
    sw_[k][d]  += (double)weight * (double)omega;
    swt_[k][d] += (double)weight;
    ++n_[k][d];
}

void BemfEstimator::begin_sweep(int k, int dir, float wlo, float whi) {
    sw_cur_.k = (k >= 0 && k < MAX_SPEEDS && dir != 0 && whi > wlo) ? k : -1;
    sw_cur_.dir = dir > 0 ? 1 : -1;
    sw_cur_.lo = wlo;
    sw_cur_.hi = whi;
    sw_cur_.total = 0;
    for (int i = 0; i < SWEEP_BINS; ++i) { sw_cur_.vq[i] = 0.0f; sw_cur_.w[i] = 0.0f; sw_cur_.n[i] = 0; }
}

void BemfEstimator::sample(float theta, float vq, float omega) {
    if (sw_cur_.k < 0 || !isfinite(theta) || !isfinite(vq) || !isfinite(omega)) return;
    if (theta < sw_cur_.lo || theta >= sw_cur_.hi) return;
    int b = (int)((theta - sw_cur_.lo) / (sw_cur_.hi - sw_cur_.lo) * (float)SWEEP_BINS);
    if (b < 0) b = 0;
    if (b >= SWEEP_BINS) b = SWEEP_BINS - 1;
    sw_cur_.vq[b] += vq;
    sw_cur_.w[b]  += omega;
    if (sw_cur_.n[b] < 0xFFFF) ++sw_cur_.n[b];
    ++sw_cur_.total;
}

bool BemfEstimator::end_sweep() {
    if (sw_cur_.k < 0) return false;
    double mv = 0.0, mw = 0.0;
    for (int i = 0; i < SWEEP_BINS; ++i) {
        if (sw_cur_.n[i] == 0) { sw_cur_.k = -1; return false; }
        mv += (double)sw_cur_.vq[i] / sw_cur_.n[i];
        mw += (double)sw_cur_.w[i] / sw_cur_.n[i];
    }
    const int d = sw_cur_.dir > 0 ? 0 : 1;
    svq_[sw_cur_.k][d] += mv / SWEEP_BINS;
    sw_[sw_cur_.k][d]  += mw / SWEEP_BINS;
    swt_[sw_cur_.k][d] += 1.0;
    n_[sw_cur_.k][d]   += sw_cur_.total;
    sw_cur_.k = -1;
    return true;
}

uint32_t BemfEstimator::count(int k, int dir) const {
    return (k < 0 || k >= MAX_SPEEDS) ? 0 : n_[k][dir > 0 ? 0 : 1];
}

bool BemfEstimator::solve(const BemfLimits& lim, BemfResult& out) const {
    out = BemfResult{};
    double w[MAX_SPEEDS], d[MAX_SPEEDS], s[MAX_SPEEDS];
    int m = 0;
    for (int k = 0; k < MAX_SPEEDS; ++k) {
        const uint32_t np = n_[k][0], nn = n_[k][1];
        if ((int)np < lim.min_samples || (int)nn < lim.min_samples) continue;
        const double big = np > nn ? np : nn;
        if (fabs((double)np - (double)nn) / big > (double)lim.max_count_asym) continue;
        if (!(swt_[k][0] > 0.0) || !(swt_[k][1] > 0.0)) continue;
        const double vp = svq_[k][0] / swt_[k][0], vn = svq_[k][1] / swt_[k][1];
        const double wp = sw_[k][0] / swt_[k][0],  wn = sw_[k][1] / swt_[k][1];
        if (wp <= 0.0 || wn >= 0.0) continue;   // did not actually move in the commanded directions
        w[m] = 0.5 * (wp - wn);
        d[m] = 0.5 * (vp - vn);
        s[m] = 0.5 * (vp + vn);
        ++m;
    }
    out.speeds = (uint8_t)m;
#ifdef MOTORCAL_DEBUG
    for (int k = 0; k < MAX_SPEEDS; ++k) if (n_[k][0] || n_[k][1])
        printf("    spd %d n+=%u n-=%u vq+=%.4f vq-=%.4f w+=%.3f w-=%.3f\n", k, n_[k][0], n_[k][1],
               swt_[k][0] > 0 ? svq_[k][0] / swt_[k][0] : 0.0, swt_[k][1] > 0 ? svq_[k][1] / swt_[k][1] : 0.0,
               swt_[k][0] > 0 ? sw_[k][0] / swt_[k][0] : 0.0, swt_[k][1] > 0 ? sw_[k][1] / swt_[k][1] : 0.0);
#endif
    if (m < lim.min_speeds || m < 2) { out.flags |= F_KV_POOR_FIT; return false; }
    double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0, ss = 0;
    for (int i = 0; i < m; ++i) {
        sx += w[i]; sy += d[i]; sxx += w[i] * w[i]; sxy += w[i] * d[i]; syy += d[i] * d[i]; ss += s[i];
    }
    const double vxx = m * sxx - sx * sx, vyy = m * syy - sy * sy, vxy = m * sxy - sx * sy;
    if (vxx <= 1e-12) { out.flags |= F_KV_POOR_FIT; return false; }
    const double ke = vxy / vxx;
    out.ke        = (float)ke;
    out.v_coulomb = (float)((sy - ke * sx) / m);
    out.v_static  = (float)(ss / m);
    out.r2        = vyy > 0.0 ? (float)(vxy * vxy / (vxx * vyy)) : 1.0f;
    if (!(ke > 0.0) || out.r2 < lim.min_r2) { out.flags |= F_KV_POOR_FIT; return false; }
    out.kv_rpm_v = kv_from_ke(out.ke);
    out.kt       = kt_from_ke(out.ke);
    out.ok = true;
    return true;
}

// ===========================================================================
// Sequencer
// ===========================================================================
namespace {

class Runner {
public:
    Runner(Io& io, const SeqConfig& cfg, AxisCal& out) : io_(io), cfg_(cfg), out_(out) {}

    int ticks(float s) const {
        const int n = (int)lroundf(s / cfg_.dt);
        return n < 1 ? 1 : n;
    }
    // Actual-volts / commanded-volts. A wildly implausible bus reading is
    // ignored (scale 1) rather than corrupting every estimate.
    float vscale() {
        const float vb = io_.busVoltage();
        const float k  = vb / cfg_.v_supply_cfg;
        return (isfinite(k) && k > 0.5f && k < 1.5f) ? k : 1.0f;
    }
    // ---- open-loop primitives -------------------------------------------
    bool ol_step(float ud, float a) {
        io_.setVoltage(0.0f, ud, a);
        io_.tick();
        ++n_ticks_;
        if (!io_.readAngle(theta_)) { out_.flags |= F_NO_SENSOR | F_ABORTED; return false; }
        return true;
    }
    bool ol_hold(float ud, float a, float s) {
        for (int i = ticks(s); i > 0; --i) if (!ol_step(ud, a)) return false;
        return true;
    }
    bool ol_ramp_amp(float v0, float v1, float a, float s) {
        const int n = ticks(s);
        for (int i = 1; i <= n; ++i) if (!ol_step(v0 + (v1 - v0) * (float)i / (float)n, a)) return false;
        return true;
    }
    // Raised-cosine angle move: zero velocity at both ends (no step excitation
    // of the very lightly damped rotor-on-vector spring).
    bool ol_move(float ud, float a0, float a1, float s) {
        const int n = ticks(s);
        for (int i = 1; i <= n; ++i) {
            const float f = 0.5f - 0.5f * cosf(PI_F * (float)i / (float)n);
            if (!ol_step(ud, a0 + (a1 - a0) * f)) return false;
        }
        return true;
    }
    // Stationary DC-link sample -> resistance estimator.
    bool r_dwell(float ud_cmd, float a) {
        if (!ol_hold(ud_cmd, a, cfg_.settle_s)) return false;
        if (!has_sense_) return true;
        double si = 0;
        int    ni = 0;
        for (int i = ticks(cfg_.dwell_s); i > 0; --i) {
            if (!ol_step(ud_cmd, a)) return false;
            float amps;
            if (!io_.readCurrent(amps)) { has_sense_ = false; return true; }
            si += (double)amps; ++ni;
        }
        const float vb = io_.busVoltage();
        const float k  = vscale();
        rest_.add(ud_cmd * k, isfinite(vb) && vb > 0.0f ? vb : cfg_.v_supply_cfg, (float)(si / ni));
        return true;
    }

    // ---- stage A: offset + R --------------------------------------------
    bool stage_offset_r(Mode mode) {
        offs_.reset();
        rest_.reset();
        float probe;
        has_sense_ = io_.readCurrent(probe);

        int first, n_lv;
        if (mode == Mode::FULL) { first = 0; n_lv = cfg_.n_levels_full; }
        else                    { first = cfg_.n_levels_full - 1; n_lv = cfg_.n_levels_verify; }
        if (n_lv < 1) n_lv = 1;
        if (first < 0) first = 0;
        if (first + n_lv > OffsetEstimator::MAX_LEVELS) n_lv = OffsetEstimator::MAX_LEVELS - first;

        // Zero-vector point: driver switching at 50 % duty, no phase current.
        if (!r_dwell(0.0f, 0.0f)) return false;

        const float a0 = 0.0f;
        float v_now = 0.0f;
        for (int j = 0; j < n_lv; ++j) {
            const int   lv = first + j;
            const float V  = cfg_.v_levels[lv];
            if (!(V > 0.0f)) continue;
            if (!ol_ramp_amp(v_now, V, a0, cfg_.ramp_s)) return false;
            v_now = V;
            if (!r_dwell(V, a0)) return false;

            // Sinusoidal sweep through one electrical revolution (+-pi).
            // Decimate so every level gets an equal share of the point buffer
            // (an even stride keeps forward/reverse sample counts balanced).
            const float k = vscale();
            const int   N = ticks(cfg_.sweep_period_s);
            const int   per_level = OffsetEstimator::MAX_POINTS / n_lv;
            int stride = (N + per_level - 1) / per_level;
            if (stride < 1) stride = 1;
            for (int i = 1; i <= N; ++i) {
                const float ph = TWO_PI_F * (float)i / (float)N;
                const float a  = a0 + PI_F * sinf(ph);
                if (!ol_step(V, a)) return false;
                const float vel = cosf(ph);
                if (i % stride == 0 && fabsf(vel) >= cfg_.sweep_min_speed_frac)
                    offs_.add(lv, V * k, a, theta_, sgn(vel));
            }
            if (!r_dwell(V, a0)) return false;   // the sweep ends back at a0
            // R at two more angles 120 deg apart (averages phase imbalance). FULL only.
            if (mode == Mode::FULL && has_sense_) {
                const float a1 = a0 + TWO_PI_F / 3.0f, a2 = a0 - TWO_PI_F / 3.0f;
                if (!ol_move(V, a0, a1, cfg_.move_s) || !r_dwell(V, a1)) return false;
                if (!ol_move(V, a1, a2, 2.0f * cfg_.move_s) || !r_dwell(V, a2)) return false;
                if (!ol_move(V, a2, a0, cfg_.move_s)) return false;
            }
        }
        if (!ol_ramp_amp(v_now, 0.0f, a0, cfg_.ramp_s)) return false;
        if (!r_dwell(0.0f, 0.0f)) return false;  // second zero-vector point (P0 drift)

        // --- solve offset
        OffsetResult o;
        const bool ok = offs_.solve(cfg_.pole_pairs, cfg_.offset_lim, o);
        out_.flags |= o.flags;
        out_.direction       = o.direction;
        out_.pole_pairs_meas = o.pole_pairs_meas;
        out_.zero            = o.zero;
        out_.zero_raw        = o.zero_raw;
        out_.load_angle      = o.load_angle;
        out_.hysteresis      = o.hysteresis;
        out_.residual_rms    = o.residual_rms;
        last_theta_m_        = theta_;

        // --- solve R (independent of the offset result)
        if (!has_sense_) {
            out_.flags |= F_R_NO_SENSE;
        } else {
            ResistanceResult r;
            if (rest_.solve(cfg_.r_driver, cfg_.r_lim, r)) {
                out_.r_eff   = r.r_eff;
                out_.r_phase = r.r_phase;
                out_.p0      = r.p0;
                out_.r_r2    = r.r2;
                out_.r_eff_25 = r_to_ref_temp(r.r_eff, cfg_.temp_c);
                const float r25 = r_to_ref_temp(r.r_phase, cfg_.temp_c);
                if (r25 < cfg_.r_min_frac * cfg_.r_nominal || r25 > cfg_.r_max_frac * cfg_.r_nominal)
                    out_.flags |= F_R_OUT_OF_RANGE;
                else
                    out_.flags |= F_R_OK;
            } else {
                out_.flags |= r.flags;
                out_.r_r2 = r.r2;
            }
        }
        return ok;
    }

    // ---- stage B: back-EMF (closed loop) ----------------------------------
    bool cl_step(float target) {
        io_.tick();
        ++n_ticks_;
        if (!io_.closedLoopStep(target, vq_, vel_, th_)) { out_.flags |= F_NO_SENSOR | F_ABORTED; return false; }
        return true;
    }
    bool cl_hold(float target, float s) {
        for (int i = ticks(s); i > 0; --i) if (!cl_step(target)) return false;
        return true;
    }
    // Trapezoidal target profile from -> to. When k >= 0, samples taken in the
    // cruise phase (after the lead time) with the MEASURED angle inside
    // [wlo, whi] go to the back-EMF estimator.
    bool cl_move(float from, float to, float vmax, float acc, int k, float wlo, float whi, float vs) {
        const float D = fabsf(to - from);
        const float s = to >= from ? 1.0f : -1.0f;
        if (D < 1e-6f) return cl_step(to);
        float ta = vmax / acc, da = 0.5f * acc * ta * ta;
        if (2.0f * da > D) { ta = sqrtf(D / acc); vmax = acc * ta; da = 0.5f * D; }
        const float tc = (D - 2.0f * da) / vmax;
        const float T  = 2.0f * ta + tc;
        const int   n  = (int)ceilf(T / cfg_.dt);
        if (k >= 0) bemf_.begin_sweep(k, (int)s, wlo, whi);
        for (int i = 1; i <= n; ++i) {
            const float t = fminf((float)i * cfg_.dt, T);
            float p;
            if (t < ta)           p = 0.5f * acc * t * t;
            else if (t < ta + tc) p = da + vmax * (t - ta);
            else                  { const float r = T - t; p = D - 0.5f * acc * r * r; }
            if (!cl_step(from + s * p)) return false;
            if (k >= 0 && t >= ta + cfg_.kv_lead_s && t <= ta + tc)
                bemf_.sample(th_, vq_ * vs, vel_);
        }
        if (k >= 0) bemf_.end_sweep();   // incomplete window coverage -> sweep dropped
        return true;
    }

    bool stage_bemf() {
        bemf_.reset();
        float theta0;
        if (!io_.closedLoopBegin(out_.zero, out_.direction, theta0)) {
            out_.flags |= F_CLOSED_LOOP_FAIL;
            return false;
        }
        th_ = theta0;
        if (!cl_hold(theta0, cfg_.settle_s)) return false;
        const float c = theta0;
        float W = cfg_.kv_window_rad > 0.0f ? cfg_.kv_window_rad
                                            : cogging_window(cfg_.stator_slots, cfg_.pole_pairs, 6);
        if (!(W > 0.0f)) W = 0.45f;
        const float acc = cfg_.kv_accel > 0.0f ? cfg_.kv_accel : 40.0f;
        const float vr  = cfg_.kv_reposition_speed > 0.0f ? cfg_.kv_reposition_speed : 1.0f;
        float pos = c;
        int   tried = 0;
        const int ns = cfg_.n_kv_speeds < BemfEstimator::MAX_SPEEDS ? cfg_.n_kv_speeds : BemfEstimator::MAX_SPEEDS;
        for (int k = 0; k < ns; ++k) {
            const float w = cfg_.kv_speeds[k];
            if (!(w > 0.0f)) continue;
            const float L = w * w / (2.0f * acc) + w * cfg_.kv_lead_s + cfg_.kv_margin_rad;
            if (W + 2.0f * L > cfg_.kv_max_travel) continue;   // would leave the travel budget
            ++tried;
            const float S = c - 0.5f * W - L, E = c + 0.5f * W + L;
            if (!cl_move(pos, S, vr, acc, -1, 0, 0, 1.0f) || !cl_hold(S, cfg_.settle_s)) return false;
            const float k1 = vscale();
            if (!cl_move(S, E, w, acc, k, c - 0.5f * W, c + 0.5f * W, k1) || !cl_hold(E, cfg_.settle_s)) return false;
            const float k2 = vscale();
            if (!cl_move(E, S, w, acc, k, c - 0.5f * W, c + 0.5f * W, k2) || !cl_hold(S, cfg_.settle_s)) return false;
            pos = S;
        }
        // Return to the start pose before releasing.
        if (!cl_move(pos, c, vr, acc, -1, 0, 0, 1.0f) || !cl_hold(c, cfg_.settle_s)) return false;

        BemfResult b;
        BemfLimits lim = cfg_.kv_lim;
        if (bemf_.solve(lim, b)) {
            out_.ke        = b.ke;
            out_.kv_rpm_v  = b.kv_rpm_v;
            out_.kt        = b.kt;
            out_.v_coulomb = b.v_coulomb;
            out_.v_static  = b.v_static;
            out_.kv_r2     = b.r2;
            if (b.ke < cfg_.ke_min_frac * cfg_.ke_nominal || b.ke > cfg_.ke_max_frac * cfg_.ke_nominal)
                out_.flags |= F_KV_OUT_OF_RANGE;
            else
                out_.flags |= F_KV_OK;
        } else {
            out_.flags |= b.flags;
            out_.kv_r2 = b.r2;
            if (tried < lim.min_speeds) out_.flags |= F_KV_TRAVEL;
        }
        return true;
    }

    uint32_t n_ticks() const { return n_ticks_; }
    float    last_theta_m() const { return last_theta_m_; }

private:
    Io&              io_;
    const SeqConfig& cfg_;
    AxisCal&         out_;
    OffsetEstimator     offs_;
    ResistanceEstimator rest_;
    BemfEstimator       bemf_;
    bool     has_sense_ = false;
    float    theta_ = 0.0f, last_theta_m_ = 0.0f;
    float    vq_ = 0.0f, vel_ = 0.0f, th_ = 0.0f;
    uint32_t n_ticks_ = 0;
};

}  // namespace

bool run(Io& io, const SeqConfig& cfg, Mode mode, AxisCal& out) {
    out = AxisCal{};
    // The estimators hold ~5.5 kB of samples: keep them off the (8 kB) loop
    // task stack. Static storage => run() is not re-entrant (one axis at a time
    // by design: the DC-link current sense is shared by all three drivers).
    alignas(Runner) static unsigned char storage[sizeof(Runner)];
    Runner* r = new (storage) Runner(io, cfg, out);

    bool ok = r->stage_offset_r(mode);
    if (ok) {
        if (mode == Mode::FULL) {
            if (!r->stage_bemf()) {
                // Closed-loop failure with a valid offset means the axis cannot run.
                if (out.flags & (F_CLOSED_LOOP_FAIL | F_ABORTED)) ok = false;
            }
        } else {
            out.flags |= F_KV_SKIPPED;
        }
    }
    io.setVoltage(0.0f, 0.0f, 0.0f);
    io.release();
    out.duration_s = (float)r->n_ticks() * cfg.dt;
    r->~Runner();
    return ok;
}

// ===========================================================================
// Persistent record
// ===========================================================================
uint32_t crc32(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void record_init(Record& r) {
    memset(&r, 0, sizeof r);
    record_seal(r);
}

void record_seal(Record& r) {
    r.magic   = RECORD_MAGIC;
    r.version = RECORD_VERSION;
    r.size    = (uint16_t)sizeof(Record);
    r.crc     = crc32(&r, offsetof(Record, crc));
}

bool record_valid(const Record& r) {
    if (r.magic != RECORD_MAGIC || r.version != RECORD_VERSION || r.size != sizeof(Record)) return false;
    if (r.crc != crc32(&r, offsetof(Record, crc))) return false;
    for (int i = 0; i < RECORD_AXES; ++i) {
        const AxisRecord& a = r.axis[i];
        if (a.valid & 1u) {
            if (!isfinite(a.zero) || a.zero < 0.0f || a.zero >= TWO_PI_F) return false;
            if (a.direction != 1 && a.direction != -1) return false;
        }
        if ((a.valid & 2u) && !(isfinite(a.r_eff_25) && a.r_eff_25 > 0.0f && isfinite(a.r_phase_25))) return false;
        if ((a.valid & 4u) && !(isfinite(a.ke) && a.ke > 0.0f && isfinite(a.v_coulomb))) return false;
    }
    return true;
}

bool axis_record_from(const AxisCal& c, AxisRecord& rec) {
    memset(&rec, 0, sizeof rec);
    if (!c.offset_ok()) return false;
    rec.zero       = c.zero;
    rec.direction  = c.direction;
    rec.pole_pairs = (uint8_t)lroundf(c.pole_pairs_meas);
    rec.valid      = 1u;
    if (c.r_ok()) {
        rec.valid     |= 2u;
        rec.r_eff_25   = c.r_eff_25;
        rec.r_phase_25 = c.r_eff_25 - (c.r_eff - c.r_phase);   // driver part is temperature-independent here
    }
    if (c.kv_ok()) {
        rec.valid    |= 4u;
        rec.ke        = c.ke;
        rec.v_coulomb = c.v_coulomb;
    }
    return true;
}

bool reconcile(const AxisRecord& stored, const ReconcileLimits& lim, AxisCal& cal) {
    if (!(stored.valid & 1u) || !cal.offset_ok()) return false;
    if (cal.direction != stored.direction) { cal.flags |= F_DIR_CHANGED; return false; }
    // VERIFY runs a single amplitude, so zero_raw still contains the static
    // load angle; the tolerance covers it. A larger jump = magnet slipped on
    // the shaft / motor re-wired / encoder replaced.
    if (fabsf(wrap_pi(cal.zero_raw - stored.zero)) > lim.zero_tol) { cal.flags |= F_ZERO_SHIFTED; return false; }
    cal.zero = stored.zero;   // load-extrapolated FULL value is the better estimate
    if (stored.valid & 4u) {
        cal.ke        = stored.ke;
        cal.kt        = kt_from_ke(stored.ke);
        cal.kv_rpm_v  = kv_from_ke(stored.ke);
        cal.v_coulomb = stored.v_coulomb;
        cal.flags    |= F_KV_OK | F_KV_SKIPPED;
    }
    if (cal.r_ok() && (stored.valid & 2u) && stored.r_eff_25 > 0.0f &&
        fabsf(cal.r_eff_25 - stored.r_eff_25) > lim.r_drift_frac * stored.r_eff_25)
        cal.flags |= F_R_DRIFT;
    return true;
}

bool record_differs(const AxisRecord& a, const AxisRecord& b) {
    if (a.valid != b.valid || a.direction != b.direction || a.pole_pairs != b.pole_pairs) return true;
    if ((a.valid & 1u) && fabsf(wrap_pi(a.zero - b.zero)) > 0.02f) return true;
    auto rel = [](float x, float y) { return fabsf(x - y) > 0.03f * fmaxf(fabsf(x), fabsf(y)); };
    if ((a.valid & 2u) && (rel(a.r_eff_25, b.r_eff_25) || rel(a.r_phase_25, b.r_phase_25))) return true;
    if ((a.valid & 4u) && rel(a.ke, b.ke)) return true;
    return false;
}

}  // namespace motorcal
