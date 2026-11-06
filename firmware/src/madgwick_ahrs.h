/**
 * madgwick_ahrs.h — 6-DoF Madgwick gradient-descent AHRS (Revision 57.0)
 *
 * Implements S. Madgwick, "An efficient orientation filter for inertial and
 * inertial/magnetic sensor arrays" (2010), IMU (gyro + accel) form, in
 * single-precision only (ESP32-S3 FPU; the module builds with
 * -Wdouble-promotion -Wconversion on the host).
 *
 * Convention: q = [w x y z] rotates BODY-frame vectors into the EARTH frame
 * (NWU-style, +Z up, gravity reference [0 0 1]); q_dot = 0.5 q (x) [0 w].
 * Euler output is aerospace Z-Y-X (yaw, pitch, roll).
 *
 * Additions to the reference filter for a handheld gimbal:
 *   - Accelerometer gating: the correction gain beta is scaled by a weight
 *     that ramps from 1 to 0 as | |a| - 1 g | goes from gate_lo to gate_hi,
 *     so walking/handle-swing linear acceleration does not tilt the horizon.
 *   - Accel-based initial alignment plus a high start-up gain (beta_init for
 *     init_time_s) so the filter is converged before the motors use it.
 *   - Variable dt per sample (from the IMU's device timestamps).
 *   - Optional on-line gyro bias integrator (Madgwick eq. 47-48, gain zeta).
 *     Default 0: bias is handled by the temperature model (imu_bias.h); about
 *     the gravity axis the IMU-only bias is unobservable anyway.
 */
#ifndef MADGWICK_AHRS_H
#define MADGWICK_AHRS_H

#include <stdint.h>

struct Quat {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
};

class MadgwickAHRS {
public:
    struct Config {
        float beta        = 0.02f;   // rad/s, steady-state correction gain
        float beta_init   = 1.0f;    // rad/s, during the start-up window
        float init_time_s = 0.5f;
        float zeta        = 0.0f;    // rad/s^2 per unit gradient, gyro-bias integrator gain
        float gate_lo_g   = 0.05f;   // |(|a|-1)| below: full accel weight
        float gate_hi_g   = 0.20f;   // above: accel ignored (pure gyro integration)
        float max_bias_rad_s = 0.035f;  // clamp on the zeta integrator (2 dps)
    };

    MadgwickAHRS() = default;
    explicit MadgwickAHRS(const Config& c) : cfg_(c) {}

    void configure(const Config& c) { cfg_ = c; }
    const Config& config() const { return cfg_; }

    /** Forget the attitude; the next update() re-aligns from the accelerometer. */
    void reset();

    /** Align roll/pitch to gravity (yaw = 0). Returns false for |a| ~ 0. */
    bool initFromAccel(float ax, float ay, float az);

    /**
     * One filter step. Gyro in rad/s (body frame), accel in g (body frame),
     * dt in seconds. If not yet initialised, aligns from the accelerometer
     * and returns without integrating.
     */
    void update(float gx, float gy, float gz, float ax, float ay, float az, float dt);

    bool  initialized() const { return init_; }
    const Quat& quat() const { return q_; }
    float roll() const;   // rad, about body X
    float pitch() const;  // rad, about body Y
    float yaw() const;    // rad, about earth Z
    /** Gravity direction (unit) expressed in the body frame, from q. */
    void  gravityBody(float g[3]) const;
    float accelWeight() const { return w_acc_; }
    float time() const { return t_; }
    const float* gyroBias() const { return bias_; }

    /** Accel gating weight in [0,1] for an accelerometer magnitude in g. */
    static float gateWeight(float a_norm_g, float lo, float hi);

private:
    Config cfg_;
    Quat   q_;
    bool   init_ = false;
    float  t_ = 0.0f;
    float  w_acc_ = 0.0f;
    float  bias_[3] = {0.0f, 0.0f, 0.0f};
};

#endif  // MADGWICK_AHRS_H
