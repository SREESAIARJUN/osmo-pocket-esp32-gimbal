# SimpleFOC Smooth Follow Mode: Exponential Easing and Deadband

This is a follow-mode controller for a pocket-style 3-axis handheld gimbal (yaw, pitch, roll). Roll stays locked level. Yaw and pitch follow the handle smoothly, using:

- **A soft deadband with hysteresis.** Small hand jitter is ignored, and there is no "snap" when you leave the window.
- **Exponential rate easing.** The ease is frame-rate independent (`1 - exp(-dt/τ)`), so it behaves the same at any loop rate.
- **Velocity and acceleration limits.** Pans start and stop like a fluid head.
- **Angle wrapping.** Yaw works through ±180°.
- **Selectable modes.** Follow, Lock, FPV and Recenter.

"Cycle 11" isn't defined anywhere I can see. Below is a self-contained revision. Tell me what changed in your previous cycle and I'll align this to it.

---

## 1. Architecture

```
 Camera IMU ──► camera world attitude (yaw, pitch, roll)
 Encoders  ──► motor joint angles
                     │
                     ▼
   handle_world = camera_world − joint_angle      (per axis, small-angle decoupled)
                     │
                     ▼
   FollowAxis: target_world eases toward handle_world
                     │  (deadband → exp easing → accel/rate limit)
                     ▼
   joint_cmd = wrap(target_world − handle_world)
                     │
                     ▼
   SimpleFOC motor.move(joint_cmd)   [MotionControlType::angle]
```

The FOC angle loop holds the joint. The follow layer only decides where in world space the camera should point.

---

## 2. Follow Axis Controller

```cpp
// FollowAxis.h
#pragma once
#include <math.h>

static inline float wrapPi(float a) {
  while (a >  PI) a -= TWO_PI;
  while (a < -PI) a += TWO_PI;
  return a;
}

struct FollowParams {
  float deadband_enter;   // rad: error needed to START following
  float deadband_exit;    // rad: error below which we STOP (< enter → hysteresis)
  float tau_follow;       // s: time constant of position easing (error → rate)
  float tau_rate;         // s: time constant of rate smoothing (ease-in/out feel)
  float max_rate;         // rad/s
  float max_accel;        // rad/s^2
  float min_angle, max_angle; // world limits (pitch); set ±1e9 to disable (yaw)
  bool  wraps;            // true for yaw
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float world_angle) {
    target = world_angle; rate = 0; following = false;
  }

  // handle_world: where the handle points (rad, world frame)
  // returns target camera world angle
  float update(float handle_world, float dt) {
    if (dt <= 0.0f || dt > 0.1f) return target;      // guard against stalls

    float err = handle_world - target;
    if (P.wraps) err = wrapPi(err);
    float aerr = fabsf(err);

    // --- Hysteretic deadband --------------------------------------
    if (!following && aerr > P.deadband_enter) following = true;
    if ( following && aerr < P.deadband_exit && fabsf(rate) < 0.02f) following = false;

    // --- Soft (continuous) deadband: subtract window, no jump -----
    float e_eff = 0.0f;
    if (following) {
      float db = P.deadband_exit;                      // continuous at exit edge
      e_eff = (aerr > db) ? copysignf(aerr - db, err) : 0.0f;
    }

    // --- Exponential easing: desired rate proportional to error ---
    //     (first-order system → exponential approach to handle)
    float rate_des = e_eff / P.tau_follow;
    rate_des = constrain(rate_des, -P.max_rate, P.max_rate);

    // --- Exponential rate smoothing (frame-rate independent) ------
    float k = 1.0f - expf(-dt / P.tau_rate);
    float rate_next = rate + (rate_des - rate) * k;

    // --- Acceleration limit (removes jerk on start/stop) ----------
    float dv_max = P.max_accel * dt;
    rate += constrain(rate_next - rate, -dv_max, dv_max);

    // --- Integrate ------------------------------------------------
    target += rate * dt;
    if (P.wraps) target = wrapPi(target);
    else {
      if (target > P.max_angle) { target = P.max_angle; if (rate > 0) rate = 0; }
      if (target < P.min_angle) { target = P.min_angle; if (rate < 0) rate = 0; }
    }
    return target;
  }

  // Smoothly drive target to an absolute world angle (recenter)
  float driveTo(float world_goal, float dt) {
    following = true;
    return update(world_goal, dt);
  }

  float target = 0, rate = 0;
  bool following = false;
  FollowParams P;
};
```

**Why this feels "cinematic":**

- The `e/τ` stage gives an exponential approach. Speed is proportional to remaining distance, so the camera decelerates gracefully into the final framing.
- The `tau_rate` low-pass plus the accel limit gives an S-curve ease-in when a pan starts. The camera doesn't lurch the moment you exceed the deadband.
- Subtracting the window (`aerr - db`) keeps the commanded rate continuous at the deadband edge.

---

## 3. Gimbal Firmware (SimpleFOC)

```cpp
// main.cpp
#include <SimpleFOC.h>
#include "FollowAxis.h"

// ---------------- Hardware (adapt pins/drivers to your board) -----
BLDCMotor   motorYaw(7), motorPitch(7), motorRoll(7);   // pole pairs
BLDCDriver3PWM drvYaw(9,10,11,8), drvPitch(3,5,6,7), drvRoll(12,13,14,15);
MagneticSensorSPI encYaw(AS5048_SPI, 20), encPitch(AS5048_SPI, 21), encRoll(AS5048_SPI, 22);

// ---------------- IMU (camera head) -------------------------------
struct Attitude { float yaw, pitch, roll; };      // rad, world frame
Attitude readCameraIMU();                          // your AHRS (Mahony/Madgwick) here

// ---------------- Follow tuning -----------------------------------
FollowParams yawP {
  .deadband_enter = radians(4.0f),
  .deadband_exit  = radians(1.5f),
  .tau_follow     = 0.45f,       // larger = lazier, more cinematic
  .tau_rate       = 0.12f,
  .max_rate       = radians(120.0f),
  .max_accel      = radians(300.0f),
  .min_angle = -1e9f, .max_angle = 1e9f,
  .wraps = true
};
FollowParams pitchP {
  .deadband_enter = radians(3.0f),
  .deadband_exit  = radians(1.0f),
  .tau_follow     = 0.35f,
  .tau_rate       = 0.10f,
  .max_rate       = radians(90.0f),
  .max_accel      = radians(250.0f),
  .min_angle = radians(-90.0f), .max_angle = radians(35.0f),
  .wraps = false
};

FollowAxis yawAx(yawP), pitchAx(pitchP);

enum Mode { FOLLOW, LOCK, FPV, RECENTER } mode = FOLLOW;

// Joint zero offsets (camera level & forward when joint == 0)
float yawZero = 0, pitchZero = 0, rollZero = 0;

void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 8.4;   // 2S
  d.init();
  m.linkDriver(&d);
  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.controller     = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;
  m.voltage_limit  = 4.0;
  m.velocity_limit = 20;
  // Angle P / velocity PI: gimbal motors like soft, damped loops
  m.P_angle.P = 18;
  m.PID_velocity.P = 0.15;
  m.PID_velocity.I = 4;
  m.PID_velocity.D = 0;
  m.LPF_velocity.Tf = 0.01;
  m.init();
  m.initFOC();
}

void setup() {
  Serial.begin(115200);
  setupMotor(motorYaw,   drvYaw,   encYaw);
  setupMotor(motorPitch, drvPitch, encPitch);
  setupMotor(motorRoll,  drvRoll,  encRoll);

  Attitude cam = readCameraIMU();
  yawAx.reset(cam.yaw);
  pitchAx.reset(cam.pitch);
}

static uint32_t lastUs = 0;
static uint32