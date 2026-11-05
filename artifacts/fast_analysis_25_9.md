# SimpleFOC Smooth Follow Mode: Exponential Easing and Soft Deadband (Pan Axis, Cycle 25)

This is a pan-axis follow controller in the style of the Osmo Pocket. The camera holds its world heading, ignores small handle wobble inside a deadband window, and eases toward the handle's heading during larger, deliberate pans. All smoothing is frame-rate independent, so tuning stays the same if the loop rate changes.

## Control Concept

```
handle_yaw (world) ──► error = wrap(handle_yaw − cam_target)
                           │
                   soft deadband (tanh window)
                           │
               desired_rate = e_eff / τ_follow    (exponential approach)
                           │
               rate ease: 1st-order low-pass on rate (τ_rate)
                           │   + accel / max-rate clamps
                           ▼
               cam_target += rate·dt              (world heading)
                           │
     motor_setpoint = cam_target − handle_yaw     (keeps camera stabilized)
                           ▼
                SimpleFOC angle loop (motor.move)
```

The behavior falls out of two pieces:

- **Exponential easing.** The target approaches the handle heading as `e·(1−e^(−dt/τ))`. Large pans start smoothly, and the camera settles without overshoot.
- **Soft deadband.** The deadband uses `e − w·tanh(e/w)`. The response is near zero (cubic) inside the window and becomes linear, offset by `w`, outside it. This avoids the "snap" that a hard deadband edge produces.

## Code (Arduino / SimpleFOC 2.3+)

```cpp
#include <SimpleFOC.h>
#include <math.h>

// ───────── Hardware (adjust to your board) ─────────
BLDCMotor        motor  = BLDCMotor(7);                 // pole pairs (e.g. 2204 gimbal)
BLDCDriver3PWM   driver = BLDCDriver3PWM(9, 5, 6, 8);
MagneticSensorSPI encoder = MagneticSensorSPI(AS5048_SPI, 10);

// Provide these from your IMU fusion (e.g. BMI270 + Mahony/Madgwick on camera head).
// Camera world yaw [rad] and camera world yaw rate [rad/s].
extern float imuCameraYaw();
extern float imuCameraYawRate();

// ───────── Follow-mode tuning ─────────
struct FollowParams {
  float deadband      = radians(4.0f);   // soft window half-width
  float tauFollow     = 0.45f;           // s, how lazily target chases handle
  float tauRate       = 0.18f;           // s, easing of the rate itself (jerk smoothing)
  float maxRate       = radians(120.0f); // rad/s pan cap
  float maxAccel      = radians(400.0f); // rad/s^2 cap
  float recenterBoost = 2.5f;            // speed-up factor when far off (> 3*deadband)
  float motorLimit    = radians(170.0f); // mechanical/cable safety
} P;

// ───────── State ─────────
struct FollowState {
  float camTarget = 0;   // world heading the camera should hold [rad]
  float rate      = 0;   // current eased pan rate [rad/s]
  float handleYawF = 0;  // filtered handle yaw
  bool  init = false;
} S;

static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}

// Smooth deadband: ~0 slope inside window, slope→1 outside, C∞ continuous.
static inline float softDeadband(float e, float w) {
  return e - w * tanhf(e / w);
}

// Frame-rate independent exponential blend factor
static inline float expAlpha(float dt, float tau) {
  return 1.0f - expf(-dt / tau);
}

// ───────── Follow update (call at fixed-ish rate, ~500 Hz) ─────────
float followUpdate(float dt) {
  // Handle heading = camera world yaw − motor relative angle
  float motorAngle = motor.shaft_angle;          // rad, relative camera↔handle
  float handleYaw  = wrapPi(imuCameraYaw() - motorAngle);

  if (!S.init) {
    S.camTarget = imuCameraYaw();
    S.handleYawF = handleYaw;
    S.rate = 0;
    S.init = true;
  }

  // Light pre-filter on handle yaw to kill hand tremor (~8 Hz)
  S.handleYawF = wrapPi(S.handleYawF +
                 wrapPi(handleYaw - S.handleYawF) * expAlpha(dt, 0.02f));

  // 1) Error between handle and where camera is aimed
  float err  = wrapPi(S.handleYawF - S.camTarget);

  // 2) Soft deadband window
  float eEff = softDeadband(err, P.deadband);

  // 3) Exponential approach → desired rate
  float tau = P.tauFollow;
  if (fabsf(err) > 3.0f * P.deadband) tau /= P.recenterBoost;  // catch up on big turns
  float desiredRate = eEff / tau;
  desiredRate = _constrain(desiredRate, -P.maxRate, P.maxRate);

  // 4) Ease the rate itself (exponential), then clamp acceleration
  float rateStep = (desiredRate - S.rate) * expAlpha(dt, P.tauRate);
  float maxStep  = P.maxAccel * dt;
  S.rate += _constrain(rateStep, -maxStep, maxStep);

  // 5) Integrate world target
  S.camTarget = wrapPi(S.camTarget + S.rate * dt);

  // 6) Convert world target to motor setpoint (stabilization)
  float setpoint = wrapPi(S.camTarget - handleYaw);
  setpoint = _constrain(setpoint, -P.motorLimit, P.motorLimit);

  // If we hit the mechanical limit, drag the world target along (no windup)
  if (fabsf(setpoint) >= P.motorLimit) {
    S.camTarget = wrapPi(handleYaw + setpoint);
    S.rate = 0;
  }
  return setpoint;
}

// ───────── Setup ─────────
void setup() {
  Serial.begin(115200);
  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 8.4f;   // 2S
  driver.init();
  motor.linkDriver(&driver);

  motor.foc_modulation   = FOCModulationType::SpaceVectorPWM;
  motor.controller       = MotionControlType::angle;
  motor.torque_controller = TorqueControlType::voltage;
  motor.voltage_limit    = 4.0f;
  motor.velocity_limit   = 20.0f;

  // Stiff, low-noise gimbal loops
  motor.P_angle.P        = 18.0f;
  motor.PID_velocity.P   = 0.12f;
  motor.PID_velocity.I   = 4.0f;
  motor.PID_velocity.D   = 0.0f;
  motor.PID_velocity.output_ramp = 1000.0f;
  motor.LPF_velocity.Tf  = 0.008f;

  motor.init();
  motor.initFOC();
}

// ───────── Loop ─────────
void loop() {
  static uint32_t tPrev = micros();
  static float setpoint = 0;

  motor.loopFOC();                         // as fast as possible

  uint32_t now = micros();
  float dt = (now - tPrev) * 1e-6f;
  if (dt >= 0.002f) {                      // 500 Hz follow update
    tPrev = now;
    dt = _constrain(dt, 1e-4f, 0.02f);
    setpoint = followUpdate(dt);
  }
  motor.move(setpoint);
}
```

## Tuning Guide

| Parameter | Effect | Video starting point |
|---|---|---|
| `deadband` | How much handle wobble is ignored | 3–6° (walking: 6°, tripod-ish: 2°) |
| `tauFollow` | Laziness of the follow; higher = more cinematic | 0.35–0.8 s |
| `tauRate` | Smoothness of pan start/stop (jerk) | 0.12–0.25 s |
| `maxRate` | Fastest pan speed | 90–150°/s |
| `maxAccel` | Snappiness at the start of a whip pan | 300–600°/s² |
| `recenterBoost` | Catch-up speed on large turns | 2–3× |

## Notes

- **Sign convention.** If the camera runs away instead of following, flip the sign of `motorAngle` in the `handleYaw` computation, or set `motor.sensor_direction`.
- **Yaw drift.** Without a magnetometer, gyro-only yaw drifts. Follow mode masks this well, because the camera target only needs to be correct relative to the handle.
- **Tilt axis.** Reuse `followUpdate` with a separate `FollowState` per axis. Use a larger deadband and a slower `tauFollow`, since tilt follow is usually more conservative.
- **Lock / FPV modes.** These map directly onto the same structure:
  - L