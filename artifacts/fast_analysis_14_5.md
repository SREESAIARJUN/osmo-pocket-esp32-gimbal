# SimpleFOC Smooth Follow Mode with Exponential Easing and Deadband (Pocket-Style Gimbal)

I don't know what "Cycle 14" refers to in your project, so I've used it as a version tag in the header. This is a complete follow-mode implementation for a 2-axis (pan/tilt) handheld gimbal. It has these features:

- **Soft deadband with hysteresis.** Small hand jitter is ignored. Once following starts, it continues until the camera has nearly re-centred, which gives the "lazy catch-up" feel of the Osmo Pocket.
- **Exponential rate easing.** The follow velocity is eased in and out with a time constant, so there are no jerks at the start or end of a pan.
- **Velocity and acceleration caps.** These give cinematic limits.
- **Framerate-independent smoothing.** It uses `alpha = 1 - exp(-dt/τ)`, so behaviour is identical at 1 kHz or 200 Hz.
- **Feed-forward handle compensation.** Stabilisation stays stiff while the follow target stays soft.

---

## `follow_mode.h`

```cpp
// =====================================================================
//  Smooth Follow Mode – SimpleFOC handheld gimbal   (rev: Cycle 14)
//  Exponential rate easing + hysteretic soft deadband
// =====================================================================
#pragma once
#include <Arduino.h>
#include <math.h>

static inline float wrapPi(float a) {
  while (a >  PI) a -= TWO_PI;
  while (a < -PI) a += TWO_PI;
  return a;
}

static inline float sgn(float x) { return (x > 0) - (x < 0); }

struct FollowParams {
  float deadband_enter;   // rad  – error needed to START following
  float deadband_exit;    // rad  – error at which following STOPS (< enter)
  float knee;             // rad  – soft-knee width (smooth deadband edge)
  float gain;             // 1/s  – rate per rad of effective error
  float tau_ease;         // s    – exponential easing time constant for rate
  float max_rate;         // rad/s
  float max_accel;        // rad/s^2
  float limit_min;        // rad  – mechanical joint limit (or -INF)
  float limit_max;        // rad
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float handle_angle) {
    cam_target = handle_angle;
    rate = 0.0f;
    following = false;
  }

  // handle_angle: handle orientation on this axis in the world frame (rad)
  // dt: seconds since last call
  // returns: desired camera angle in the world frame (rad)
  float update(float handle_angle, float dt) {
    if (dt <= 0.0f || dt > 0.1f) dt = 0.001f;            // guard glitches

    float err = wrapPi(handle_angle - cam_target);
    float aerr = fabsf(err);

    // ---- 1. Hysteretic deadband state machine -------------------------
    if (!following && aerr > P.deadband_enter) following = true;
    if ( following && aerr < P.deadband_exit && fabsf(rate) < 0.02f)
      following = false;

    // ---- 2. Soft-knee deadband -> effective error ---------------------
    float db = following ? P.deadband_exit : P.deadband_enter;
    float eff = softDeadband(aerr, db, P.knee) * sgn(err);

    // ---- 3. Commanded rate (proportional, capped) ---------------------
    float rate_cmd = following ? P.gain * eff : 0.0f;
    rate_cmd = constrain(rate_cmd, -P.max_rate, P.max_rate);

    // ---- 4. Exponential easing of the rate (dt-independent) -----------
    float alpha = 1.0f - expf(-dt / P.tau_ease);
    float eased = rate + alpha * (rate_cmd - rate);

    // ---- 5. Acceleration limit ----------------------------------------
    float dv_max = P.max_accel * dt;
    rate += constrain(eased - rate, -dv_max, dv_max);

    // ---- 6. Integrate, never overshoot the handle ----------------------
    float step = rate * dt;
    if (fabsf(step) > aerr && sgn(step) == sgn(err)) { step = err; rate *= 0.5f; }
    cam_target = wrapPi(cam_target + step);

    // ---- 7. Respect joint limits: drag target if handle pushes past ---
    float joint = wrapPi(cam_target - handle_angle);
    if (joint < P.limit_min) cam_target = wrapPi(handle_angle + P.limit_min);
    if (joint > P.limit_max) cam_target = wrapPi(handle_angle + P.limit_max);

    return cam_target;
  }

  float rateNow()   const { return rate; }
  bool  isFollowing() const { return following; }

private:
  // C1-continuous soft deadband: 0 below db, quadratic knee, then linear
  static float softDeadband(float x, float db, float k) {
    if (x <= db) return 0.0f;
    float d = x - db;
    if (d < k) return 0.5f * d * d / k;   // smooth knee
    return d - 0.5f * k;                  // linear region (slope 1)
  }

  FollowParams P;
  float cam_target = 0.0f;
  float rate = 0.0f;
  bool  following = false;
};
```

---

## `main.cpp`: SimpleFOC Integration

```cpp
#include <SimpleFOC.h>
#include "follow_mode.h"

// ---------------- Hardware (adjust pins) -----------------------------
BLDCMotor   panMotor(7);                       // pole pairs
BLDCDriver3PWM panDrv(9, 10, 11, 8);
MagneticSensorSPI panEnc(AS5048_SPI, 10);

BLDCMotor   tiltMotor(7);
BLDCDriver3PWM tiltDrv(5, 6, 3, 7);
MagneticSensorSPI tiltEnc(AS5048_SPI, 4);

// ---------------- IMU (provide your fusion: Mahony/Madgwick) ---------
// Returns handle yaw/pitch in world frame (rad), and body rates (rad/s)
extern void imuUpdate(float& yaw, float& pitch, float& gz, float& gy);

// ---------------- Follow tuning (Pocket-like feel) -------------------
FollowParams panP  = { radians(4.0f), radians(0.8f), radians(3.0f),
                       2.2f, 0.18f, radians(120), radians(400),
                       -INFINITY, INFINITY };          // continuous pan
FollowParams tiltP = { radians(5.0f), radians(1.0f), radians(3.0f),
                       1.8f, 0.22f, radians(90),  radians(300),
                       radians(-80), radians(45) };

FollowAxis panFollow(panP), tiltFollow(tiltP);

// Joint zero offsets (camera level/forward when encoder reads this)
float panZero = 0, tiltZero = 0;
unsigned long lastUs;

void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 12;
  d.init();
  m.linkDriver(&d);
  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.controller     = MotionControlType::angle;
  m.PID_velocity.P = 0.25f;  m.PID_velocity.I = 4.0f;  m.PID_velocity.D = 0;
  m.LPF_velocity.Tf = 0.005f;
  m.P_angle.P      = 25.0f;              // stiff: stabilisation is the job here
  m.velocity_limit = 30.0f;
  m.voltage_limit  = 6.0f;
  m.init();
  m.initFOC();
}

void setup() {
  Serial.begin(115200);
  setupMotor(panMotor,  panDrv,  panEnc);
  setupMotor(tiltMotor, tiltDrv, tiltEnc);

  float y, p, gz, gy;
  imuUpdate(y, p, gz, gy);
  panFollow.reset(y);
  tiltFollow.reset(p);
  lastUs = micros();
}

void loop() {
  // FOC inner loop at full speed
  panMotor.loopFOC();
  tiltMotor.loopFOC();

  // Outer follow/stabilisation loop at ~1 kHz
  unsigned long now = micros();
  float dt = (now - lastUs) * 1e-6f;
  if (dt < 0.001f) return;
  lastUs = now;

  float hYaw, hPitch, gz, gy;
  imuUpdate(hYaw, hPitch, gz, gy);

  // World-frame camera targets (smooth)
  float camYaw   = panFollow.update(hYaw, dt);
  float camPitch = tiltFollow.update(hPitch, dt);

  // Joint angle = camera_world - handle_world  (feed-forward stabilisation)
  float panJoint  = wrapPi(camYaw   - hYaw)   + panZero;
  float tiltJoint = wrapPi(camPitch - hPitch) + tiltZero;

  // Unwrap pan to be continuous with the encoder's multi-turn angle
  float cur = panMotor.shaft_angle;
  panJo