# SimpleFOC Smooth Follow Mode, Cycle 1

This first cycle implements **pan and tilt follow** for a handheld gimbal like the Osmo Pocket. It uses three stages:

1. **Soft deadband window** with hysteresis. Small handle jitters and hand tremor are ignored, and engaging follow causes no jerk.
2. **Exponential rate easing.** The follow speed rises smoothly with error and saturates at a maximum. The rate itself is low-pass eased, so starts and stops feel cinematic.
3. **Acceleration clamp and SimpleFOC angle loop.** These turn the world-frame setpoint into a motor target.

---

## 1. Concept

```
 handle IMU ──► handleAngle (world)
                     │
                     ▼
   error = wrap(handleAngle − camSetpoint)
                     │
          ┌──────────┴──────────┐
          │  SOFT DEADBAND +    │   |e| < inner  → idle
          │  HYSTERESIS WINDOW  │   |e| > outer  → engage
          └──────────┬──────────┘   eff = sign(e)·max(0,|e|−inner)
                     ▼
   desiredRate = maxRate · (1 − e^(−|eff|/kCurve)) · sign(eff)     ← exponential response
                     ▼
   rate += (desiredRate − rate) · (1 − e^(−dt/τ))                 ← exponential easing
                     ▼
   accel clamp → camSetpoint += rate·dt
                     ▼
   motor.target = wrap(camSetpoint − handleAngle)  (relative joint angle)
```

Subtracting the inner deadband from the error makes the response **continuous at the window edge**. The rate starts from exactly zero, so the camera never lurches when following begins.

---

## 2. Code (Arduino / SimpleFOC ≥ 2.3)

```cpp
#include <SimpleFOC.h>

// ============================================================
//  HARDWARE (adjust to your board)
// ============================================================
// Pan axis
BLDCMotor        panMotor  = BLDCMotor(7);                 // pole pairs
BLDCDriver3PWM   panDriver = BLDCDriver3PWM(9, 10, 11, 8);
MagneticSensorSPI panEnc   = MagneticSensorSPI(AS5048_SPI, 10);

// Tilt axis
BLDCMotor        tiltMotor  = BLDCMotor(7);
BLDCDriver3PWM   tiltDriver = BLDCDriver3PWM(5, 6, 3, 7);
MagneticSensorSPI tiltEnc   = MagneticSensorSPI(AS5048_SPI, 4);

// ============================================================
//  UTILS
// ============================================================
static inline float wrapPi(float a) {
  while (a >  _PI) a -= _2PI;
  while (a < -_PI) a += _2PI;
  return a;
}
static inline float signf(float x) { return (x > 0) - (x < 0); }

// ============================================================
//  FOLLOW AXIS: deadband + exponential rate easing
// ============================================================
struct FollowParams {
  float dbInner;      // rad, release window (stop following inside this)
  float dbOuter;      // rad, engage window (start following beyond this)
  float maxRate;      // rad/s, top follow speed
  float kCurve;       // rad, error at which rate reaches ~63% of maxRate
  float tauRate;      // s, rate easing time constant (smoothness)
  float maxAccel;     // rad/s^2, accel clamp
  bool  wrap;         // continuous axis (pan) or limited (tilt)
  float minAngle;     // limits for non-wrapping axis (world-relative joint)
  float maxAngle;
};

class FollowAxis {
public:
  FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float handleAngle) {
    setpoint = handleAngle;
    rate = 0;
    engaged = false;
  }

  // handleAngle: world-frame handle angle from IMU (rad)
  // returns world-frame camera setpoint
  float update(float handleAngle, float dt) {
    if (dt <= 0 || dt > 0.1f) return setpoint;        // guard bad dt

    // 1) error between where handle points and where camera points
    float err = handleAngle - setpoint;
    if (P.wrap) err = wrapPi(err);
    float aErr = fabsf(err);

    // 2) hysteresis deadband window
    if (!engaged && aErr > P.dbOuter) engaged = true;
    if ( engaged && aErr < P.dbInner && fabsf(rate) < 0.01f) engaged = false;

    // 3) soft deadband: continuous at edge, zero inside
    float eff = engaged ? signf(err) * fmaxf(0.0f, aErr - P.dbInner) : 0.0f;

    // 4) exponential response curve: gentle near 0, saturates at maxRate
    float desired = P.maxRate * (1.0f - expf(-fabsf(eff) / P.kCurve)) * signf(eff);

    // 5) exponential easing of rate (frame-rate independent)
    float alpha  = 1.0f - expf(-dt / P.tauRate);
    float target = rate + (desired - rate) * alpha;

    // 6) acceleration clamp
    float dMax = P.maxAccel * dt;
    rate += _constrain(target - rate, -dMax, dMax);

    // 7) prevent overshoot past the handle in a single step
    float step = rate * dt;
    if (fabsf(step) > aErr && signf(step) == signf(err)) {
      step = err;
      rate = 0;
    }

    // 8) integrate setpoint
    setpoint += step;
    if (P.wrap) setpoint = wrapPi(setpoint);

    return setpoint;
  }

  // joint target the motor must hold so the camera stays at setpoint
  float jointTarget(float handleAngle) const {
    float j = setpoint - handleAngle;
    if (P.wrap) return wrapPi(j);
    return _constrain(j, P.minAngle, P.maxAngle);
  }

  FollowParams P;
  float setpoint = 0;
  float rate     = 0;
  bool  engaged  = false;
};

// ============================================================
//  TUNING: "cinematic" defaults
// ============================================================
FollowParams panParams = {
  .dbInner  = 1.5f  * DEG_TO_RAD,
  .dbOuter  = 4.0f  * DEG_TO_RAD,
  .maxRate  = 120.0f* DEG_TO_RAD,
  .kCurve   = 25.0f * DEG_TO_RAD,
  .tauRate  = 0.35f,
  .maxAccel = 300.0f* DEG_TO_RAD,
  .wrap     = true,
  .minAngle = 0, .maxAngle = 0
};

FollowParams tiltParams = {
  .dbInner  = 2.0f  * DEG_TO_RAD,
  .dbOuter  = 5.0f  * DEG_TO_RAD,
  .maxRate  = 80.0f * DEG_TO_RAD,
  .kCurve   = 20.0f * DEG_TO_RAD,
  .tauRate  = 0.45f,
  .maxAccel = 200.0f* DEG_TO_RAD,
  .wrap     = false,
  .minAngle = -90.0f * DEG_TO_RAD,
  .maxAngle =  45.0f * DEG_TO_RAD
};

FollowAxis panFollow(panParams);
FollowAxis tiltFollow(tiltParams);

// Joint zero offsets (encoder reading when camera faces handle-forward)
float panZero  = 0.0f;
float tiltZero = 0.0f;

// ============================================================
//  IMU STUB: replace with your fusion (Mahony/Madgwick, BMI270, ICM42688...)
//  Must return handle orientation in world frame, radians.
// ============================================================
void readHandleYawPitch(float& yaw, float& pitch) {
  // TODO: IMU fusion on handle body
  yaw = 0; pitch = 0;
}

// ============================================================
//  SETUP
// ============================================================
void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 12;
  d.init();
  m.linkDriver(&d);

  m.foc_modulation   = FOCModulationType::SpaceVectorPWM;
  m.controller       = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;

  // gimbal motors: soft, low-voltage, high-damping tuning
  m.voltage_limit   = 6;
  m.velocity_limit  = 20;
  m.PID_velocity.P  = 0.15f;
  m.PID_velocity.I  = 2.0f;
  m.PID_velocity.D  = 0;
  m.PID_velocity.output_ramp = 1000;
  m