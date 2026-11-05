# SimpleFOC Smooth Follow Mode: Exponential Rate Easing and Deadband Window

This is a handheld 3-axis gimbal follow controller in the style of the Osmo Pocket. It works as follows:

- The camera holds its orientation in the **world frame**.
- When the handle turns past a **deadband window**, the camera setpoint starts **following** the handle.
- The follow rate ramps in and out with **exponential (first-order) easing** and an acceleration cap, so pans start and stop smoothly.
- **Hysteresis** in the deadband prevents hunting. Once following starts, the camera re-centers almost fully, then releases.
- The math is **dt-independent**, so it behaves the same if the loop rate jitters.

I couldn't tell what "Cycle 7" refers to. I've treated it as a revision label, and the code is marked `rev 7`.

---

## 1. Follow Controller (header-only, per axis)

```cpp
// FollowAxis.h — Smooth follow controller, rev 7
#pragma once
#include <math.h>

static inline float wrapPi(float a) {
  while (a >  M_PI) a -= 2.0f * M_PI;
  while (a < -M_PI) a += 2.0f * M_PI;
  return a;
}

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

struct FollowParams {
  float deadbandEnter = 0.087f;  // rad (~5°): handle must exceed this to start following
  float deadbandExit  = 0.010f;  // rad (~0.6°): stop following once this close
  float gain          = 2.5f;    // 1/s, desired rate = gain * shaped error
  float expo          = 0.35f;   // 0 = linear, 1 = strongly cubic (soft near center)
  float maxRate       = 3.0f;    // rad/s cap on follow speed
  float maxAccel      = 8.0f;    // rad/s^2 cap on rate change
  float tauRise       = 0.18f;   // s, easing time constant when speeding up
  float tauFall       = 0.30f;   // s, easing time constant when slowing (longer = silkier stop)
  float stopRate      = 0.02f;   // rad/s, rate below which we may disengage
  bool  wrap          = true;    // yaw wraps, pitch doesn't
};

class FollowAxis {
public:
  FollowParams p;
  float setpoint = 0.0f;  // camera world-frame target (rad)
  float rate     = 0.0f;  // current eased follow rate (rad/s)
  bool  engaged  = false;

  void reset(float handleAngle) {
    setpoint = handleAngle;
    rate = 0;
    engaged = false;
  }

  // handleAngle: handle orientation in world frame (rad)
  // Returns the updated camera world-frame setpoint.
  float update(float handleAngle, float dt) {
    if (dt <= 0.0f || dt > 0.1f) return setpoint;  // reject bad timing

    float err = handleAngle - setpoint;
    if (p.wrap) err = wrapPi(err);
    float aerr = fabsf(err);

    // --- Deadband window with hysteresis ---
    if (!engaged && aerr > p.deadbandEnter) engaged = true;
    if (engaged && aerr < p.deadbandExit && fabsf(rate) < p.stopRate) engaged = false;

    // --- Shaped desired rate ---
    float desired = 0.0f;
    if (engaged) {
      // Soft-edged deadband: subtract the exit window, so there's no step at engage.
      float e = copysignf(fmaxf(aerr - p.deadbandExit, 0.0f), err);
      // Expo shaping: blend linear and cubic (normalized to the enter window).
      float n = e / p.deadbandEnter;
      float shaped = ((1.0f - p.expo) * n + p.expo * n * n * n) * p.deadbandEnter;
      desired = clampf(p.gain * shaped, -p.maxRate, p.maxRate);
    }

    // --- Exponential rate easing (dt-independent) ---
    bool speedingUp = fabsf(desired) > fabsf(rate) && (desired * rate >= 0.0f);
    float tau   = speedingUp ? p.tauRise : p.tauFall;
    float alpha = 1.0f - expf(-dt / tau);
    float target = rate + (desired - rate) * alpha;

    // --- Acceleration limit (removes residual jerk on big flicks) ---
    float dvMax = p.maxAccel * dt;
    rate += clampf(target - rate, -dvMax, dvMax);

    // --- Integrate setpoint ---
    setpoint += rate * dt;
    if (p.wrap) setpoint = wrapPi(setpoint);
    return setpoint;
  }
};
```

---

## 2. SimpleFOC Integration (yaw follow, pitch follow, roll locked)

```cpp
// GimbalFollow.ino — rev 7
#include <SimpleFOC.h>
#include "FollowAxis.h"

// ---- Hardware (adapt to your board) ----
BLDCMotor   motorYaw(7), motorPitch(7), motorRoll(7);
BLDCDriver3PWM drvYaw(9, 5, 6, 8), drvPitch(3, 10, 11, 8), drvRoll(A0, A1, A2, 8);
MagneticSensorI2C encYaw(AS5600_I2C), encPitch(AS5600_I2C), encRoll(AS5600_I2C);
// (Use separate I2C buses or SPI encoders in practice.)

// ---- IMU on the camera head: provide world-frame camera attitude ----
struct Attitude { float yaw, pitch, roll; };
Attitude readCameraAttitude();  // implement with your AHRS (Mahony/Madgwick on BMI270/ICM42688)

FollowAxis followYaw, followPitch;
unsigned long lastUs;

// Handle world attitude = camera attitude minus joint angles (small-angle approximation;
// for large pitch, use full rotation composition).
Attitude handleAttitude(const Attitude& cam) {
  return {
    wrapPi(cam.yaw - motorYaw.shaft_angle),
    cam.pitch - motorPitch.shaft_angle,
    cam.roll  - motorRoll.shaft_angle
  };
}

void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorI2C& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 8.0f;
  d.init();
  m.linkDriver(&d);
  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.controller = MotionControlType::angle;
  m.voltage_limit = 3.0f;
  m.velocity_limit = 20.0f;
  m.PID_velocity.P = 0.15f; m.PID_velocity.I = 4.0f; m.PID_velocity.D = 0.0f;
  m.LPF_velocity.Tf = 0.005f;
  m.P_angle.P = 18.0f;
  m.init();
  m.initFOC();
}

void setup() {
  Serial.begin(115200);
  setupMotor(motorYaw,   drvYaw,   encYaw);
  setupMotor(motorPitch, drvPitch, encPitch);
  setupMotor(motorRoll,  drvRoll,  encRoll);

  // Pitch: no wrap, gentler and slower than yaw (tilt pans read as more "cinematic" slow).
  followPitch.p.wrap = false;
  followPitch.p.gain = 1.8f;
  followPitch.p.maxRate = 1.5f;
  followPitch.p.deadbandEnter = 0.10f;

  Attitude cam = readCameraAttitude();
  Attitude h = handleAttitude(cam);
  followYaw.reset(h.yaw);
  followPitch.reset(h.pitch);
  lastUs = micros();
}

void loop() {
  // FOC inner loop: run as fast as possible.
  motorYaw.loopFOC(); motorPitch.loopFOC(); motorRoll.loopFOC();

  // Outer follow/stabilization loop at ~500 Hz.
  unsigned long now = micros();
  if (now - lastUs < 2000) return;
  float dt = (now - lastUs) * 1e-6f;
  lastUs = now;

  Attitude cam = readCameraAttitude();
  Attitude h   = handleAttitude(cam);

  float yawSp   = followYaw.update(h.yaw, dt);
  float pitchSp = followPitch.update(h.pitch, dt);
  float rollSp  = 0.0f;  // horizon lock

  // Joint target = desired camera world angle - handle world angle.
  // Use shaft_angle + error, so the joint target stays continuous across wraps.
  float yawJoint   = motorYaw.shaft_angle   + wrapPi(yawSp - cam.yaw);
  float pitchJoint = motorPitch.shaft_angle + (pitchSp - cam.pitch);
  float rollJoint  = motorRoll.shaft_angle  + (rollSp  - cam.roll);

  // Mechanical limits on pitch/roll.
  pitchJoint = clampf(pitchJoint, -1.4f, 1.4f);
  rollJoint  = clampf(rollJoint,  -0.6f, 0.6f);

  motorYaw.move(yawJoint);
  motorPitch.move(pitchJoint