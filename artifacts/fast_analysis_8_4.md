# SimpleFOC Smooth Follow Mode: Exponential Easing + Deadband Window

Below is a complete follow-mode controller for a handheld 3-axis gimbal built on **SimpleFOC**. It includes:

- An **exponential (time-constant) easing** follower that behaves the same at any loop rate.
- A **deadband window with hysteresis**. Small handle jitters are ignored. Once the camera starts following, it keeps following until it has fully settled, which avoids stutter at the edge of the deadband.
- A **soft deadband edge**, so the camera eases into motion instead of starting abruptly.
- **Velocity and acceleration limits** for smooth, cinematic pans.
- **Angle wrapping** so the yaw axis works correctly through 360°.
- **Per-axis profiles** for yaw (pan), pitch (tilt) and roll (horizon lock).

> "Cycle 8" isn't a standard SimpleFOC or DJI term, so I've treated it as your iteration label. If it refers to a specific requirement, tell me and I'll adapt the code.

---

## 1. Architecture

```
 Handle IMU (world attitude)
          │  handle_angle (rad)
          ▼
 ┌──────────────────────────┐
 │ SmoothFollow (per axis)  │  deadband → exp ease → vel/accel limit
 └──────────┬───────────────┘
            │ camera_target (world frame)
            ▼
 motor_setpoint = camera_target − handle_angle   (joint frame)
            │
            ▼
 SimpleFOC BLDCMotor (MotionControlType::angle)
            │
            ▼
 Camera holds a smooth world heading while the handle moves
```

---

## 2. `SmoothFollow.h`

```cpp
#pragma once
#include <Arduino.h>
#include <math.h>

// Wrap angle to (-PI, PI]
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a <= -PI) a += _2PI;
  return a;
}

struct FollowProfile {
  float deadband;        // rad: half-width of the inner window (no motion inside)
  float softZone;        // rad: width of the soft ramp outside the deadband
  float settleBand;      // rad: hysteresis; stop following once error < this
  float tau;             // s:   exponential easing time constant (larger = lazier)
  float maxVel;          // rad/s: velocity cap of the camera target
  float maxAccel;        // rad/s^2: acceleration cap (makes starts/stops smooth)
  bool  wrap;            // true for continuous yaw
  bool  enabled;         // false = lock mode (target held)
};

class SmoothFollow {
public:
  explicit SmoothFollow(const FollowProfile& p) : prof(p) {}

  void reset(float handleAngle) {
    target = handleAngle;
    vel = 0.0f;
    following = false;
  }

  void setProfile(const FollowProfile& p) { prof = p; }

  // dt in seconds; handleAngle = world-frame angle of the handle (rad)
  float update(float handleAngle, float dt) {
    if (dt <= 0.0f || dt > 0.1f) dt = 0.001f;   // guard against stalls/glitches

    if (!prof.enabled) {                         // lock mode: decelerate and hold
      vel = approach(vel, 0.0f, prof.maxAccel * dt);
      target += vel * dt;
      if (prof.wrap) target = wrapPi(target);
      return target;
    }

    float err = handleAngle - target;
    if (prof.wrap) err = wrapPi(err);
    float aerr = fabsf(err);

    // ---------- Deadband with hysteresis ----------
    // Enter following when error leaves the deadband;
    // exit only when error falls inside the (smaller) settle band.
    if (!following && aerr > prof.deadband)  following = true;
    if ( following && aerr < prof.settleBand && fabsf(vel) < 0.02f) following = false;

    float effErr = 0.0f;
    if (following) {
      // Inner band used while following = settleBand (tighter), so we finish centered
      float db = prof.settleBand;
      float over = aerr - db;
      if (over > 0.0f) {
        // Soft edge: smoothstep gain across softZone, then linear (gain 1)
        float g = 1.0f;
        if (prof.softZone > 1e-6f && over < prof.softZone) {
          float x = over / prof.softZone;
          g = x * x * (3.0f - 2.0f * x);
        }
        effErr = copysignf(over * g, err);
      }
    }

    // ---------- Exponential easing (frame-rate independent) ----------
    // Desired step this tick = effErr * (1 - e^(-dt/tau))
    float alpha   = 1.0f - expf(-dt / fmaxf(prof.tau, 1e-3f));
    float desVel  = (effErr * alpha) / dt;

    // ---------- Velocity & acceleration limiting ----------
    desVel = constrain(desVel, -prof.maxVel, prof.maxVel);
    vel    = approach(vel, desVel, prof.maxAccel * dt);

    // Don't overshoot the handle
    float step = vel * dt;
    if (fabsf(step) > fabsf(effErr) && following) {
      step = effErr;
      vel  = step / dt;
    }

    target += step;
    if (prof.wrap) target = wrapPi(target);
    return target;
  }

  float getTarget()   const { return target; }
  float getVelocity() const { return vel; }
  bool  isFollowing() const { return following; }

private:
  static float approach(float cur, float goal, float maxDelta) {
    float d = goal - cur;
    if (d >  maxDelta) d =  maxDelta;
    if (d < -maxDelta) d = -maxDelta;
    return cur + d;
  }

  FollowProfile prof;
  float target = 0.0f;
  float vel = 0.0f;
  bool following = false;
};
```

---

## 3. Main Firmware (one axis shown; repeat for pitch and roll)

```cpp
#include <SimpleFOC.h>
#include "SmoothFollow.h"

// ---------------- Hardware ----------------
BLDCMotor        yawMotor(7);                         // pole pairs (gimbal motor, e.g. 2204)
BLDCDriver3PWM   yawDriver(9, 10, 11, 8);
MagneticSensorSPI yawEnc(AS5048_SPI, 10);

// ---------------- Profiles (Osmo-Pocket-like feel) ----------------
FollowProfile YAW_PROFILE = {
  /*deadband  */ radians(3.0f),
  /*softZone  */ radians(6.0f),
  /*settleBand*/ radians(0.4f),
  /*tau       */ 0.45f,
  /*maxVel    */ radians(120.0f),
  /*maxAccel  */ radians(300.0f),
  /*wrap      */ true,
  /*enabled   */ true
};

FollowProfile PITCH_PROFILE = {
  radians(4.0f), radians(6.0f), radians(0.5f),
  0.60f, radians(90.0f), radians(250.0f),
  false, true
};

FollowProfile ROLL_PROFILE = {   // horizon lock
  0, 0, 0, 1.0f, 0, radians(200.0f), false, false
};

SmoothFollow yawFollow(YAW_PROFILE);

// ---------------- IMU stub: replace with your fusion (Mahony/Madgwick) ----------------
float readHandleYaw();   // world-frame handle yaw (rad), from the IMU on the handle

// Joint limits (pitch especially; yaw on a Pocket is limited too)
const float YAW_JOINT_MIN = radians(-150.0f);
const float YAW_JOINT_MAX = radians( 150.0f);

uint32_t lastMicros;

void setup() {
  Serial.begin(115200);

  yawEnc.init();
  yawMotor.linkSensor(&yawEnc);

  yawDriver.voltage_power_supply = 8.0f;   // 2S
  yawDriver.init();
  yawMotor.linkDriver(&yawDriver);

  yawMotor.foc_modulation = FOCModulationType::SpaceVectorPWM;
  yawMotor.controller     = MotionControlType::angle;
  yawMotor.torque_controller = TorqueControlType::voltage;

  // Gimbal-style tuning: soft position loop, damped velocity loop
  yawMotor.PID_velocity.P = 0.15f;
  yawMotor.PID_velocity.I = 2.0f;
  yawMotor.PID_velocity.D = 0.0f;
  yawMotor.LPF_velocity.Tf = 0.01f;
  yawMotor.P_angle.P = 18.0f;
  yawMotor.velocity_limit = 20.0f;
  yawMotor.voltage_limit  = 4.0f;

  yawMotor.init();
  yawMotor.initFOC();

  yawFollow.reset(readHandleYaw());
  lastMicros = micros();
}

void loop() {
  yawMotor.loopFOC();                       // run as fast as possible

  // Follow update at ~1 kHz
  uint32_t now = micros();
  float dt = (now - lastMicros) * 1e-6f;
  if (dt >= 0.001f) {
    lastMicros = now;

    float handleYaw = readHandleYaw();
    float camYaw    = yawFollow.update(handleYaw, dt);

    // Convert world camera heading → joint angle relative to the handle
    float joint = wrapPi(camYaw - handleYaw);
    joint = constrain(joint, YAW_JOINT_MIN, YAW_JOINT_MAX);

    // If we hit the joint limit, drag the target so it can't wind up
    if (joint == YAW_JOINT_MIN || joint == YAW_JOINT_MAX)
      yawFollow.reset(wrapPi(handleYaw + joint));

    yawMotor.move(joint);
  }