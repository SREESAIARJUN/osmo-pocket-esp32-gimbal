# SimpleFOC Smooth Follow Mode: Exponential Rate Easing + Deadband Window

This is a follow-mode controller for a handheld gimbal like the Osmo Pocket. The camera holds its world orientation until the handle moves past a deadband window. It then catches up with an eased velocity profile, with no jerk at start or stop. The code is labeled "Cycle 17" as a revision tag.

## Control concept

```
handle IMU ──► follow error e = wrap(handle − camTarget)
                     │
              ┌──────▼───────┐
              │ Deadband     │  hysteresis window + soft knee
              │ window       │  (no hunting, no step at the edge)
              └──────┬───────┘
                     │ e_eff
              ┌──────▼───────┐
              │ Rate law     │  ω_des = e_eff / τ_follow  (exponential approach)
              │ + max rate   │
              └──────┬───────┘
              ┌──────▼───────┐
              │ Rate easing  │  1st-order EMA (τ_ease) + accel limit
              └──────┬───────┘
                     │ ω_cam
              camTarget += ω_cam·dt   (world frame)
                     │
   joint target = camTarget − handle  ──► SimpleFOC velocity loop
   (+ feedforward ω_cam − ω_handle)
```

## `FollowAxis.h`

```cpp
// FollowAxis.h — Smooth follow mode, Cycle 17
#pragma once
#include <Arduino.h>
#include <math.h>

static inline float wrapPi(float a) {
  while (a >  PI) a -= TWO_PI;
  while (a < -PI) a += TWO_PI;
  return a;
}

struct FollowParams {
  float db_inner   = 0.035f;  // rad (~2°)   disengage threshold
  float db_outer   = 0.087f;  // rad (~5°)   engage threshold
  float knee       = 0.035f;  // rad         soft-knee width past db_inner
  float tau_follow = 0.45f;   // s           exponential catch-up time constant
  float tau_ease   = 0.18f;   // s           rate smoothing time constant
  float max_rate   = 2.6f;    // rad/s       (~150°/s)
  float max_accel  = 6.0f;    // rad/s^2
  float min_lim    = -1e9f;   // joint limits (rad, joint frame) — tilt uses real ones
  float max_lim    =  1e9f;
};

class FollowAxis {
public:
  FollowParams p;

  void reset(float handleAngle) {
    camTarget = handleAngle;
    rate = 0; engaged = false;
  }

  // handleAngle: handle orientation on this axis (world frame, rad)
  // returns camera rate (rad/s) for feedforward
  float update(float handleAngle, float dt) {
    if (dt <= 0 || dt > 0.05f) dt = 0.001f;              // guard against stalls

    float e  = wrapPi(handleAngle - camTarget);
    float ae = fabsf(e);

    // ---- Deadband window with hysteresis ----
    if (!engaged && ae > p.db_outer) engaged = true;
    if ( engaged && ae < p.db_inner && fabsf(rate) < 0.02f) engaged = false;

    // ---- Soft-knee effective error (C1-continuous at db_inner) ----
    float eEff = 0.0f;
    if (engaged) {
      float x = ae - p.db_inner;
      if (x > 0) {
        float k = p.knee;
        // quadratic knee for x<k, linear beyond -> no step in rate demand
        float mag = (x < k) ? (x * x) / (2.0f * k) : (x - 0.5f * k);
        eEff = copysignf(mag, e);
      }
    }

    // ---- Exponential rate law: ω = e/τ, clamped ----
    float wDes = eEff / p.tau_follow;
    wDes = constrain(wDes, -p.max_rate, p.max_rate);

    // ---- Exponential easing (dt-correct EMA) ----
    float alpha = 1.0f - expf(-dt / p.tau_ease);
    float wEased = rate + alpha * (wDes - rate);

    // ---- Acceleration limit (bounds jerk at take-off / landing) ----
    float dwMax = p.max_accel * dt;
    rate += constrain(wEased - rate, -dwMax, dwMax);

    // ---- Integrate world-frame camera target ----
    camTarget = wrapPi(camTarget + rate * dt);

    // ---- Respect mechanical joint limits (keep target reachable) ----
    float joint = wrapPi(camTarget - handleAngle);
    if (joint > p.max_lim) { camTarget = wrapPi(handleAngle + p.max_lim); rate = 0; }
    if (joint < p.min_lim) { camTarget = wrapPi(handleAngle + p.min_lim); rate = 0; }

    return rate;
  }

  float target() const { return camTarget; }
  bool  isEngaged() const { return engaged; }

private:
  float camTarget = 0;
  float rate = 0;
  bool  engaged = false;
};
```

## Main sketch (pan + tilt, SimpleFOC)

```cpp
// gimbal_follow_c17.ino
#include <SimpleFOC.h>
#include "FollowAxis.h"

// ---------- Hardware (adjust to your board) ----------
BLDCMotor  panMotor(7),  tiltMotor(7);
BLDCDriver3PWM panDrv(9, 10, 11, 8), tiltDrv(5, 6, 3, 7);
MagneticSensorSPI panEnc(AS5048_SPI, 10), tiltEnc(AS5048_SPI, 4);

FollowAxis panFollow, tiltFollow;

// Joint-angle outer loop (ours) -> SimpleFOC velocity loop (inner)
const float KP_JOINT = 18.0f;   // (rad/s)/rad
const float PAN_ZERO = 0.0f, TILT_ZERO = 0.0f;   // encoder offsets at joint = 0

// ---------- IMU interface (replace with your fusion: Mahony/Madgwick) ----------
struct HandleState { float yaw, pitch, yawRate, pitchRate; };
HandleState readHandleIMU();    // implement: world-frame handle orientation + gyro

void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 8.4;           // 2S Li-ion
  d.init();
  m.linkDriver(&d);
  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.controller     = MotionControlType::velocity;
  m.PID_velocity.P = 0.25f;
  m.PID_velocity.I = 8.0f;
  m.PID_velocity.D = 0.0f;
  m.PID_velocity.output_ramp = 1000;
  m.LPF_velocity.Tf = 0.008f;
  m.voltage_limit  = 4.0f;
  m.velocity_limit = 20.0f;
  m.init();
  m.initFOC();
}

uint32_t lastUs;

void setup() {
  Serial.begin(115200);
  setupMotor(panMotor,  panDrv,  panEnc);
  setupMotor(tiltMotor, tiltDrv, tiltEnc);

  // Tilt: slower and with hard limits, typical for pocket gimbals
  tiltFollow.p.tau_follow = 0.6f;
  tiltFollow.p.max_rate   = 1.6f;
  tiltFollow.p.min_lim    = radians(-90);
  tiltFollow.p.max_lim    = radians( 45);

  HandleState h = readHandleIMU();
  panFollow.reset(h.yaw);
  tiltFollow.reset(h.pitch);
  lastUs = micros();
}

static float jointCmd(BLDCMotor& m, float zero, float jointTarget,
                      float camRate, float handleRate) {
  float jointMeas = wrapPi(m.shaft_angle - zero);
  float err       = wrapPi(jointTarget - jointMeas);
  // feedforward: joint must move at (camera rate − handle rate)
  return (camRate - handleRate) + KP_JOINT * err;
}

void loop() {
  // FOC inner loop as fast as possible
  panMotor.loopFOC();
  tiltMotor.loopFOC();

  // Follow loop at ~1 kHz
  uint32_t now = micros();
  float dt = (now - lastUs) * 1e-6f;
  if (dt < 0.001f) return;
  lastUs = now;

  HandleState h = readHandleIMU();

  float wPan  = panFollow.update(h.yaw,   dt);
  float wTilt = tiltFollow.update(h.pitch, dt);

  float panJointTgt  = wrapPi(panFollow.target()  - h.yaw);
  float tiltJointTgt = wrapPi(tiltFollow.target() - h.pitch);

  panMotor.move (j