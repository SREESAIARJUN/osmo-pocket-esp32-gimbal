# SimpleFOC Smooth Follow Mode: Exponential Easing with Deadband

This is a follow-mode controller for a handheld pan gimbal in the style of the Osmo Pocket. The camera holds its world heading while the handle moves. When the handle turns deliberately, the camera eases after it with an exponential approach. Small wobbles stay inside a deadband and are ignored.

I couldn't tell what "Cycle 16" refers to. I've treated it as the next revision in your sequence. If it means something specific, such as a 16-step control cycle or a 16 kHz loop, tell me and I'll adapt the code.

---

## 1. Control Concept

```
 handle yaw (ψh) ──► [ error = ψh − ψt ] ──► [ soft deadband w/ hysteresis ]
                                                      │
                                                      ▼
                                       [ exponential easing: rate = e_eff / τ ]
                                                      │
                                                      ▼
                                   [ velocity clamp ] → [ accel/jerk clamp ]
                                                      │
                                                      ▼
                                      ψt (camera target heading, world frame)
                                                      │
       motor target angle  θm* = wrap(ψt − ψh) ───────┘
                                                      ▼
                                    SimpleFOC angle loop (MotionControlType::angle)
```

- **ψh** is the handle heading. It is estimated as `camera_yaw_imu − motor_angle`, or read from a handle IMU.
- **ψt** is the camera's commanded world heading, which is the smoothed follow target.
- The motor angle is the camera heading relative to the handle. Driving it to `ψt − ψh` therefore keeps the camera on ψt.

### Exponential Easing

The continuous form is a first-order lag:

```
dψt/dt = (ψh − ψt) / τ
```

Its exact discrete-time version is independent of loop-rate jitter:

```
α  = 1 − exp(−dt / τ)
ψt += α · e
```

### Soft Deadband

A hard deadband makes the camera jerk the moment the error leaves the window. Instead, the window is subtracted out:

```
e_eff = sign(e) · max(0, |e| − DB)
```

This keeps the motion continuous at the edge of the window. Hysteresis is added on top:

- The follower **engages** when `|e| > DB_enter`.
- It **disengages** only when `|e| < DB_exit`, with `DB_exit < DB_enter`.
- While engaged, it keeps converging to near zero error, the way a real Pocket "settles."

---

## 2. Code

### `SmoothFollow.h`

```cpp
#pragma once
#include <Arduino.h>
#include <math.h>

// Wrap angle to (-PI, PI]
static inline float wrapPi(float a) {
  a = fmodf(a + PI, _2PI);
  if (a < 0) a += _2PI;
  return a - PI;
}

struct SmoothFollowConfig {
  float tau          = 0.35f;   // s, easing time constant (smaller = snappier)
  float dbEnter      = 0.10f;   // rad (~5.7°) error needed to start following
  float dbExit       = 0.015f;  // rad (~0.9°) error at which follow disengages
  float dbSoft       = 0.06f;   // rad, soft-subtracted window while engaged
  float maxRate      = 3.0f;    // rad/s, max camera pan speed
  float maxAccel     = 8.0f;    // rad/s^2, accel limit (smooth start/stop)
  float engageRampT  = 0.25f;   // s, gain fade-in after engaging
  float recenterTau  = 0.12f;   // s, tau used during recenter command
};

class SmoothFollow {
public:
  explicit SmoothFollow(const SmoothFollowConfig& c = SmoothFollowConfig()) : cfg(c) {}

  void reset(float handleYaw) {
    target = handleYaw;
    rate = 0; engaged = false; engageGain = 0; recentering = false;
  }

  void recenter() { recentering = true; engaged = true; }

  // dt in seconds, handleYaw in rad (world frame). Returns camera target heading.
  float update(float handleYaw, float dt) {
    if (dt <= 0 || dt > 0.1f) dt = 0.001f;           // guard against stalls
    float e    = wrapPi(handleYaw - target);
    float aerr = fabsf(e);

    // ---- Deadband state machine with hysteresis ----
    if (!engaged && aerr > cfg.dbEnter) { engaged = true; }
    if (engaged && !recentering && aerr < cfg.dbExit && fabsf(rate) < 0.05f) {
      engaged = false;
    }
    if (recentering && aerr < cfg.dbExit) recentering = false;

    // Engage gain fade (prevents step at engagement)
    float gTarget = engaged ? 1.0f : 0.0f;
    float gStep   = dt / cfg.engageRampT;
    engageGain   += constrain(gTarget - engageGain, -gStep, gStep);

    // ---- Soft deadband: subtract window, continuous at edge ----
    float db   = recentering ? 0.0f : cfg.dbSoft * (1.0f - engageGain * 0.8f);
    // window shrinks as follow engages -> settles close to zero
    float eEff = (aerr > db) ? copysignf(aerr - db, e) : 0.0f;

    // ---- Exponential easing (exact discretization) ----
    float tau   = recentering ? cfg.recenterTau : cfg.tau;
    float alpha = 1.0f - expf(-dt / tau);
    float desiredRate = engageGain * (alpha * eEff) / dt;  // rad/s

    // ---- Velocity clamp ----
    desiredRate = constrain(desiredRate, -cfg.maxRate, cfg.maxRate);

    // ---- Acceleration clamp (S-curve-ish smoothness) ----
    float dvMax = cfg.maxAccel * dt;
    rate += constrain(desiredRate - rate, -dvMax, dvMax);

    // Don't overshoot the handle
    float step = rate * dt;
    if (fabsf(step) > aerr && (step * e) > 0) { step = e; rate = e / dt; }

    target = wrapPi(target + step);
    return target;
  }

  float getTarget() const { return target; }
  float getRate()   const { return rate; }
  bool  isEngaged() const { return engaged; }

  SmoothFollowConfig cfg;

private:
  float target = 0, rate = 0, engageGain = 0;
  bool  engaged = false, recentering = false;
};
```

### `main.cpp` (SimpleFOC pan axis)

```cpp
#include <SimpleFOC.h>
#include "SmoothFollow.h"

// ---------- Hardware (adjust to your board) ----------
BLDCMotor motor(7);                                    // gimbal motor pole pairs
BLDCDriver3PWM driver(9, 10, 11, 8);
MagneticSensorSPI encoder(AS5048_SPI, 10);             // motor-relative angle

// Camera IMU yaw source (implement for your IMU / AHRS)
float readCameraYaw();   // rad, world frame, e.g. from Madgwick/Mahony fusion

SmoothFollow follow;
Commander command(Serial);

const float CONTROL_HZ = 1000.0f;                      // follow loop rate
uint32_t lastUs = 0;
float motorZero = 0;                                   // mechanical center

void onTau(char* c)    { command.scalar(&follow.cfg.tau, c); }
void onDB(char* c)     { command.scalar(&follow.cfg.dbEnter, c); }
void onRate(char* c)   { command.scalar(&follow.cfg.maxRate, c); }
void onRecenter(char*) { follow.recenter(); }

void setup() {
  Serial.begin(115200);
  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 8.4;                   // 2S
  driver.init();
  motor.linkDriver(&driver);

  motor.foc_modulation   = FOCModulationType::SpaceVectorPWM;
  motor.controller       = MotionControlType::angle;
  motor.torque_controller = TorqueControlType::voltage;

  // Gimbal motors: high-resistance, voltage mode, stiff but quiet tuning
  motor.voltage_limit        = 4.0;
  motor.PID_velocity.P       = 0.15;
  motor.PID_velocity.I       = 4.0;
  motor.PID_velocity.D       = 0.0;
  motor.PID_velocity.output_ramp = 300;
  motor.LPF_velocity.Tf      = 0.008;
  motor.P_angle.P            = 18.0;
  motor.velocity_limit       = 8.0;

  motor.init();
  motor.initFOC();

  motorZero = motor.shaft_angle;
  float handleYaw = wrapPi(readCameraYaw() - (motor.shaft_angle - motorZero));
  follow.reset(handleYaw);

  command.add('T', onTau,      "follow tau [s]");
  command.add('D', onDB,       "deadband enter [rad]");
  command.add('R', onRate,     "max pan rate [rad/s]");
  command.add('C', onRecenter, "recenter");
  lastUs = micros();
}

void loop() {
  motor.loopFOC();                                     // run as fast as possible

  uint32_t now = micros();
  float dt = (now - lastUs) * 1e-6f;
  if (dt >= 1.0f / CONTROL_HZ) {
    lastUs = now;

    float motorRel  = motor.shaft_angle - mot