# SimpleFOC Smooth Follow Mode with Exponential Easing and Deadband

This is a follow-mode controller for a 3-axis handheld gimbal in the style of the Osmo Pocket. The camera ignores small hand jitter inside a deadband. Larger movements are followed with a smooth exponential ease-in and ease-out, and the speed and acceleration of that motion are capped.

## Architecture

```
 Handle motion ──► [Soft Deadband + Hysteresis] ──► [Exponential Rate Easing] ──► [Rate/Accel Limit]
                                                                                        │
                       Camera IMU (world attitude) ◄── Motor (SimpleFOC angle mode) ◄── followTarget
```

- **Follow target:** the camera's desired world-frame angle. It slowly chases the handle's heading.
- **Motor setpoint:** `followTarget − handleAngle`. This is the relative joint angle that holds the camera at the target.
- **Handle angle:** reconstructed as `cameraWorldAngle − jointAngle`, so only one IMU (on the camera) is needed.

## Code

```cpp
// =====================================================================
//  SimpleFOC Gimbal — Smooth Follow Mode
//  Exponential rate easing + soft deadband with hysteresis
// =====================================================================
#include <SimpleFOC.h>
#include <math.h>

// ---------------------------------------------------------------------
//  Utility
// ---------------------------------------------------------------------
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}

static inline float smoothstep01(float x) {
  x = constrain(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

// dt-independent exponential smoothing coefficient
static inline float expAlpha(float dt, float tau) {
  return (tau <= 1e-6f) ? 1.0f : 1.0f - expf(-dt / tau);
}

// ---------------------------------------------------------------------
//  Follow axis controller
// ---------------------------------------------------------------------
struct FollowAxisConfig {
  float deadband;      // rad  - error ignored inside this window
  float softZone;      // rad  - width of smooth ramp beyond deadband
  float releaseRatio;  // 0..1 - hysteresis: stop following below deadband*ratio
  float tauFollow;     // s    - exponential catch-up time constant (position)
  float tauRate;       // s    - easing of the rate itself (ease-in/out)
  float maxRate;       // rad/s
  float maxAccel;      // rad/s^2
  bool  enabled;       // false = lock axis (e.g. roll)
};

class FollowAxis {
public:
  FollowAxisConfig cfg;
  float target = 0.0f;   // world-frame follow target (rad)
  float rate   = 0.0f;   // current target velocity (rad/s)
  bool  active = false;  // hysteresis state

  explicit FollowAxis(const FollowAxisConfig& c) : cfg(c) {}

  void reset(float angle) { target = angle; rate = 0.0f; active = false; }

  // handleAngle: world-frame handle heading for this axis (rad)
  float update(float handleAngle, float dt) {
    if (!cfg.enabled || dt <= 0.0f) {
      rate = 0.0f;
      return target;
    }

    float err  = wrapPi(handleAngle - target);
    float aerr = fabsf(err);

    // --- Hysteresis deadband ------------------------------------------
    // Engage at deadband; release only once well inside it.
    // This gives the "catch, then settle" feel without chatter at the edge.
    float dbEngage  = cfg.deadband;
    float dbRelease = cfg.deadband * cfg.releaseRatio;
    if (!active && aerr > dbEngage)  active = true;
    if ( active && aerr < dbRelease) active = false;

    // --- Soft deadband shaping ----------------------------------------
    // Effective error rises from 0 with zero slope (smoothstep blend),
    // so there's no visible "kick" when the deadband is crossed.
    float effErr = 0.0f;
    if (active) {
      float db     = dbRelease;               // measure from release edge
      float excess = aerr - db;
      if (excess > 0.0f) {
        float blend = smoothstep01(excess / fmaxf(cfg.softZone, 1e-4f));
        effErr = copysignf(excess * blend, err);
      }
    }

    // --- Exponential rate easing --------------------------------------
    // Desired rate from a first-order approach: v = e / tau.
    // This decays exponentially as the target closes in (ease-out).
    float desiredRate = effErr / cfg.tauFollow;
    desiredRate = constrain(desiredRate, -cfg.maxRate, cfg.maxRate);

    // Ease the rate itself toward desiredRate (ease-in, removes jerk)
    float a     = expAlpha(dt, cfg.tauRate);
    float eased = rate + (desiredRate - rate) * a;

    // Acceleration limit (hard safety net on top of easing)
    float maxDv = cfg.maxAccel * dt;
    rate += constrain(eased - rate, -maxDv, maxDv);

    // Kill tiny residual drift once settled
    if (!active && fabsf(rate) < 1e-3f) rate = 0.0f;

    target = wrapPi(target + rate * dt);
    return target;
  }
};

// ---------------------------------------------------------------------
//  Hardware (adjust pins / pole pairs to your board)
// ---------------------------------------------------------------------
BLDCMotor        motorYaw(7),  motorPitch(7),  motorRoll(7);
BLDCDriver3PWM   drvYaw(9,10,11,8), drvPitch(5,6,3,7), drvRoll(PA8,PA9,PA10,PB1);
MagneticSensorI2C encYaw(AS5600_I2C), encPitch(AS5600_I2C), encRoll(AS5600_I2C);

// Joint zero offsets (camera centered) — calibrate once
float zeroYaw = 0, zeroPitch = 0, zeroRoll = 0;

// ---------------------------------------------------------------------
//  Follow tuning — "Osmo Pocket" style feel
// ---------------------------------------------------------------------
FollowAxis followYaw({
  .deadband = 0.052f,   // ~3°
  .softZone = 0.087f,   // ~5° ramp
  .releaseRatio = 0.35f,
  .tauFollow = 0.45f,   // smooth, cinematic pan
  .tauRate   = 0.18f,
  .maxRate   = 2.6f,    // ~150°/s
  .maxAccel  = 6.0f,
  .enabled   = true
});

FollowAxis followPitch({
  .deadband = 0.070f,   // ~4° (pitch jitter is more common)
  .softZone = 0.087f,
  .releaseRatio = 0.35f,
  .tauFollow = 0.60f,   // pitch slower than yaw
  .tauRate   = 0.22f,
  .maxRate   = 1.7f,
  .maxAccel  = 4.0f,
  .enabled   = true
});

FollowAxis followRoll({
  .deadband = 0, .softZone = 0, .releaseRatio = 0,
  .tauFollow = 1, .tauRate = 1, .maxRate = 0, .maxAccel = 0,
  .enabled = false      // horizon locked
});

// ---------------------------------------------------------------------
//  IMU interface (replace with your fusion: Mahony/Madgwick/BNO08x)
// ---------------------------------------------------------------------
struct Attitude { float yaw, pitch, roll; };  // camera world attitude (rad)
extern Attitude readCameraAttitude();         // implement for your IMU

// ---------------------------------------------------------------------
//  Motor setup
// ---------------------------------------------------------------------
void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorI2C& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 8.4f;            // 2S Li-ion
  d.init();
  m.linkDriver(&d);

  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.controller     = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;

  m.PID_velocity.P = 0.12f;
  m.PID_velocity.I = 4.0f;
  m.PID_velocity.D = 0.0f;
  m.LPF_velocity.Tf = 0.008f;
  m.P_angle.P      = 18.0f;
  m.velocity_limit = 12.0f;
  m.voltage_limit  = 4.0f;

  m.init();
  m.initFOC();
}

// ---------------------------------------------------------------------