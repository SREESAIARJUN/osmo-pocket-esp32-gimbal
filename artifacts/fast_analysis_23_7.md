# SimpleFOC Smooth Follow Mode: Exponential Easing with a Deadband Window

The camera head stays locked in world space until the handle drifts outside a deadband window. Then it eases toward the handle along a critically-damped exponential curve. Acceleration is limited so pans start and stop gently, the way Osmo Pocket follow mode feels.

## Algorithm Overview

```
handleYaw  = cameraYaw(IMU) − motorAngle(encoder)        // reconstruct handle heading
err        = wrap(handleYaw − camTarget)

Deadband with hysteresis:
  IDLE   → FOLLOW  when |err| > enterDeadband
  FOLLOW → IDLE    when |err| < exitDeadband AND rate ≈ 0

softErr    = sign(err) · smoothstep(|err|)               // C1-continuous deadband edge
engage    += (target − engage)·(1 − e^(−dt/τ_engage))     // ease-in/out of follow
rateCmd    = engage · softErr / τ_follow                  // exponential approach
rate       = slew(rate → rateCmd, maxAccel·dt), clamp(maxRate)
camTarget += rate·dt
motorSetpoint = wrap(camTarget − handleYaw) + motorAngle  // unwrapped for SimpleFOC
```

Two choices make it frame-rate independent and jerk-free:
- **`1 − e^(−dt/τ)`** is used instead of a fixed alpha.
- **The soft deadband edge** avoids the step in commanded rate that a hard `max(|e|−db, 0)` produces at the boundary.

---

## `FollowAxis.h`

```cpp
#pragma once
#include <Arduino.h>
#include <math.h>

// Cycle 23: smooth follow axis with exponential easing + hysteretic deadband
struct FollowConfig {
  float enterDeadband = 6.0f  * DEG_TO_RAD; // start following beyond this
  float exitDeadband  = 0.5f  * DEG_TO_RAD; // settle/re-lock inside this
  float softKnee      = 4.0f  * DEG_TO_RAD; // width of smooth deadband edge
  float tauFollow     = 0.35f;              // s, exponential catch-up time constant
  float tauEngage     = 0.25f;              // s, ease-in/out of follow gain
  float maxRate       = 120.0f * DEG_TO_RAD; // rad/s
  float maxAccel      = 400.0f * DEG_TO_RAD; // rad/s^2
  float settleRate    = 1.0f  * DEG_TO_RAD; // rad/s, "stopped" threshold
  bool  enabled       = true;               // false = full lock (FPV/lock modes off)
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowConfig& c = FollowConfig()) : cfg(c) {}

  void reset(float cameraWorldAngle) {
    camTarget = cameraWorldAngle;
    rate = 0; engage = 0; following = false;
  }

  // handleAngle: handle heading in world frame (rad)
  // returns camera world-frame target (rad, continuous)
  float update(float handleAngle, float dt) {
    if (dt <= 0 || dt > 0.05f) dt = 0.002f;          // guard against stalls

    float err = wrapPi(handleAngle - camTarget);
    float aerr = fabsf(err);

    // ---- Hysteretic deadband window ----
    if (!cfg.enabled) {
      following = false;
    } else if (!following && aerr > cfg.enterDeadband) {
      following = true;
    } else if (following && aerr < cfg.exitDeadband && fabsf(rate) < cfg.settleRate) {
      following = false;
    }

    // ---- Exponential engage easing (smooth start/stop of follow) ----
    float engTarget = following ? 1.0f : 0.0f;
    engage += (engTarget - engage) * (1.0f - expf(-dt / cfg.tauEngage));

    // ---- Soft deadband: while following, track to center; idle uses knee ----
    float db = following ? cfg.exitDeadband : cfg.enterDeadband;
    float softErr = softDeadband(err, db, cfg.softKnee);

    // ---- Exponential approach -> rate command ----
    float rateCmd = engage * softErr / cfg.tauFollow;
    rateCmd = constrain(rateCmd, -cfg.maxRate, cfg.maxRate);

    // ---- Acceleration (slew) limit for cinematic ease ----
    float dvMax = cfg.maxAccel * dt;
    rate += constrain(rateCmd - rate, -dvMax, dvMax);

    // Exact exponential step cap: never overshoot the handle in one step
    float step = rate * dt;
    if (fabsf(step) > aerr) { step = err; rate = err / dt * 0.0f; }

    camTarget += step;
    return camTarget;
  }

  bool  isFollowing() const { return following; }
  float getRate()     const { return rate; }
  FollowConfig cfg;

  static float wrapPi(float a) {
    a = fmodf(a + PI, TWO_PI);
    if (a < 0) a += TWO_PI;
    return a - PI;
  }

private:
  float camTarget = 0, rate = 0, engage = 0;
  bool  following = false;

  // C1-continuous deadband: 0 inside db, smoothstep-blended over knee, linear after
  static float softDeadband(float e, float db, float knee) {
    float a = fabsf(e);
    if (a <= db) return 0.0f;
    float x = a - db, out;
    if (x < knee) {
      // integral of smoothstep slope -> derivative goes 0 -> 1 smoothly
      float t = x / knee;
      out = knee * (t * t * t - 0.5f * t * t * t * t);   // ∫(3t²-2t³) dt
      // at t=1: knee*0.5, slope 1 -> continue linearly
    } else {
      out = 0.5f * knee + (x - knee);
    }
    return copysignf(out, e);
  }
};
```

---

## Main Sketch (Pan + Tilt, SimpleFOC)

```cpp
#include <SimpleFOC.h>
#include "FollowAxis.h"

// ---------- Hardware ----------
BLDCMotor        panMotor(7), tiltMotor(7);   // gimbal motors, 7 pole pairs
BLDCDriver3PWM   panDrv(9, 5, 6, 8), tiltDrv(3, 10, 11, 7);
MagneticSensorI2C panEnc(AS5600_I2C), tiltEnc(AS5600_I2C); // use 2 buses/mux in practice

// ---------- Follow axes ----------
FollowConfig panCfg;                 // defaults tuned for yaw
FollowConfig tiltCfg = [] {
  FollowConfig c;
  c.enterDeadband = 4.0f * DEG_TO_RAD;
  c.tauFollow     = 0.45f;           // tilt slower = more cinematic
  c.maxRate       = 90.0f * DEG_TO_RAD;
  return c;
}();
FollowAxis panFollow(panCfg), tiltFollow(tiltCfg);

// ---------- IMU (camera head) ----------
// Replace with your fusion (Mahony/Madgwick/BMI270 game rotation vector)
struct Euler { float yaw, pitch, roll; };
Euler readCameraIMU();               // world-frame camera orientation, rad
float unwrapYaw(float y);            // continuous yaw

// Encoder zero offsets (motor angle when camera aligned with handle)
float panZero = 0, tiltZero = 0;

const uint32_t FOLLOW_PERIOD_US = 2000;  // 500 Hz follow loop
uint32_t lastFollow = 0;

void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorI2C& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 12;
  d.init();
  m.linkDriver(&d);
  m.foc_modulation    = FOCModulationType::SpaceVectorPWM;
  m.controller        = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;
  m.voltage_limit     = 6;
  m.velocity_limit    = 20;          // rad/s; follow axis limits the real rate
  m.PID_velocity.P = 0.15f; m.PID_velocity.I = 8.0f; m.PID_velocity.D = 0;
  m.LPF_velocity.Tf = 0.005f;
  m.P_angle.