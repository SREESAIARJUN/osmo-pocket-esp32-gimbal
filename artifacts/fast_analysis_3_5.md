# SimpleFOC Smooth Follow Mode: Exponential Easing + Deadband (Cycle 3)

This is a per-axis follow controller for a handheld 2- or 3-axis gimbal in the style of the Osmo Pocket. The camera holds its world orientation (stabilization). Its heading then *drifts* toward the handle's heading using:

1. **A soft deadband window with hysteresis.** Small wrist jitter is ignored. Once a follow starts, the window shrinks so the camera fully re-centers instead of stopping at the window's edge.
2. **Exponential rate easing.** The commanded follow rate eases in and out with a first-order exponential (frame-rate independent), plus acceleration and rate caps. Pans start and stop without visible "steps."

---

## 1. Control Concept

```
handle IMU (world)          joint encoder (motor shaft vs handle)
      │                                │
      ▼                                │
  handleAngle ──► FollowAxis ──► cameraTarget (world frame, eased)
                                       │
          jointTarget = cameraTarget − handleAngle   (wrapped, clamped)
                                       │
                                       ▼
                       SimpleFOC angle loop → BLDC motor
```

- **Stabilization:** with `cameraTarget` held constant, the motor cancels handle motion.
- **Follow:** `cameraTarget` slowly chases `handleAngle + userOffset`.

---

## 2. `SmoothFollow.h`

```cpp
#pragma once
#include <Arduino.h>
#include <math.h>

// ---------- helpers ----------
static inline float wrapPi(float a) {
  while (a >  PI) a -= TWO_PI;
  while (a < -PI) a += TWO_PI;
  return a;
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float signf(float x) { return (x > 0) - (x < 0); }

// ---------- tuning per axis ----------
struct FollowConfig {
  // Deadband window (rad)
  float deadbandEnter   = 0.087f;  // ~5.0°  error needed to START following
  float deadbandExit    = 0.009f;  // ~0.5°  error at which follow STOPS (hysteresis)
  float deadbandSoft    = 0.035f;  // ~2.0°  quadratic blend width (no step at edge)

  // Exponential easing
  float tauPosition     = 0.45f;   // s  error → desired rate time constant
  float tauRate         = 0.18f;   // s  smoothing of the rate itself (ease in/out)

  // Hard limits
  float maxRate         = 2.5f;    // rad/s   (~143°/s) top follow speed
  float maxAccel        = 8.0f;    // rad/s²  jerk-free start/stop cap

  // Mechanical joint limits (relative to handle), rad
  float jointMin        = -PI;     // pan: effectively unlimited with slip ring
  float jointMax        =  PI;
  bool  wraps           = true;    // pan = true, tilt/roll = false
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowConfig& c) : cfg(c) {}

  void reset(float handleAngle, float jointAngle) {
    cameraTarget = cfg.wraps ? wrapPi(handleAngle + jointAngle)
                             : handleAngle + jointAngle;
    rate = 0.0f;
    following = false;
  }

  void setEnabled(bool en) { enabled = en; if (!en) { rate = 0; following = false; } }
  void setUserOffset(float off) { userOffset = off; }   // joystick / recenter offset

  // Returns joint (motor) target angle in rad, relative to handle.
  float update(float handleAngle, float dt) {
    if (dt <= 0.0f || dt > 0.1f) dt = 0.001f;           // guard against stalls

    // 1. Raw follow error (world frame)
    float goal = handleAngle + userOffset;
    float err  = cfg.wraps ? wrapPi(goal - cameraTarget) : (goal - cameraTarget);
    float aerr = fabsf(err);

    // 2. Deadband window with hysteresis
    if (!following && aerr > cfg.deadbandEnter) following = true;
    if ( following && aerr < cfg.deadbandExit && fabsf(rate) < 0.02f) following = false;

    float window = following ? cfg.deadbandExit : cfg.deadbandEnter;
    float effErr = softDeadband(err, window, cfg.deadbandSoft);

    // 3. Exponential easing: error → desired rate (first-order, time-constant based)
    float desiredRate = enabled ? effErr / cfg.tauPosition : 0.0f;
    desiredRate = clampf(desiredRate, -cfg.maxRate, cfg.maxRate);

    //    Rate eases toward desired with exp(-dt/tau): frame-rate independent
    float alpha   = 1.0f - expf(-dt / cfg.tauRate);
    float newRate = rate + alpha * (desiredRate - rate);

    //    Acceleration cap (prevents a "kick" on large sudden handle turns)
    float maxStep = cfg.maxAccel * dt;
    rate += clampf(newRate - rate, -maxStep, maxStep);

    // 4. Integrate the world-frame camera target (never overshoot the goal)
    float step = rate * dt;
    if (fabsf(step) > aerr && signf(step) == signf(err)) step = err;
    cameraTarget += step;
    if (cfg.wraps) cameraTarget = wrapPi(cameraTarget);

    // 5. Convert to joint target and respect mechanical limits
    float joint = cameraTarget - handleAngle;
    if (cfg.wraps) joint = wrapPi(joint);

    float clamped = clampf(joint, cfg.jointMin, cfg.jointMax);
    if (clamped != joint) {
      // At a hard stop the camera is dragged with the handle: back-solve the target
      cameraTarget = handleAngle + clamped;
      rate = 0.0f;
    }
    return clamped;
  }

  float getRate()   const { return rate; }
  bool  isFollowing() const { return following; }

private:
  // Soft deadband: zero inside the window, quadratic blend over `soft`, then linear.
  // C1-continuous, so there is no step in commanded rate at the window edge.
  static float softDeadband(float e, float window, float soft) {
    float a = fabsf(e);
    if (a <= window) return 0.0f;
    float x = a - window;
    float out = (x < soft) ? (x * x) / (2.0f * soft)   // smooth ramp
                           : x - soft * 0.5f;          // linear region
    return signf(e) * out;
  }

  FollowConfig cfg;
  float cameraTarget = 0.0f;
  float rate         = 0.0f;
  float userOffset   = 0.0f;
  bool  following    = false;
  bool  enabled      = true;
};
```

---

## 3. Arduino / SimpleFOC Sketch (Pan + Tilt)

```cpp
#include <SimpleFOC.h>
#include "SmoothFollow.h"

// ----- hardware (adjust pins / driver to your board) -----
BLDCMotor        panMotor  = BLDCMotor(7);          // gimbal motor, 7 pole pairs
BLDCDriver3PWM   panDriver = BLDCDriver3PWM(9, 5, 6, 8);
MagneticSensorSPI panEnc   = MagneticSensorSPI(AS5048_SPI, 10);

BLDCMotor        tiltMotor  = BLDCMotor(7);
BLDCDriver3PWM   tiltDriver = BLDCDriver3PWM(3, 11, 13, 7);
MagneticSensorSPI tiltEnc   = MagneticSensorSPI(AS5048_SPI, 4);

// ----- follow configs -----
FollowConfig panCfg;                 // defaults: wrapping, smooth pans

FollowConfig makeTiltCfg() {
  FollowConfig c;
  c.wraps         = false;
  c.jointMin      = -radians(90);
  c.jointMax      =  radians(45);
  c.deadbandEnter =  radians(7);     // tilt is usually lazier than pan
  c.tauPosition   = 0.6f;
  c.maxRate       = 1.5f;
  return c;
}
FollowConfig tiltCfg = makeTiltCfg();

FollowAxis panFollow(panCfg);
FollowAxis tiltFollow(tiltCfg);

// ----- IMU on the HANDLE (implement with your sensor + fusion filter) -----
// Must return world-frame yaw/pitch in radians (e.g. Mahony/Madgwick output).
float readHandleYaw();
float readHandlePitch();

void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 12;
  d.init();
  m.linkDriver(&d);