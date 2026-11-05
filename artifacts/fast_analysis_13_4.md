# SimpleFOC Smooth Follow Mode: Exponential Rate Easing and Deadband Window

This is a pan/tilt follow-mode controller for a handheld gimbal like the Osmo Pocket, built on SimpleFOC. I wasn't sure what "Cycle 13" refers to, so I treated this as a standalone, complete revision.

## How It Works

**Frames:**
- `camWorld` is the camera angle in the world frame, from the IMU.
- `motorRel` is the camera angle relative to the handle, from `motor.shaft_angle`.
- `handleWorld` is derived as `camWorld − motorRel`.

**Follow logic:**
- The camera should hold a world-frame target (`targetWorld`) that slowly chases the handle.
- The follow error is `e = handleWorld − targetWorld`.

**Processing pipeline:**

```
e ──► wrap(±π) ──► deadband window (hysteresis) ──► soft-deadband shaping
  ──► exponential rate curve  r = Rmax·(1−e^(−|e'|/τ))·sign(e)
  ──► exponential rate easing (1st-order, separate rise/fall constants)
  ──► accel (jerk) clamp ──► integrate targetWorld
  ──► motor target = targetWorld − handleWorld  (SimpleFOC angle mode)
```

**Why each stage matters:**
- **Deadband window.** Small hand jitter is ignored completely. Hysteresis (separate enter and exit thresholds) prevents start/stop chatter at the edge.
- **Soft deadband.** The deadband width is subtracted from the error, so the rate starts from zero instead of jumping when following begins.
- **Exponential curve.** The rate is gentle near zero, saturates smoothly at large errors, and has no hard knee.
- **Exponential easing.** This gives the cinematic "slow in / slow out" feel. A longer fall constant makes the camera glide to a stop.

## Code

```cpp
#include <SimpleFOC.h>
#include <math.h>

// ---------------- Utilities ----------------
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float signf(float x) { return (x > 0) - (x < 0); }

// ---------------- Follow axis ----------------
struct FollowParams {
  float deadbandEnter = 0.087f;  // rad (~5°)  start following beyond this
  float deadbandExit  = 0.026f;  // rad (~1.5°) stop following below this
  float maxRate       = 3.0f;    // rad/s  peak follow speed
  float curveTau      = 0.35f;   // rad    exponential curve knee
  float easeRiseT     = 0.25f;   // s      rate ramp-up time constant
  float easeFallT     = 0.60f;   // s      rate ramp-down (glide) time constant
  float maxAccel      = 8.0f;    // rad/s^2 hard accel clamp (anti-jerk)
  float limitMin      = -INFINITY; // mechanical limits on motorRel (tilt)
  float limitMax      =  INFINITY;
  bool  wrap          = true;    // pan: continuous; tilt: false
};

class FollowAxis {
public:
  FollowParams p;
  float targetWorld = 0.0f;   // camera world setpoint
  float rate        = 0.0f;   // eased follow rate (rad/s)
  bool  following   = false;

  void reset(float camWorld) { targetWorld = camWorld; rate = 0; following = false; }

  // Returns the follow error after windowing (debug/telemetry)
  float update(float dt, float handleWorld) {
    if (dt <= 0.0f || dt > 0.05f) return 0.0f;  // drop bad samples

    float e = handleWorld - targetWorld;
    if (p.wrap) e = wrapPi(e);
    float ae = fabsf(e);

    // 1) Deadband window with hysteresis
    if (!following && ae > p.deadbandEnter) following = true;
    else if (following && ae < p.deadbandExit) following = false;

    // 2) Soft deadband: subtract exit width so rate is continuous from 0
    float eff = following ? fmaxf(ae - p.deadbandExit, 0.0f) : 0.0f;

    // 3) Exponential rate curve
    float rCmd = signf(e) * p.maxRate * (1.0f - expf(-eff / p.curveTau));

    // 4) Exponential easing, asymmetric rise/fall
    bool speedingUp = fabsf(rCmd) > fabsf(rate) && signf(rCmd) == signf(rate)
                      || rate == 0.0f;
    float T = speedingUp ? p.easeRiseT : p.easeFallT;
    float alpha = 1.0f - expf(-dt / T);
    float rEased = rate + alpha * (rCmd - rate);

    // 5) Acceleration clamp
    float dMax = p.maxAccel * dt;
    rate += clampf(rEased - rate, -dMax, dMax);

    // 6) Never overshoot the handle within one step
    float step = rate * dt;
    if (fabsf(step) > ae) { step = e; rate = 0.0f; }

    targetWorld += step;
    if (p.wrap) targetWorld = wrapPi(targetWorld);
    return e;
  }

  // Motor (relative) setpoint to realize targetWorld
  float motorTarget(float handleWorld) {
    float rel = targetWorld - handleWorld;
    if (p.wrap) rel = wrapPi(rel);
    float c = clampf(rel, p.limitMin, p.limitMax);
    if (c != rel) {                       // at hard stop: drag target along
      targetWorld = handleWorld + c;
      rate = 0.0f;
    }
    return c;
  }
};

// ---------------- Hardware ----------------
BLDCMotor   panMotor(7), tiltMotor(7);
BLDCDriver3PWM panDrv(9, 10, 11, 8), tiltDrv(3, 5, 6, 7);
MagneticSensorSPI panEnc(AS5048_SPI, 10), tiltEnc(AS5048_SPI, 4);

FollowAxis pan, tilt;

// Provide from your IMU/AHRS (Mahony/Madgwick), camera frame, radians
extern float imuYaw();
extern float imuPitch();

void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, Sensor& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 8.0f;
  d.init();
  m.linkDriver(&d);
  m.controller = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;
  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.PID_velocity.P = 0.15f; m.PID_velocity.I = 2.0f; m.PID_velocity.D = 0;
  m.PID_velocity.output_ramp = 1000;
  m.LPF_velocity.Tf = 0.005f;
  m.P_angle.P = 20.0f;
  m.velocity_limit = 20.0f;
  m.voltage_limit = 4.0f;
  m.init();
  m.initFOC();
}

void setup() {
  Serial.begin(115200);
  setupMotor(panMotor, panDrv, panEnc);
  setupMotor(tiltMotor, tiltDrv, tiltEnc);

  pan.p.wrap = true;                       // continuous yaw
  tilt.p.wrap = false;
  tilt.p.limitMin = -1.4f; tilt.p.limitMax = 1.4f;
  tilt.p.maxRate = 2.0f;                   // tilt calmer than pan
  tilt.p.deadbandEnter = 0.10f;

  pan.reset(imuYaw());
  tilt.reset(imuPitch());
}

uint32_t lastUs = 0;

void loop() {
  panMotor.loopFOC();
  tiltMotor.loopFOC();

  uint32_t now = micros();
  if (now - lastUs >= 1000) {              // 1 kHz follow loop
    float dt = (now - lastUs) * 1e-6f;
    lastUs = now;

    float panHandle  = wrapPi(imuYaw()   - panMotor.shaft_angle);
    float tiltHandle =        imuPitch() - tiltMotor.shaft_angle;

    pan.update(dt, panHandle);
    tilt.update(dt, tiltHandle);

    panMotor.move(pan.motorTarget(panHandle));
    tiltMotor.move(tilt.motorTarget(tiltHandle));
  }
}

// Recenter (e.g. double-press): snap target to handle with eased glide
void recenter() {
  pan.p.deadbandExit = 0.0f;               // temporarily disable window
  tilt.p.deadbandExit = 0.0f;
  pan.following = tilt.following = true;
  