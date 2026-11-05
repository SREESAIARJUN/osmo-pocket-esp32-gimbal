# SimpleFOC Smooth Follow Mode: Exponential Easing and Deadband

This is a pan-axis (yaw) follow controller for a handheld gimbal in the style of the Osmo Pocket. When you turn the handle, the camera holds still inside a deadband window. Once you move past the window, the camera eases toward the handle heading along an exponential curve. That gives a cinematic, lag-then-glide pan without the jerk of a hard threshold.

I'm not sure what "Cycle 31" refers to, so it isn't used anywhere in the code. If it's a project or firmware iteration label, you can use it as the version tag in `setup()`.

## Concept

```
camera_yaw  = handle_yaw + motor_angle        (motor sits between handle & camera)
motor_target = follow_setpoint - handle_yaw   (stabilize camera to setpoint)

follow_setpoint eases toward handle_yaw:
  err      = wrap(handle_yaw - setpoint)
  err_eff  = softDeadband(err)                 (continuous, no step at edge)
  alpha    = 1 - exp(-dt / tau)                (frame-rate-independent easing)
  rate     = clamp(err_eff * alpha / dt, ±maxRate), accel-limited
  setpoint += rate * dt
```

Key properties:
- **Frame-rate independent.** `1 - exp(-dt/τ)` gives the same easing at 1 kHz or 5 kHz.
- **Soft deadband.** The effective error is `|e| - db`, so motion starts from zero velocity instead of snapping.
- **Hysteresis.** The controller "engages" past an outer window and "settles" inside an inner one, so it never hunts at the edge.
- **Rate and acceleration caps.** Fast flicks never produce jerky pans.
- **Adaptive τ.** Large errors get a shorter time constant, so the camera catches up on big turns but stays silky on small ones.

## Code (Arduino / SimpleFOC 2.3+)

```cpp
#include <SimpleFOC.h>

// ---------------- Hardware ----------------
BLDCMotor motor = BLDCMotor(7);                       // pole pairs (gimbal motor)
BLDCDriver3PWM driver = BLDCDriver3PWM(9, 5, 6, 8);
MagneticSensorSPI encoder = MagneticSensorSPI(AS5048_SPI, 10);

// ---------------- IMU stub ----------------
// Replace with your fused handle yaw (rad), e.g. Mahony/Madgwick from an
// IMU on the HANDLE, or derive it from camera IMU yaw minus encoder angle.
float readHandleYaw();       // continuous/unwrapped OK, wrap handled below
float readHandleYawRate();   // rad/s (gyro z), optional, for feedforward

// ---------------- Helpers ----------------
static inline float wrapPi(float a) {
  while (a >  _PI) a -= _2PI;
  while (a < -_PI) a += _2PI;
  return a;
}

// ============================================================
//                 Smooth Follow Controller
// ============================================================
struct FollowParams {
  float deadbandOuter = 6.0f  * DEG_TO_RAD; // engage follow beyond this
  float deadbandInner = 1.5f  * DEG_TO_RAD; // settle (stop) inside this
  float tauSlow       = 0.60f;              // s, easing for small errors
  float tauFast       = 0.18f;              // s, easing for large errors
  float tauBlendErr   = 45.0f * DEG_TO_RAD; // error at which tau -> tauFast
  float maxRate       = 180.0f * DEG_TO_RAD;// rad/s pan speed cap
  float maxAccel      = 600.0f * DEG_TO_RAD;// rad/s^2 accel cap (anti-jerk)
  float settleRate    = 2.0f  * DEG_TO_RAD; // rad/s: below this + inner = idle
};

class SmoothFollow {
public:
  FollowParams p;
  float setpoint = 0.0f;   // desired camera yaw (rad, world frame)
  float rate     = 0.0f;   // current setpoint velocity (rad/s)
  bool  engaged  = false;

  void reset(float handleYaw) { setpoint = handleYaw; rate = 0; engaged = false; }

  // Call at a fixed or variable rate; dt in seconds
  float update(float handleYaw, float dt) {
    if (dt <= 0.0f || dt > 0.05f) dt = 0.001f;           // guard glitches

    float err = wrapPi(handleYaw - setpoint);
    float aerr = fabsf(err);

    // ---- Hysteretic deadband window ----
    if (!engaged && aerr > p.deadbandOuter) engaged = true;
    if (engaged && aerr < p.deadbandInner && fabsf(rate) < p.settleRate)
      engaged = false;

    float targetRate = 0.0f;
    if (engaged) {
      // ---- Soft deadband: continuous, starts from zero ----
      // While engaged, use the inner band so we track down close to center.
      float db = p.deadbandInner;
      float eEff = (aerr > db) ? copysignf(aerr - db, err) : 0.0f;

      // ---- Adaptive time constant (smoothstep blend) ----
      float x = constrain(aerr / p.tauBlendErr, 0.0f, 1.0f);
      x = x * x * (3.0f - 2.0f * x);
      float tau = p.tauSlow + (p.tauFast - p.tauSlow) * x;

      // ---- Exponential easing (frame-rate independent) ----
      float alpha = 1.0f - expf(-dt / tau);
      targetRate = (eEff * alpha) / dt;                  // rad/s
      targetRate = constrain(targetRate, -p.maxRate, p.maxRate);
    }

    // ---- Acceleration limit -> no jerk at start/stop ----
    float dvMax = p.maxAccel * dt;
    rate += constrain(targetRate - rate, -dvMax, dvMax);

    // Never overshoot the handle within a step
    float step = rate * dt;
    if (fabsf(step) > aerr && copysignf(1.0f, step) == copysignf(1.0f, err)) {
      step = err; rate = err / dt;
    }

    setpoint = wrapPi(setpoint + step);
    return setpoint;
  }
};

SmoothFollow follow;

// ============================================================
//                         Setup
// ============================================================
void setup() {
  Serial.begin(115200);

  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 12;
  driver.init();
  motor.linkDriver(&driver);

  motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
  motor.controller     = MotionControlType::angle;
  motor.torque_controller = TorqueControlType::voltage;

  // Gimbal motors: high-resistance, voltage mode, gentle tuning
  motor.voltage_limit   = 6.0f;
  motor.velocity_limit  = 20.0f;
  motor.PID_velocity.P  = 0.25f;
  motor.PID_velocity.I  = 4.0f;
  motor.PID_velocity.D  = 0.0f;
  motor.PID_velocity.output_ramp = 1000;
  motor.LPF_velocity.Tf = 0.01f;
  motor.P_angle.P       = 18.0f;
  motor.P_angle.output_ramp = 0;           // easing handled upstream

  motor.init();
  motor.initFOC();

  follow.reset(readHandleYaw());
  Serial.println(F("Smooth follow ready"));
}

// ============================================================
//                          Loop
// ============================================================
void loop() {
  motor.loopFOC();                          // run as fast as possible

  static uint32_t last = micros();
  uint32_t now = micros();
  float dt = (now - last) * 1e-6f;

  // Run the follow + outer loop at ~1 kHz
  if (dt >= 0.001f) {
    last = now;

    float handleYaw = readHandleYaw();
    float sp = follow.update(handleYaw, dt);

    // Motor angle needed to keep camera at setpoint
    float motorTarget = wrapPi(sp - handleYaw);

    // Keep the target continuous relative to the shaft (avoid 2π jumps)
    float shaft = motor.shaft_angle;
    motorTarget = shaft + wrapPi(motorTarget - wrapPi(shaft));

    // Optional: soft mechanical limit if the pan axis has a hard stop
    // motorTarget = constrain(motorTarget, -170*DEG_TO_RAD, 170*DEG_TO_RAD);

    motor.move(motorTarget);
  }
}
```

## Tuning Guide

| Parameter | Effect | Osmo-like start |
|---|---|---|
| `deadbandOuter` | How far you can turn before the camera reacts | 4–8° |
| `deadbandInner` | How close it settles to center (keep it below outer) | 1–2° |
| `tauSlow` | Glide for small reframes; higher is more cinematic | 0.5–0.9 s |
| `tauFast` | Catch-up speed on big turns | 0.15–0.25 s |
| `maxRate` | Pan speed ceiling | 120–200 °/s |
| `maxAccel` | Smoothness of starts and stops | 400–800 °/s² |

**Presets:**
- **Cinematic/slow:** τ 0.9 / 0.3, maxRate 90°/s, accel 300°/s²
- **Normal:** the defaults above
- **Sport/fast:** τ 0.25 / 0.08, deadband 2°/0.5°, maxRate 300°/s

## Practical Notes

1. **Yaw source.** Handle yaw drifts without a magnetometer. Follow mode tolerates this well, because drift just slowly re-centers the setpoint. Lock mode does not tolerate it.
2. **Stab