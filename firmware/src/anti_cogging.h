// Anti-cogging feed-forward — Revision 8.0 / 150
//
// 512-point, electrical-angle-indexed cogging-torque LUT synthesised from (or
// fitted to) a sparse set of slot-harmonic coefficients, plus a voltage-mode
// feed-forward that cancels the cogging torque one FOC cycle ahead.
//
// Physics (12N14P = 12 slots, 14 magnet poles, 7 pole pairs):
//   cogging periods / mech rev  N_cog  = lcm(slots, poles) = lcm(12, 14) = 84
//   cogging periods / elec rev  k0     = N_cog / pole_pairs           = 12
// Cogging is the rotor magnets interacting with fixed stator teeth, and the
// stator teeth are fixed relative to the windings, so for an ideal motor
// tau_cog is periodic in the *electrical* angle with fundamental order k0.
// Only orders k0, 2*k0, ... up to ANTI_COGGING_ORDER are kept. Indexing by the
// normalised electrical angle (pole pairs, sensor direction and
// zero_electric_angle applied) makes the table independent of where the
// diametric magnet happens to sit on the shaft.
//
// Sign & unit convention:
//   LUT stores the *cogging* torque tau_cog(theta_e) in N*m.
//   Feed-forward torque  tau_ff = -tau_cog.
//   Voltage-mode feed-forward  Vq_ff = tau_ff * R / Kt  (quasi-static, back-EMF
//   is already handled by the velocity loop; the term is faded out at speed).
//
// The module is pure math (no Arduino/SimpleFOC dependency) so it is unit
// tested on the host: firmware/test/host/run_host_tests.sh.
#ifndef ANTI_COGGING_H
#define ANTI_COGGING_H

#include <stdint.h>
#include <stddef.h>

#define COGGING_LUT_SIZE 512

namespace cogging {

constexpr int   LUT_SIZE      = COGGING_LUT_SIZE;
constexpr int   LUT_MASK      = LUT_SIZE - 1;
constexpr int   MAX_HARMONICS = 8;
constexpr float TWO_PI_F      = 6.28318530717958647692f;
static_assert((LUT_SIZE & (LUT_SIZE - 1)) == 0, "LUT size must be a power of two (index masking)");

constexpr int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }
constexpr int lcm(int a, int b) { return a / gcd(a, b) * b; }
// Cogging fundamental expressed as an ELECTRICAL harmonic order.
// lcm(slots, 2*pp) is always a multiple of pp, so this is an integer.
constexpr int fundamental_order(int slots, int pole_pairs) {
    return lcm(slots, 2 * pole_pairs) / pole_pairs;
}

// tau(theta_e) = sum_i amp_nm[i] * sin(order[i] * theta_e + phase_rad[i])
struct Harmonic {
    uint8_t order;
    float   amp_nm;
    float   phase_rad;
};

struct HarmonicSet {
    Harmonic h[MAX_HARMONICS];
    uint8_t  count = 0;
    bool add(uint8_t order, float amp_nm, float phase_rad);
};

// Wrap any finite angle into [0, 2*pi). Non-finite input returns NaN.
float wrap_2pi(float a);

// SimpleFOC-equivalent electrical angle:
//   wrap(dir * pp * theta_mech - zero_electric_angle)
// dir must be +1/-1 (Direction::CW/CCW); dir == 0 (UNKNOWN) or non-finite
// input returns NaN so the caller suppresses feed-forward.
float electrical_angle(float theta_mech, int pole_pairs, int dir, float zero_electric_angle);

class Lut {
public:
    Lut() { clear(); }
    void clear();
    // Synthesise all LUT_SIZE points from the harmonic set (double precision).
    // Orders >= LUT_SIZE/2 (aliasing) or amplitude non-finite are rejected.
    bool build(const HarmonicSet& hs);
    // Linear interpolation between LUT points; theta_e in rad, any range.
    // Returns cogging torque in N*m, 0 for NaN input or an unbuilt table.
    float sample(float theta_e) const;
    float point(int i) const { return lut_[i & LUT_MASK]; }
    bool  valid() const { return valid_; }
    float peak_nm() const { return peak_; }
    const HarmonicSet& harmonics() const { return hs_; }

private:
    float       lut_[LUT_SIZE + 1];  // +1 guard copy of lut_[0] -> branch-free interpolation
    HarmonicSet hs_;
    float       peak_ = 0.0f;
    bool        valid_ = false;
};

// Single-bin DFT of a uniformly sampled, full-period table:
// returns amplitude/phase of the sin(order*theta + phase) component.
Harmonic extract_harmonic(const float* table, int n, uint8_t order);

// Calibration accumulator: bins hold-voltage samples by electrical angle,
// then fits the cogging harmonics (multiples of the fundamental only, which
// rejects gravity/load and DC friction bias). Feed it forward AND reverse
// sweeps so Coulomb friction (sign-dependent) cancels in the bin mean.
class Calibrator {
public:
    void  reset();
    // vq_hold: Vq the position loop needed to hold/track at theta_e (V).
    void  accumulate(float theta_e, float vq_hold);
    float coverage() const;  // fraction of bins with >= 1 sample
    // Fit harmonics k0, 2k0, ... <= max_order. Requires coverage >= min_coverage.
    // Converts Vq to cogging torque: tau_cog = -vq * Kt / R.
    bool  fit(int fundamental, int max_order, float kt, float phase_r,
              HarmonicSet& out, float min_coverage = 0.90f) const;
    uint32_t count(int bin) const { return n_[bin & LUT_MASK]; }

private:
    double   sum_[LUT_SIZE] = {};
    uint32_t n_[LUT_SIZE] = {};
};

struct FeedforwardParams {
    float kt          = 0.035f;  // N*m/A
    float phase_r     = 8.5f;    // Ohm (phase, as seen by SimpleFOC Vq)
    float vq_limit    = 0.5f;    // |Vq_ff| clamp, V
    float fade_start  = 20.0f;   // |omega_e| rad/s: full compensation below
    float fade_end    = 120.0f;  // |omega_e| rad/s: zero compensation above
};

// Feed-forward Vq for the NEXT commutation:
//   theta_pred = theta_e + omega_e * latency_s     (phase-advance)
//   Vq_ff      = -lut(theta_pred) * R / Kt * fade(|omega_e|), clamped.
float feedforward_vq(const Lut& lut, const FeedforwardParams& p,
                     float theta_e, float omega_e, float latency_s);

}  // namespace cogging

// ---- Firmware-facing API (3 axes: 0 = pan, 1 = roll, 2 = pitch) ----------
void  init_anti_cogging();
bool  anti_cogging_load(uint8_t axis, const cogging::HarmonicSet& hs);
const cogging::Lut* anti_cogging_lut(uint8_t axis);
// theta_e: normalised electrical angle; omega_e: electrical speed (rad/s);
// latency_s: time from this call until the Vq is applied by loopFOC().
float get_anti_cogging_voltage(uint8_t axis, float theta_e, float omega_e, float latency_s);

#endif  // ANTI_COGGING_H
