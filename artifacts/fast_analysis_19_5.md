# SimpleFOC Smooth Follow Mode with Exponential Easing and Deadband

This is a follow-mode controller for an Osmo Pocket-style handheld gimbal. The camera holds its heading while small handle jitter falls inside a deadband. When you pan deliberately, the camera eases toward the handle direction with a critically-damped exponential response. It never snaps, overshoots or stutters at the deadband edge.

(I don't have context for "Cycle 19". I've treated it as your iteration label. Tell me if it refers to a specific spec or set of requirements.)

## Algorithm overview

```
 handle yaw ──► wrap(handle − setpoint) ──► soft deadband (with hysteresis)
                                                 │
                                                 ▼
                                    desired rate = e_eff / τ   (exponential approach)
                                                 │
                                    clamp |rate| ≤ ω_max
                                                 │
                                    slew limit (accel ≤ α_max)  ← eases START of motion
                                                 │
                                    1st-order LPF on rate        ← eases END of motion
                                                 │
                                    setpoint += rate · dt
                                                 │
                    motor target = wrap(setpoint − handle_yaw) ──► SimpleFOC angle loop
```

Key properties:
- **Frame-rate independent.** Every filter uses `1 − exp(−dt/τ)`, so loop jitter doesn't change the feel.
- **Continuous soft deadband.** The deadband subtracts its width instead of zeroing the error, so there is no step when you cross the edge.
- **Hysteresis.** The deadband shrinks while following. This prevents the "start/stop/start" stutter typical of naive deadbands.
- **Wrap-safe.** All angle math goes through a ±π wrap, so the 359°→0° crossing is handled.

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

// dt-independent exponential smoothing coefficient
static inline float expAlpha(float dt, float tau) {
  return (tau <= 0.0f) ? 1.0f : 1.0f - expf(-dt / tau);
}

static inline float signf(float x) { return (x > 0) - (x < 0); }

// ============================================================
//  Follow axis controller
// ============================================================
struct FollowParams {
  float deadband;        // rad – hold window while idle
  float deadbandActive;  // rad – shrunken window while following (hysteresis)
  float knee;            // rad – soft-knee width at deadband edge
  float tau;             // s   – exponential approach time constant
  float maxRate;         // rad/s
  float maxAccel;        // rad/s^2 – start-of-motion easing
  float rateTau;         // s   – end-of-motion easing (rate LPF)
  float stopRate;        // rad/s – below this + inside window => back to HOLD
  bool  enabled;         // false = lock axis (e.g. roll)
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float handleAngle) {
    setpoint = handleAngle;
    rateCmd = rateFilt = 0.0f;
    following = false;
  }

  // Returns the camera setpoint in the world frame
  float update(float handleAngle, float dt) {
    if (!P.enabled || dt <= 0.0f) return setpoint;

    float err = wrapPi(handleAngle - setpoint);
    float aerr = fabsf(err);

    // ---- Hysteretic deadband state ----
    float db = following ? P.deadbandActive : P.deadband;
    if (!following && aerr > P.deadband) following = true;
    if (following && aerr < P.deadbandActive && fabsf(rateFilt) < P.stopRate)
      following = false;

    // ---- Soft deadband with quadratic knee (C1 continuous) ----
    float e_eff = 0.0f;
    float over = aerr - db;
    if (over > 0.0f) {
      if (over < P.knee)  e_eff = (over * over) / (2.0f * P.knee);
      else                e_eff = over - 0.5f * P.knee;
      e_eff *= signf(err);
    }

    // ---- Exponential approach: rate proportional to remaining error ----
    float rateDesired = following ? (e_eff / P.tau) : 0.0f;
    rateDesired = constrain(rateDesired, -P.maxRate, P.maxRate);

    // ---- Acceleration slew limit (eases motion start) ----
    float maxStep = P.maxAccel * dt;
    rateCmd += constrain(rateDesired - rateCmd, -maxStep, maxStep);

    // ---- Rate low-pass (eases motion end, removes jerk) ----
    rateFilt += expAlpha(dt, P.rateTau) * (rateCmd - rateFilt);

    setpoint = wrapPi(setpoint + rateFilt * dt);
    return setpoint;
  }

  // Recenter on demand (e.g. triple-click), using the same easing
  void recenter(float handleAngle) { following = true; P.deadbandActive = 0.0f; }

  float rate() const { return rateFilt; }
  bool  isFollowing() const { return following; }

  FollowParams P;
private:
  float setpoint = 0, rateCmd = 0, rateFilt = 0;
  bool following = false;
};

// ============================================================
//  Hardware: pan motor (repeat for tilt/roll as needed)
// ============================================================
BLDCMotor        panMotor(7);                 // pole pairs (gimbal motors: 7–11 typical)
BLDCDriver3PWM   panDriver(9, 10, 11, 8);
MagneticSensorSPI panEnc(AS5048_SPI, 10);

// Pocket-style feel: smooth cinematic pan, firm tilt, locked roll
FollowAxis panFollow({
  .deadband       = radians(4.0f),
  .deadbandActive = radians(1.0f),
  .knee           = radians(2.0f),
  .tau            = 0.45f,
  .maxRate        = radians(120.0f),
  .maxAccel       = radians(400.0f),
  .rateTau        = 0.08f,
  .stopRate       = radians(1.5f),
  .enabled        = true
});

FollowAxis tiltFollow({
  radians(6.0f), radians(1.5f), radians(2.0f),
  0.6f, radians(90.0f), radians(300.0f), 0.10f, radians(1.5f), true
});

// ---- IMU stub: replace with your fusion (Mahony/Madgwick on BMI270/ICM-42688) ----
struct Attitude { float yaw, pitch, roll; };
Attitude readHandleAttitude();   // handle frame in world (derive: camera IMU − joint angles)

// ============================================================
void setup() {
  Serial.begin(115200);

  panEnc.init();
  panMotor.linkSensor(&panEnc);

  panDriver.voltage_power_supply = 8.4f;     // 2S
  panDriver.init();
  panMotor.linkDriver(&panDriver);

  panMotor.foc_modulation   = FOCModulationType::SpaceVectorPWM;
  panMotor.controller       = MotionControlType::angle;
  panMotor.torque_controller = TorqueControlType::voltage;

  // Gimbal motors: high-resistance, low-voltage loops
  panMotor.voltage_limit   = 4.0f;
  panMotor.PID_velocity.P  = 0.12f;
  panMotor.PID_velocity.I  = 4.0f;
  panMotor.PID_velocity.D  = 0.0f;
  panMotor.PID_velocity.output_ramp = 1000;
  panMotor.LPF_velocity.Tf = 0.01f;
  panMotor.P_angle.P       = 18.0f;
  panMotor.velocity_limit  = 12.0f;

  panMotor.init();
  panMotor.initFOC();

  Attitude h = readHandleAttitude();
  panFollow.reset(h.yaw);
  tiltFollow.reset(h.pitch);
}

// ============================================================
void loop() {
  panMotor.loopFOC();                        // run as fast as possible

  // Outer follow loop at ~500 Hz
  static uint32_t last = micros();
  uint32_t now = micros();
  float dt = (now - last) * 1e-6f;
  if (dt < 0.002f) return;
  last = now;
  dt = min(dt, 0.02f);                       // guard against stalls

  Attitude h = readHandleAttitude();

  float camYawSet = panFollow.update(h.yaw, dt);

  // Joint angle = camera heading relative to handle
  float jointTarget = wrapPi(camYawSet - h.yaw);

  // Unwrap relative to the current shaft angle so SimpleFOC never spins the long way
  float shaft = panMotor.shaft_angle;
  float target = shaft + wrapPi(jointTarget - wrapPi(shaft));

  panMotor.move(target);
}
```

## Tuning guide

| Parameter | Effect | Pocket-like starting point |
|---|---|---|
