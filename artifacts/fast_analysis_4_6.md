# SimpleFOC Smooth Follow Mode with Exponential Easing and Deadband

This is a pan/tilt follow controller for a handheld 3-axis gimbal in the style of the Osmo Pocket. I don't have context for what "Cycle 4" refers to in your project, so I've treated it as the next refinement pass. That pass adds three things:

- **Hysteresis deadband**: there is a separate enter window and exit window, so the camera doesn't stutter at the deadband edge.
- **Exponential rate easing**: the frame-rate-independent `1 - exp(-dt/τ)` form.
- **Acceleration/jerk limiting**: starts and stops ease in and out, with no velocity steps visible in footage.

## Concept

```
camera_world_yaw  = handle_yaw + motor_angle
follow target     : camera_world_target eases toward handle_yaw
motor setpoint    = camera_world_target - handle_yaw   (stabilization comes for free)
```

The IMU subtraction runs every cycle, so hand jitter is cancelled at full loop rate. The slow follow dynamics only shape *intentional* pans.

## Code (Arduino / SimpleFOC 2.x)

```cpp
#include <SimpleFOC.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}
static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline float signf(float v) { return (v > 0) - (v < 0); }

// ---------------------------------------------------------------------------
// Follow axis: deadband + exponential easing + accel limit
// ---------------------------------------------------------------------------
struct FollowParams {
  float deadbandEnter;   // rad  – error needed to START following
  float deadbandExit;    // rad  – error at which following STOPS (< enter)
  float tau;             // s    – exponential easing time constant
  float maxRate;         // rad/s
  float maxAccel;        // rad/s^2
  float softLimit;       // rad  – max motor offset from center (0 = continuous)
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float handleAngle) {
    target = handleAngle;
    rate = 0;
    following = false;
  }

  // handleAngle: handle orientation from IMU (rad, world frame)
  // returns motor setpoint (rad, relative to handle)
  float update(float handleAngle, float dt) {
    if (dt <= 0 || dt > 0.1f) dt = 0.002f;           // guard against stalls

    float err = wrapPi(handleAngle - target);
    float aerr = fabsf(err);

    // --- Hysteresis deadband -------------------------------------------
    if (!following && aerr > P.deadbandEnter) following = true;
    if ( following && aerr < P.deadbandExit && fabsf(rate) < 0.02f)
      following = false;

    // --- Desired rate via exponential easing ---------------------------
    // Soft deadband: subtract exit window so command starts at zero (no step)
    float desiredRate = 0.0f;
    if (following) {
      float eEff = signf(err) * fmaxf(0.0f, aerr - P.deadbandExit);
      // Frame-rate-independent exponential approach:
      // fraction of remaining error to close this step = 1 - e^(-dt/tau)
      float alpha = 1.0f - expf(-dt / P.tau);
      desiredRate = (eEff * alpha) / dt;
      desiredRate = clampf(desiredRate, -P.maxRate, P.maxRate);
    }

    // --- Acceleration limit (smooth ease-in / ease-out) ----------------
    float dvMax = P.maxAccel * dt;
    rate += clampf(desiredRate - rate, -dvMax, dvMax);

    // Don't overshoot the handle
    float step = rate * dt;
    if (fabsf(step) > aerr && signf(step) == signf(err)) {
      step = err;
      rate = 0;
    }
    target = wrapPi(target + step);

    // --- Motor setpoint (stabilization + follow) -----------------------
    float motorSet = wrapPi(target - handleAngle);

    // Mechanical soft limit: if hitting stop, drag target with handle
    if (P.softLimit > 0 && fabsf(motorSet) > P.softLimit) {
      motorSet = clampf(motorSet, -P.softLimit, P.softLimit);
      target = wrapPi(handleAngle + motorSet);
    }
    return motorSet;
  }

  void recenter() { following = true; }  // force follow until centered
  bool isFollowing() const { return following; }

  FollowParams P;
private:
  float target = 0, rate = 0;
  bool following = false;
};

// ---------------------------------------------------------------------------
// Hardware (adjust pins / pole pairs)
// ---------------------------------------------------------------------------
BLDCMotor panMotor(7);
BLDCDriver3PWM panDriver(9, 10, 11, 8);
MagneticSensorSPI panSensor(AS5048_SPI, 10);

BLDCMotor tiltMotor(7);
BLDCDriver3PWM tiltDriver(5, 6, 3, 4);
MagneticSensorSPI tiltSensor(AS5048_SPI, 7);

FollowAxis panFollow ({ /*enter*/ 0.10f, /*exit*/ 0.03f, /*tau*/ 0.45f,
                        /*maxRate*/ 2.0f, /*maxAccel*/ 4.0f, /*limit*/ 0.0f });
FollowAxis tiltFollow({ 0.14f, 0.04f, 0.60f, 1.5f, 3.0f, 1.2f });

// ---------------------------------------------------------------------------
// IMU stub – replace with your AHRS (Mahony/Madgwick on MPU6500/BMI270 etc.)
// Must return HANDLE orientation in world frame, radians.
// ---------------------------------------------------------------------------
struct Attitude { float yaw, pitch, roll; };
Attitude readHandleAttitude();   // implement elsewhere

// ---------------------------------------------------------------------------
void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 12;
  d.init();
  m.linkDriver(&d);

  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.controller     = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;

  // Gimbal motors: high-resistance, low current -> voltage mode
  m.voltage_limit   = 6;
  m.velocity_limit  = 20;          // inner loop must be faster than follow
  m.PID_velocity.P  = 0.4f;
  m.PID_velocity.I  = 8.0f;
  m.PID_velocity.D  = 0.0f;
  m.LPF_velocity.Tf = 0.005f;
  m.P_angle.P       = 25.0f;

  m.init();
  m.initFOC();
}

unsigned long lastFollowUs = 0;
const unsigned long FOLLOW_PERIOD_US = 2000;   // 500 Hz follow/stab loop
float panSet = 0, tiltSet = 0;
float panZero = 0, tiltZero = 0;

void setup() {
  Serial.begin(115200);
  setupMotor(panMotor,  panDriver,  panSensor);
  setupMotor(tiltMotor, tiltDriver, tiltSensor);

  panZero  = panMotor.shaft_angle;
  tiltZero = tiltMotor.shaft_angle;

  Attitude a = readHandleAttitude();
  panFollow.reset(a.yaw);
  tiltFollow.reset(a.pitch);
  lastFollowUs = micros();
}

void loop() {
  // Inner FOC loop: run as fast as possible
  panMotor.loopFOC();
  tiltMotor.loopFOC();

  unsigned long now = micros();
  if (now - lastFollowUs >= FOLLOW_PERIOD_US) {
    float dt = (now - lastFollowUs) * 1e-6f;
    lastFollowUs = now;

    Attitude a = readHandleAttitude();
    panSet  = panFollow.update(a.yaw,   dt);
    tiltSet = tiltFollow.update(a.pitch, dt);
  }

  // Motor angle = mechanical zero + relative setpoint
  panMotor.move(panZero + panSet);
  tiltMotor.move(tiltZero + tiltSet);
}
```

## Tuning Guide

| Parameter | Effect | Pocket-like start |
|---|---|---|
| `deadbandEnter` | How far you can wobble the handle before the camera pans | 4–8° |
| `deadbandExit` | Where the pan settles; must be smaller than enter | ~⅓ of enter |
| `tau` | Easing softness. Larger values give a lazier, more cinematic pan | 0.3–0.8 s |
| `maxRate` | Fast-whip cap | 1.5