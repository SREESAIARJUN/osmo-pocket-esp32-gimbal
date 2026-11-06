// Madgwick 6-DoF AHRS — Revision 57.0. See madgwick_ahrs.h.
#include "madgwick_ahrs.h"
#include <math.h>

namespace {
inline float inv_norm3(float a, float b, float c) {
    const float n = sqrtf(a * a + b * b + c * c);
    return n > 0.0f ? 1.0f / n : 0.0f;
}
}  // namespace

void MadgwickAHRS::reset() {
    q_ = Quat{};
    init_ = false;
    t_ = 0.0f;
    w_acc_ = 0.0f;
    bias_[0] = bias_[1] = bias_[2] = 0.0f;
}

float MadgwickAHRS::gateWeight(float a_norm_g, float lo, float hi) {
    const float e = fabsf(a_norm_g - 1.0f);
    if (e <= lo) return 1.0f;
    if (e >= hi || hi <= lo) return 0.0f;
    return (hi - e) / (hi - lo);
}

bool MadgwickAHRS::initFromAccel(float ax, float ay, float az) {
    const float in = inv_norm3(ax, ay, az);
    if (in == 0.0f || !isfinite(in)) return false;
    ax *= in; ay *= in; az *= in;
    // Gravity in body = [-sin(p), sin(r)cos(p), cos(r)cos(p)]  (Z-Y-X, yaw = 0)
    const float roll  = atan2f(ay, az);
    const float pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    const float cr = cosf(0.5f * roll),  sr = sinf(0.5f * roll);
    const float cp = cosf(0.5f * pitch), sp = sinf(0.5f * pitch);
    q_.w = cr * cp;
    q_.x = sr * cp;
    q_.y = cr * sp;
    q_.z = -sr * sp;
    init_ = true;
    t_ = 0.0f;
    return true;
}

void MadgwickAHRS::update(float gx, float gy, float gz, float ax, float ay, float az, float dt) {
    if (!init_) {
        initFromAccel(ax, ay, az);
        return;
    }
    if (!(dt > 0.0f) || dt > 0.1f) return;  // reject nonsense dt (NaN, negative, > 100 ms gap)

    gx -= bias_[0]; gy -= bias_[1]; gz -= bias_[2];
    float q0 = q_.w, q1 = q_.x, q2 = q_.y, q3 = q_.z;

    // Rate of change of quaternion from gyroscope: 0.5 q (x) [0 g]
    float qd0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    float qd1 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
    float qd2 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
    float qd3 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

    const float an = sqrtf(ax * ax + ay * ay + az * az);
    w_acc_ = gateWeight(an, cfg_.gate_lo_g, cfg_.gate_hi_g);
    const float beta = (t_ < cfg_.init_time_s ? cfg_.beta_init : cfg_.beta) * w_acc_;

    if (beta > 0.0f && an > 0.0f) {
        const float ia = 1.0f / an;
        ax *= ia; ay *= ia; az *= ia;

        // Gradient of f = R(q)^T [0 0 1] - a  (objective function Jacobian^T * f)
        const float _2q0 = 2.0f * q0, _2q1 = 2.0f * q1, _2q2 = 2.0f * q2, _2q3 = 2.0f * q3;
        const float _4q0 = 4.0f * q0, _4q1 = 4.0f * q1, _4q2 = 4.0f * q2;
        const float _8q1 = 8.0f * q1, _8q2 = 8.0f * q2;
        const float q0q0 = q0 * q0, q1q1 = q1 * q1, q2q2 = q2 * q2, q3q3 = q3 * q3;
        float s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        float s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        float s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        float s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
        const float sn = sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        if (sn > 1e-9f) {
            const float is = 1.0f / sn;
            s0 *= is; s1 *= is; s2 *= is; s3 *= is;

            if (cfg_.zeta > 0.0f && w_acc_ > 0.0f) {
                // Gyro error in body frame: 2 q* (x) s_hat  (Madgwick eq. 47), integrated (eq. 48)
                const float ex = _2q0 * s1 - _2q1 * s0 - _2q2 * s3 + _2q3 * s2;
                const float ey = _2q0 * s2 + _2q1 * s3 - _2q2 * s0 - _2q3 * s1;
                const float ez = _2q0 * s3 - _2q1 * s2 + _2q2 * s1 - _2q3 * s0;
                const float k  = cfg_.zeta * w_acc_ * dt;
                const float e[3] = {ex, ey, ez};
                for (int i = 0; i < 3; ++i) {
                    float b = bias_[i] + k * e[i];
                    if (b >  cfg_.max_bias_rad_s) b =  cfg_.max_bias_rad_s;
                    if (b < -cfg_.max_bias_rad_s) b = -cfg_.max_bias_rad_s;
                    bias_[i] = b;
                }
            }
            qd0 -= beta * s0;
            qd1 -= beta * s1;
            qd2 -= beta * s2;
            qd3 -= beta * s3;
        }
    }

    q0 += qd0 * dt;
    q1 += qd1 * dt;
    q2 += qd2 * dt;
    q3 += qd3 * dt;
    const float n = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    if (!(n > 0.0f) || !isfinite(n)) {  // numerical blow-up: re-align on the next sample
        reset();
        return;
    }
    const float in = 1.0f / n;
    q_.w = q0 * in; q_.x = q1 * in; q_.y = q2 * in; q_.z = q3 * in;
    t_ += dt;
}

float MadgwickAHRS::roll() const {
    return atan2f(q_.w * q_.x + q_.y * q_.z, 0.5f - q_.x * q_.x - q_.y * q_.y);
}

float MadgwickAHRS::pitch() const {
    float s = -2.0f * (q_.x * q_.z - q_.w * q_.y);
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return asinf(s);
}

float MadgwickAHRS::yaw() const {
    return atan2f(q_.x * q_.y + q_.w * q_.z, 0.5f - q_.y * q_.y - q_.z * q_.z);
}

void MadgwickAHRS::gravityBody(float g[3]) const {
    g[0] = 2.0f * (q_.x * q_.z - q_.w * q_.y);
    g[1] = 2.0f * (q_.w * q_.x + q_.y * q_.z);
    g[2] = q_.w * q_.w - q_.x * q_.x - q_.y * q_.y + q_.z * q_.z;
}
