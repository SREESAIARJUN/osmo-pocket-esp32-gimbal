# SimpleFOC Smooth Follow Mode: Exponential Easing + Deadband Window

This implements an Osmo Pocket style pan/tilt follow mode. The camera holds its world orientation for small hand movements. When the handle moves past a deadband window, the camera eases toward the new heading with no snap or jerk. (I've treated "Cycle 26" as your iteration label. If it refers to a specific spec, send the parameters and I'll align the defaults.)

## Control Architecture

```
 IMU (handle orientation) ──┐
                            ▼
              ┌───────────────────────────┐
              │  error = handle − camTgt  │  (wrapped to ±π)
              │  soft deadband + hysteresis│
              │  desired rate = Kf·e_eff  │
              │  exp easing on rate (τ)   │
              │  accel + max-rate clamps  │
              │  camTgt += rate·dt        │
              └────────────┬──────────────┘
                           ▼
         joint cmd = camTgt − handle  (stabilization)
                           ▼
        SimpleFOC angle loop → motor.move(joint)
```

Key design points:

- **Frame-rate independent easing.** `alpha = 1 − exp(−dt/τ)` instead of a fixed lerp factor, so behaviour is identical at 200 Hz or 1 kHz.
- **Soft deadband.** A smoothstep blend zone removes the "kink" at the deadband edge, which otherwise shows up as a visible micro-jerk in footage.
- **Hysteresis.** Follow engages at `dbEnter` and only disengages below `dbExit`. This prevents chatter when the handle hovers at the boundary.
- **Two-stage smoothing.** Easing on rate plus an acceleration clamp gives an S-curve start and stop, which produces the cinematic feel.

## Code

```cpp
#include <Arduino.h>
#include <SimpleFOC.h>

// ============================================================
//  Utilities
// ============================================================
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float smoothstep(float e0, float e1, float x) {
  float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// ============================================================
//  Follow axis: deadband window + exponential rate easing
// ============================================================
struct FollowParams {
  float dbEnter   = radians(4.0f);   // start following beyond this error
  float dbExit    = radians(1.0f);   // stop following below this (hysteresis)
  float dbBlend   = radians(3.0f);   // smoothstep blend width past dbEnter
  float followGain = 2.2f;           // [1/s] desired rate per rad of error
  float tauRate   = 0.18f;           // [s] exponential easing time constant
  float maxRate   = radians(120.0f); // [rad/s] max follow speed
  float maxAccel  = radians(400.0f); // [rad/s^2] S-curve accel limit
  float jointMin  = -INFINITY;       // mechanical limits (tilt)
  float jointMax  =  INFINITY;
};

class FollowAxis {
public:
  FollowParams p;
  float camTarget = 0.0f;   // camera world-frame target angle [rad]
  float rate      = 0.0f;   // eased follow rate [rad/s]
  bool  engaged   = false;

  void reset(float handleAngle) {
    camTarget = handleAngle;
    rate = 0.0f;
    engaged = false;
  }

  // handleAngle: handle orientation from IMU (world frame) [rad]
  // returns: joint angle command for the motor [rad]
  float update(float handleAngle, float dt) {
    if (dt <= 0.0f || dt > 0.1f) dt = 0.001f;   // guard against stalls

    float err  = wrapPi(handleAngle - camTarget);
    float aerr = fabsf(err);

    // ---- Deadband window with hysteresis ----
    if (!engaged && aerr > p.dbEnter) engaged = true;
    if ( engaged && aerr < p.dbExit)  engaged = false;

    float effErr = 0.0f;
    if (engaged) {
      // Soft deadband: subtract exit band, then fade in through blend zone
      float mag  = aerr - p.dbExit;
      float fade = smoothstep(p.dbExit, p.dbEnter + p.dbBlend, aerr);
      effErr = copysignf(mag * fade, err);
    }

    // ---- Desired rate (proportional follow, clamped) ----
    float desired = clampf(p.followGain * effErr, -p.maxRate, p.maxRate);

    // ---- Exponential easing on rate (dt-invariant) ----
    float alpha = 1.0f - expf(-dt / p.tauRate);
    float eased = rate + (desired - rate) * alpha;

    // ---- Acceleration clamp -> S-curve start/stop ----
    float dv = clampf(eased - rate, -p.maxAccel * dt, p.maxAccel * dt);
    rate += dv;

    // ---- Integrate camera target ----
    camTarget = wrapPi(camTarget + rate * dt);

    // ---- Stabilization: joint = where camera should be relative to handle
    float joint = wrapPi(camTarget - handleAngle);

    // Respect mechanical limits; if hit, drag the target so it doesn't wind up
    float jc = clampf(joint, p.jointMin, p.jointMax);
    if (jc != joint) {
      camTarget = wrapPi(handleAngle + jc);
      rate = 0.0f;
    }
    return jc;
  }
};

// ============================================================
//  Hardware (adjust pins / pole pairs to your gimbal motors)
// ============================================================
BLDCMotor        panMotor(7);
BLDCDriver3PWM   panDriver(9, 10, 11, 8);
MagneticSensorI2C panSensor(AS5600_I2C);

BLDCMotor        tiltMotor(7);
BLDCDriver3PWM   tiltDriver(3, 5, 6, 4);
MagneticSensorI2C tiltSensor(AS5600_I2C);   // put on 2nd I2C bus / mux

FollowAxis panAxis, tiltAxis;

// Replace with your IMU fusion (Mahony/Madgwick), mounted on the HANDLE.
extern bool  imuRead(float& yaw, float& pitch);   // radians, world frame

// Zero offsets so joint=0 means camera centered on the handle
float panZero = 0.0f, tiltZero = 0.0f;

// Follow loop runs slower than FOC loop
constexpr uint32_t FOLLOW_PERIOD_US = 2000;   // 500 Hz
uint32_t lastFollowUs = 0;
float panCmd = 0.0f, tiltCmd = 0.0f;

void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorI2C& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 12;
  d.init();
  m.linkDriver(&d);

  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.controller     = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;

  // Gimbal motors: low voltage, soft velocity loop
  m.voltage_limit   = 5.0f;
  m.velocity_limit  = 20.0f;
  m.PID_velocity.P  = 0.15f;
  m.PID_velocity.I  = 4.0f;
  m.PID_velocity.D  = 0.0f;
  m.PID_velocity.output_ramp = 1000;
  m.LPF_velocity.Tf = 0.01f;
  m.P_angle.P       = 18.0f;

  m.init();
  m.initFOC();
}

void setup() {
  Serial.begin(115200);
  setupMotor(panMotor,  panDriver,  panSensor);
  setupMotor(tiltMotor, tiltDriver, tiltSensor);

  // Pan: free 360°, slightly lazier feel
  panAxis.p.tauRate    = 0.22f;
  panAxis.p.followGain = 2.0f;

  // Tilt: tighter deadband, mechanical limits
  tiltAxis.p.dbEnter   = radians(3.0f);
  tiltAxis.p.dbExit    = radians(0.8f);
  tiltAxis.p.tauRate   = 0.15f;
  tiltAxis.p.maxRate   = radians(90.0f);
  tiltAxis.p.jointMin  = radians(-90.0f);
  tiltAxis.p.jointMax  = radians( 35.0f);

  float yaw, pitch;
  while (!imuRead(yaw, pitch)) delay(5);
  panAxis.reset(yaw);
  tiltAxis.reset(pitch);
  lastFollowUs = micros();
}

void loop() {
  // FOC must run as fast as possible
  panMotor.loopFOC();
  tiltMotor.loopFOC();

  uint32_t now = micros();
  