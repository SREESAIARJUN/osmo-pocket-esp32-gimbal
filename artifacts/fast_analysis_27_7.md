# SimpleFOC Smooth Follow Mode for a Handheld Gimbal

This is a complete follow-mode controller for an Osmo Pocket–style gimbal built on SimpleFOC. It combines four techniques:

- **Hysteretic soft deadband.** Small hand jitter is ignored. Once following starts, it continues until the camera is nearly re-centered, so motion doesn't stutter at the deadband edge.
- **Exponential rate easing.** The follow speed is proportional to the error beyond the deadband, so the camera approaches the target asymptotically.
- **Rate low-pass and acceleration limit.** These produce S-curve starts and stops instead of a sudden jerk into motion.
- **Frame-rate independence.** All smoothing uses `1 - exp(-dt/τ)`, so the feel doesn't change if the loop timing varies.

I don't know what "Cycle 27" refers to in your project, so I've treated this as a standalone implementation. Tell me if it should match a previous revision's structure.

---

## Architecture

```
 Camera IMU ──► cam_angle (world frame)
 Encoder    ──► rel_angle = camera − handle (mechanical)
                handle_angle = cam_angle − rel_angle

 handle_angle ──► [FollowAxis] ──► setpoint (world frame, smoothed)
                                       │
 cam_angle ─────────────────────► [Stabilize PID] ──► motor.target (voltage/torque)
```

The camera is stabilized in world space by its IMU. The follow controller only moves the world-frame setpoint, letting it trail the handle smoothly. No IMU is needed on the handle, because the encoder supplies the camera-to-handle relative angle.

---

## `FollowAxis.h`

```cpp
#pragma once
#include <Arduino.h>
#include <math.h>

static inline float wrapPi(float a) {
  while (a >  PI) a -= TWO_PI;
  while (a < -PI) a += TWO_PI;
  return a;
}

static inline float expAlpha(float dt, float tau) {
  return (tau <= 0.0f) ? 1.0f : 1.0f - expf(-dt / tau);
}

struct FollowParams {
  float deadband_enter   = radians(6.0f);  // start following beyond this
  float deadband_release = radians(1.0f);  // stop following when this close
  float soft_knee        = radians(3.0f);  // smooth transition width at deadband edge
  float follow_tau       = 0.45f;          // s, exponential approach time constant
  float rate_tau         = 0.12f;          // s, rate low-pass (easing in/out)
  float max_rate         = radians(120.0f);// rad/s cap
  float max_accel        = radians(400.0f);// rad/s^2 cap (S-curve)
  bool  wrap             = true;           // yaw: wrap ±π; pitch/roll: false
  float min_limit        = -1e9f;          // world setpoint limits (pitch)
  float max_limit        =  1e9f;
};

class FollowAxis {
public:
  FollowParams p;
  float setpoint = 0.0f;   // world-frame camera target
  float rate     = 0.0f;   // current smoothed follow rate
  bool  enabled  = true;

  void reset(float angle) { setpoint = angle; rate = 0.0f; following_ = false; }

  float update(float handle_angle, float dt) {
    if (dt <= 0.0f || dt > 0.1f) return setpoint;   // guard against stalls

    float err = handle_angle - setpoint;
    if (p.wrap) err = wrapPi(err);
    float aerr = fabsf(err);

    // --- Hysteretic deadband state ---
    if (!enabled)                         following_ = false;
    else if (aerr > p.deadband_enter)     following_ = true;
    else if (aerr < p.deadband_release)   following_ = false;

    // --- Effective error with soft knee ---
    float eff = 0.0f;
    if (following_) {
      // While following, the window shrinks to the release threshold
      float db = p.deadband_release;
      float x  = aerr - db;
      if (x > 0.0f) {
        // Quadratic knee: smooth derivative at the edge, linear beyond
        float k = p.soft_knee;
        float shaped = (x < k) ? (x * x) / (2.0f * k) : (x - 0.5f * k);
        eff = copysignf(shaped, err);
      }
    }

    // --- Exponential approach: rate proportional to error ---
    float target_rate = eff / p.follow_tau;
    target_rate = constrain(target_rate, -p.max_rate, p.max_rate);

    // --- Rate easing: low-pass, then acceleration limit ---
    float eased = rate + (target_rate - rate) * expAlpha(dt, p.rate_tau);
    float dv    = constrain(eased - rate, -p.max_accel * dt, p.max_accel * dt);
    rate += dv;

    // --- Integrate setpoint ---
    setpoint += rate * dt;
    if (p.wrap) setpoint = wrapPi(setpoint);
    else {
      float c = constrain(setpoint, p.min_limit, p.max_limit);
      if (c != setpoint) { setpoint = c; rate = 0.0f; }
    }
    return setpoint;
  }

  bool isFollowing() const { return following_; }

private:
  bool following_ = false;
};
```

---

## Main sketch (yaw follow, pitch with gentle follow)

```cpp
#include <SimpleFOC.h>
#include "FollowAxis.h"

// ---------------- Hardware ----------------
BLDCMotor        yawMotor(7);                       // pole pairs: set for your gimbal motor
BLDCDriver3PWM   yawDriver(9, 10, 11, 8);
MagneticSensorSPI yawEnc(AS5048_SPI, 10);

BLDCMotor        pitchMotor(7);
BLDCDriver3PWM   pitchDriver(5, 6, 3, 7);
MagneticSensorSPI pitchEnc(AS5048_SPI, 4);

// Replace with your IMU fusion (Mahony/Madgwick/BMI270 etc.).
// Must return the CAMERA orientation in world frame, radians.
extern void  imuUpdate();
extern float imuYaw();
extern float imuPitch();
extern float imuYawRate();    // rad/s, gyro (camera frame)
extern float imuPitchRate();

// ---------------- Controllers ----------------
FollowAxis yawFollow, pitchFollow;

struct StabPD {
  float kp, kd, limit;
  float out(float err, float rate) {          // D-on-measurement using gyro
    return constrain(kp * err - kd * rate, -limit, limit);
  }
};
StabPD yawStab   {6.0f, 0.25f, 6.0f};   // output: voltage (V)
StabPD pitchStab {5.0f, 0.20f, 6.0f};

float yawEncZero = 0, pitchEncZero = 0;
uint32_t lastFollowUs = 0;

// ---------------- Setup ----------------
void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 8.4;   // 2S
  d.init();
  m.linkDriver(&d);
  m.foc_modulation   = FOCModulationType::SpaceVectorPWM;
  m.torque_controller = TorqueControlType::voltage;
  m.controller       = MotionControlType::torque;
  m.voltage_limit    = 6.0;
  m.init();
  m.initFOC();
}

void setup() {
  Serial.begin(115200);
  setupMotor(yawMotor,   yawDriver,   yawEnc);
  setupMotor(pitchMotor, pitchDriver, pitchEnc);

  // Encoder zero = camera aligned with handle (calibrate once, store in flash)
  yawEncZero   = yawMotor.shaft_angle;
  pitchEncZero = pitchMotor.shaft_angle;

  // Yaw: classic follow
  yawFollow.p.deadband_enter   = radians(6);
  yawFollow.p.deadband_release = radians(1);
  yawFollow.p.follow_tau       = 0.45f;
  yawFollow.p.rate_tau         = 0.12f;
  yawFollow.p.max_rate         = radians(120);
  yawFollow.p.wrap             = true;

  // Pitch: wider deadband, slower, mechanically limited
  pitchFollow.p.deadband_enter   = radians(10);
  pitchFollow.p.deadband_release = radians(2);
  pitchFollow.p.follow_tau       = 0.8f;
  pitchFollow.p.rate_tau         = 0.18f;