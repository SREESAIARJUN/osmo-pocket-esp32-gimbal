/**
 * imu_bias.h — Gyro zero-rate-offset temperature compensation (Revision 57.0)
 *
 * MEMS gyro zero-rate output drifts with die temperature. The ICM-42688-P
 * specifies a ZRO tempco of +/-0.005 dps/degC typical; the ESP32-S3, the three
 * TMC6300s and the LDO warm the board by 15-25 degC in the first minutes after
 * power-on, i.e. 0.1 dps of bias change. Integrated about the gravity axis
 * (where the accelerometer cannot correct it) that is 6 deg/min of yaw drift
 * — the pan "creep" seen on every cheap gimbal.
 *
 * Model, per sensor axis i (sensor frame, rad/s):
 *     b_i(T) = b0_i + b1_i * (T - T_REF)
 * estimated on-line by a 2-state Kalman filter per axis:
 *     state  x = [b0, b1]^T, random-walk process noise Q = diag(q0, q1) * dt
 *     meas.  z = mean gyro over a STILL window, H = [1, T_mean - T_REF],
 *            R = var(gyro)/N + r_floor
 * With only one temperature seen, b1 stays at its prior (0 +/- tempco spec)
 * and b0 absorbs the offset; as the board warms the slope becomes observable
 * and the filter learns it, so the bias keeps being cancelled while the gimbal
 * is in use and NOT still. Innovations beyond GATE_SIGMA are rejected (a slow
 * steady pan can masquerade as stillness).
 *
 * StillnessDetector decides when a window is "still": low gyro and accel
 * variance, |a| within 1 g +/- tol, mean rate below a plausible-bias bound,
 * and the gravity direction not rotating between the window halves (rejects
 * slow constant-rate rotation about any non-vertical axis).
 *
 * The die temperature is low-pass filtered (TEMP_TAU_S) before the model is
 * evaluated: the MEMS structure lags the die sensor and the filter removes
 * the 7.5 mK quantisation and record-to-record noise.
 *
 * Pure math, no Arduino dependency: host-tested in test_imu.cpp.
 */
#ifndef IMU_BIAS_H
#define IMU_BIAS_H

#include <stdint.h>

namespace imubias {
constexpr float T_REF_C = 25.0f;
}

/** Persistable model snapshot (e.g. NVS); trivially copyable. */
struct GyroTempModel {
    static constexpr uint32_t MAGIC = 0x47544D31u;  // "GTM1"
    uint32_t magic = MAGIC;
    float    b0[3] = {0, 0, 0};      // rad/s at T_REF
    float    b1[3] = {0, 0, 0};      // rad/s/degC
    float    p00[3] = {0, 0, 0};     // covariance diagonals + cross term per axis
    float    p01[3] = {0, 0, 0};
    float    p11[3] = {0, 0, 0};
    float    t_min = 0.0f, t_max = 0.0f;  // temperature span the slope was learned over
    uint32_t updates = 0;
    uint32_t crc = 0;                // CRC-32 over everything above
    uint32_t computeCrc() const;
    bool     valid() const;
    void     seal() { crc = computeCrc(); }
};

class StillnessDetector {
public:
    struct Config {
        uint16_t window = 500;            // samples (0.5 s @ 1 kHz)
        float gyro_std_max  = 0.0035f;    // rad/s (0.2 dps): ~4x the ICM-42688 LN noise at 250 Hz BW
        float accel_std_max = 0.006f;     // g
        float accel_norm_tol = 0.05f;     // | |a| - 1 g |
        float mean_rate_max = 0.0524f;    // rad/s (3 dps): ZRO spec +/-0.5 dps + tempco margin
        float tilt_change_max = 0.0006f;  // rad (0.034 deg) of gravity rotation between window halves
    };
    struct Window {
        float gyro_mean[3];   // rad/s, sensor frame, NOT bias-corrected
        float gyro_var[3];    // (rad/s)^2 per-sample variance
        float temp_mean;      // degC
        uint16_t n;
    };

    StillnessDetector() = default;
    explicit StillnessDetector(const Config& c) : cfg_(c) {}
    void configure(const Config& c) { cfg_ = c; reset(); }
    void reset();

    /**
     * Add one sample. Returns true when a window completed AND was still;
     * then @p out holds its statistics.
     */
    bool push(const float gyro[3], const float accel[3], float temp_c, Window& out);
    /** True if the most recently completed window was still. */
    bool lastStill() const { return last_still_; }

private:
    Config   cfg_;
    uint16_t n_ = 0;
    // Sums are taken about the window's first sample (shifted data), so single
    // precision is exact enough for the variance (no E[x^2]-E[x]^2 cancellation)
    // and no soft-float double is needed on the LX7.
    float    g_ref_[3] = {0, 0, 0}, a_ref_[3] = {0, 0, 0};
    float    gs_[3] = {0, 0, 0}, gss_[3] = {0, 0, 0};
    float    as_[3] = {0, 0, 0}, ass_[3] = {0, 0, 0};
    float    ah_[3] = {0, 0, 0};                        // shifted accel sum of the first half
    float    ts_ = 0.0f;
    bool     last_still_ = false;
};

class GyroTempBias {
public:
    struct Config {
        float b0_sigma0   = 0.0175f;    // rad/s (1 dps) prior on the offset
        float b1_sigma0   = 1.75e-4f;   // rad/s/degC (0.01 dps/degC) prior on the slope
        // Offset random walk: 1e-11 (rad/s)^2/s = 0.0018 dps per 100 s, in line with the
        // ICM-42688-P bias instability. 1e-9 forgot old windows within seconds and
        // made the slope estimate noisy (host test: 1.05 deg vs 0.4 deg warm-up yaw drift).
        float q_b0        = 1.0e-11f;   // (rad/s)^2 per s
        float q_b1        = 1.0e-13f;   // (rad/s/degC)^2 per s
        float r_floor     = 1.0e-9f;    // (rad/s)^2  measurement-noise floor (vibration, quantisation)
        float gate_sigma  = 5.0f;       // innovation gate
        float temp_tau_s  = 0.5f;       // die-temperature low-pass time constant
        // Lock-out recovery: a converged filter's gate is ~0.02 dps wide, so a real
        // bias shift (turn-on repeatability, shock, tempco hysteresis) would be
        // rejected for tens of minutes (Rev 57 review). After REANCHOR_WINDOWS
        // consecutive rejected windows that agree with each other (same sign,
        // spread < gate), the offset covariance is inflated to the innovation and
        // the window is fused: the filter re-anchors in ~3 s.
        uint8_t reanchor_windows = 6;
        // Turn-on-to-turn-on ZRO repeatability: floor on the offset sigma after
        // importing a persisted model (the q_b0 * age random walk is far too small).
        float import_b0_sigma = 0.00175f;  // rad/s (0.1 dps)
    };

    GyroTempBias() { reset(); }
    explicit GyroTempBias(const Config& c) : cfg_(c) { reset(); }
    void configure(const Config& c) { cfg_ = c; reset(); }
    void reset();

    /** Low-pass the die temperature (call once per sample, dt in s). */
    float filterTemp(float temp_c, float dt);
    float filteredTemp() const { return t_filt_; }

    /** Bias at temperature @p temp_c (rad/s, sensor frame). */
    void bias(float temp_c, float out[3]) const;

    /** Random-walk the covariance by @p dt seconds. */
    void predict(float dt);

    /**
     * Fuse one still window. Returns the number of axes accepted (0..3);
     * axes whose innovation exceeds the gate are rejected individually.
     */
    int update(const StillnessDetector::Window& w);

    uint32_t updates() const { return updates_; }
    uint32_t reanchors() const { return reanchors_; }
    uint32_t rejected() const { return rejected_; }
    float b0(int i) const { return b0_[i]; }
    float b1(int i) const { return b1_[i]; }
    float b0Sigma(int i) const;
    float b1Sigma(int i) const;
    float tempSpan() const { return seen_ ? t_max_ - t_min_ : 0.0f; }

    void exportModel(GyroTempModel& m) const;
    /** Restore a persisted model (rejects bad CRC/magic). Covariance is inflated by @p age_s of random walk. */
    bool importModel(const GyroTempModel& m, float age_s = 0.0f);

private:
    Config   cfg_;
    float    b0_[3], b1_[3], p00_[3], p01_[3], p11_[3];
    float    t_filt_ = 0.0f;
    bool     t_init_ = false;
    bool     seen_ = false;
    float    t_min_ = 0.0f, t_max_ = 0.0f;
    uint32_t updates_ = 0, rejected_ = 0, reanchors_ = 0;
    uint8_t  rej_run_[3] = {0, 0, 0};       // consecutive rejected windows per axis
    float    rej_first_[3] = {0, 0, 0};     // innovation of the first window in the run
};

#endif  // IMU_BIAS_H
