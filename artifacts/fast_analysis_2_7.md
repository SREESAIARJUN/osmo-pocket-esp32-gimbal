# SimpleFOC Smooth Follow Mode (Cycle 2)

This is a pan-axis follow controller for a handheld gimbal. When you turn the handle, the camera heading drifts after it smoothly, with no jerk at the start or end of the motion. Small hand tremors are ignored by a deadband.

## Changes from Cycle 1

| Area | Cycle 1 | Cycle 2 |
|---|---|---|
| Easing | Fixed `alpha` low-pass, which depends on loop rate | `1 - exp(-dt/τ)`, independent of loop rate |
| Deadband | Hard cutoff, causing a "notch" at the edge | Hysteretic deadband with smoothstep edge blending |
| Rate | Position low-pass only | Two-stage: exponential target rate, then eased rate (critically damped) |
| Wrap | Broke at ±180° | All headings wrapped, shortest-path error |
| Limits | None | Max rate, max accel, joint soft-stops |
| Disturbance | Lag on fast pans | Gyro feedforward on the joint setpoint |

## Control Concept

```
handleYaw (IMU) ──► error = wrap(handleYaw - camTarget)
                       │
                 [hysteretic soft deadband]
                       │  e_eff
                 rate_cmd = e_eff / τ_follow     (exponential approach)
                       │  clamp ±maxRate
                 rate = ease(rate, rate_cmd, τ_rate) + accel limit
                       │
                 camTarget += rate·dt
                       │
 jointSetpoint = wrap(camTarget - handleYaw) ──► motor.move()  (angle mode)
```

The camera's world heading (`camTarget`) is the slow state being controlled. The motor joint angle is derived from it each loop, so the camera stays stabilized against fast hand shake while following the slow intent.

## Code

```cpp
#include <SimpleFOC.h>
#include <math.h>

// ================= Hardware (adapt to your board) =================
BLDCMotor motor = BLDCMotor(7);                       // pole pairs
BLDCDriver3PWM driver = BLDCDriver3PWM(PA8, PA9, PA10, PB12);
MagneticSensorSPI encoder = MagneticSensorSPI(AS5048_SPI, PA4);

// Supply from your IMU fusion (handle-mounted or derived)
extern float imuHandleYaw();      // rad, world frame
extern float imuHandleYawRate();  // rad/s, gyro z (world-projected)

// ================= Tuning =================
struct FollowParams {
  float deadbandOuter = 0.087f;  // 5.0°  error needed to START following
  float deadbandInner = 0.017f;  // 1.0°  error at which following STOPS
  float edgeSoftness  = 0.035f;  // 2.0°  smoothstep blend width past deadband
  float tauFollow     = 0.45f;   // s     exponential approach time constant
  float tauRate       = 0.12f;   // s     rate easing (start/stop smoothness)
  float maxRate       = 3.0f;    // rad/s ~170°/s
  float maxAccel      = 12.0f;   // rad/s^2
  float jointLimit    = 2.6f;    // rad   soft mechanical limit (±150°)
  float ffGain        = 1.0f;    // gyro feedforward on joint setpoint
};
FollowParams P;

// ================= Helpers =================
static inline float wrapPi(float a) {
  a = fmodf(a + PI, _2PI);
  if (a < 0) a += _2PI;
  return a - PI;
}

static inline float smoothstep01(float x) {
  x = constrain(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

static inline float expAlpha(float dt, float tau) {
  return (tau <= 0.0f) ? 1.0f : 1.0f - expf(-dt / tau);
}

// ================= Follow controller =================
class SmoothFollow {
public:
  void reset(float handleYaw, float jointAngle) {
    camTarget = wrapPi(handleYaw + jointAngle);  // hold current camera heading
    rate = 0.0f;
    following = false;
  }

  // Returns joint (motor) angle setpoint in rad
  float update(float handleYaw, float handleRate, float dt) {
    if (dt <= 0.0f || dt > 0.05f) dt = 0.001f;   // guard against stalls

    // 1. Shortest-path error between handle intent and camera heading
    float err = wrapPi(handleYaw - camTarget);
    float aerr = fabsf(err);

    // 2. Hysteretic deadband: start at outer threshold, stop at inner
    if (!following && aerr > P.deadbandOuter) following = true;
    if ( following && aerr < P.deadbandInner && fabsf(rate) < 0.02f) following = false;

    float db = following ? P.deadbandInner : P.deadbandOuter;

    // 3. Soft-edge deadband: continuous, C1-smooth past the threshold
    float eEff = 0.0f;
    if (aerr > db) {
      float over  = aerr - db;
      float blend = smoothstep01(over / P.edgeSoftness);
      eEff = copysignf(over * blend, err);
    }

    // 4. Exponential approach: rate proportional to remaining error
    float rateCmd = following ? eEff / P.tauFollow : 0.0f;
    rateCmd = constrain(rateCmd, -P.maxRate, P.maxRate);

    // 5. Ease the rate itself (dt-independent), then limit acceleration
    float eased = rate + (rateCmd - rate) * expAlpha(dt, P.tauRate);
    float dvMax = P.maxAccel * dt;
    rate += constrain(eased - rate, -dvMax, dvMax);

    // 6. Integrate camera heading
    camTarget = wrapPi(camTarget + rate * dt);

    // 7. Joint setpoint, with gyro feedforward to cancel fast hand motion
    float joint = wrapPi(camTarget - handleYaw);
    joint -= P.ffGain * handleRate * dt;   // phase-lead for one sample of motion

    // 8. Soft-stop: if near the mechanical limit, drag the target along
    if (fabsf(joint) > P.jointLimit) {
      float excess = joint - copysignf(P.jointLimit, joint);
      camTarget = wrapPi(camTarget - excess);
      joint = copysignf(P.jointLimit, joint);
      rate = (handleRate);                 // match hand to avoid a snap on release
    }
    return joint;
  }

  float heading()   const { return camTarget; }
  float rateNow()   const { return rate; }
  bool  isActive()  const { return following; }

private:
  float camTarget = 0.0f;
  float rate = 0.0f;
  bool  following = false;
};

SmoothFollow follow;
unsigned long lastUs = 0;
Commander command = Commander(Serial);

// Runtime tuning via serial: e.g. "T0.6" sets tauFollow
void cmdTau(char* c)  { P.tauFollow     = atof(c); }
void cmdDb(char* c)   { P.deadbandOuter = atof(c) * DEG_TO_RAD; }
void cmdRate(char* c) { P.maxRate       = atof(c); }

void setup() {
  Serial.begin(115200);
  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 12;
  driver.init();
  motor.linkDriver(&driver);

  motor.controller = MotionControlType::angle;
  motor.foc_modulation = FOCModulationType::SpaceVectorPWM;

  // Gimbal-style gentle position loop
  motor.P_angle.P = 18.0f;
  motor.PID_velocity.P = 0.15f;
  motor.PID_velocity.I = 2.0f;
  motor.PID_velocity.D = 0.0f;
  motor.LPF_velocity.Tf = 0.01f;
  motor.velocity_limit = 20.0f;
  motor.voltage_limit = 4.0f;

  motor.init();
  motor.initFOC();

  follow.reset(imuHandleYaw(), motor.shaftAngle());
  lastUs = micros();

  command.add('T', cmdTau,  "tau follow [s]");
  command.add('D', cmdDb,   "deadband outer [deg]");
  command.add('R', cmdRate, "max rate [rad/s]");
}

void loop() {
  motor.loopFOC();

  unsigned long now = micros();
  float dt = (now - lastUs) * 1e-6f;
  lastUs = now;

  float jointSp = follow.update(imuHandleYaw(), imuHandleYawRate(), dt);
  motor.move(jointSp);

  command.run();
}
```

## Tuning Guide (Osmo Pocket feel)

| Parameter | Effect | Typical range |
|---|---|---|
| `tauFollow` | Overall laziness of follow | 0.3 (sporty) to 0.8 (cinematic) |
| `tauRate` | Softness of start/stop ramps | 0.08 to 0.2 |
| `deadbandOuter` | Ignores tremor and small adjustments | 3 to 8° |
| `deadbandInner` | Settling precision (keep below outer) | 0.5 to 1.5° |
| `edgeSoftness`