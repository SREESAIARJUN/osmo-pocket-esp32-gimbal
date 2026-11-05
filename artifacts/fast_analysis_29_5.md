# SimpleFOC Smooth Follow Mode: Exponential Easing with a Soft Deadband

This is an Arduino/SimpleFOC sketch for an Osmo Pocket–style follow mode. The camera stays world-stabilized and slowly follows the handle's heading. Small wrist jitter inside a deadband window is ignored, and larger, intentional pans are followed with exponentially eased, rate-limited motion. I don't have context for "Cycle 29", so this is a self-contained implementation you can version however you like.

## How it works

```
handle yaw (IMU) ──► follow error = handleYaw − cameraHeading (wrapped to ±π)
                         │
                   soft deadband (quadratic knee, no step at edge)
                         │
                   desired rate = shapedError / τ    (exponential approach)
                         │
                   clamp |rate| ≤ maxRate
                         │
                   rate easing: 1st-order filter + accel limit
                         │
                   cameraHeading += rate·dt          (world-frame target)
                         │
motor target = cameraHeading − handleYaw  ──► SimpleFOC angle loop
```

Design choices:

- **Frame-rate-independent easing.** The filter uses `α = 1 − e^(−dt/τ)`, so behavior doesn't change if the loop rate jitters.
- **Soft deadband.** A hard deadband causes a visible "kick" when the error crosses its edge. Here, a quadratic knee makes the output and its slope continuous.
- **Two-stage smoothing.** Position error is converted to a target rate, then the rate itself is eased. That gives the cinematic "slow-in / slow-out" feel.
- **Hysteresis.** Once following starts, it continues until the camera is nearly centered (`settleWindow`). This prevents start/stop chatter at the deadband edge.

## Code

```cpp
#include <Arduino.h>
#include <SimpleFOC.h>

// ---------------- Hardware (adjust to your board) ----------------
BLDCMotor motor(7);                                   // gimbal motor pole pairs
BLDCDriver3PWM driver(9, 5, 6, 8);
MagneticSensorSPI encoder(AS5048_SPI, 10);

// Replace with your IMU fusion output (radians, world-frame yaw of the handle).
float readHandleYaw();

// ---------------- Helpers ----------------
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}
static inline float signf(float x) { return (x > 0) - (x < 0); }

// ---------------- Follow controller ----------------
struct FollowParams {
  float deadband      = radians(4.0f);   // ignore jitter inside ±4°
  float knee          = radians(3.0f);   // soft transition width beyond deadband
  float settleWindow  = radians(0.5f);   // hysteresis: stop following when this close
  float tau           = 0.45f;           // s, exponential approach time constant
  float maxRate       = radians(120.0f); // rad/s, cap on follow speed
  float rateTau       = 0.12f;           // s, rate easing (slow-in/slow-out)
  float maxAccel      = radians(400.0f); // rad/s^2, accel limit
  float motorLimit    = radians(170.0f); // mechanical / cable limit for pan
};

class SmoothFollow {
public:
  FollowParams p;

  void reset(float handleYaw) {
    cameraHeading = handleYaw;
    rate = 0.0f;
    following = false;
  }

  // Returns the motor angle target (relative to the handle).
  float update(float handleYaw, float dt) {
    if (dt <= 0.0f || dt > 0.1f) dt = 0.001f;           // guard against stalls

    float err = wrapPi(handleYaw - cameraHeading);
    float aerr = fabsf(err);

    // --- Hysteresis on the deadband window ---
    if (!following && aerr > p.deadband)    following = true;
    if ( following && aerr < p.settleWindow) following = false;

    // --- Soft deadband shaping ---
    float shaped = 0.0f;
    if (following) {
      float db = (aerr > p.deadband) ? p.deadband : p.settleWindow;
      float x = aerr - db;                              // distance past window
      if (x > 0.0f) {
        // Quadratic knee: y = x²/(2k) for x<k, then linear (C1 continuous)
        float y = (x < p.knee) ? (x * x) / (2.0f * p.knee)
                               : x - 0.5f * p.knee;
        shaped = signf(err) * y;
      }
    }

    // --- Exponential approach -> desired rate ---
    float desiredRate = shaped / p.tau;
    desiredRate = constrain(desiredRate, -p.maxRate, p.maxRate);

    // --- Rate easing (frame-rate independent) ---
    float alpha = 1.0f - expf(-dt / p.rateTau);
    float easedRate = rate + alpha * (desiredRate - rate);

    // --- Accel limit ---
    float maxDelta = p.maxAccel * dt;
    rate += constrain(easedRate - rate, -maxDelta, maxDelta);

    // --- Integrate world-frame camera heading ---
    cameraHeading = wrapPi(cameraHeading + rate * dt);

    // --- Convert to motor target (stabilization: cancel handle motion) ---
    float motorTarget = wrapPi(cameraHeading - handleYaw);

    // Mechanical limit: if hit, drag the camera heading along with the handle.
    if (fabsf(motorTarget) > p.motorLimit) {
      motorTarget = signf(motorTarget) * p.motorLimit;
      cameraHeading = wrapPi(handleYaw + motorTarget);
      rate = 0.0f;
    }
    return motorTarget;
  }

  // Quick "recenter" (e.g. double-tap): ease the camera back to the handle heading.
  void recenter() { following = true; }

  float heading() const { return cameraHeading; }
  bool isFollowing() const { return following; }

private:
  float cameraHeading = 0.0f;
  float rate = 0.0f;
  bool following = false;
};

SmoothFollow follow;
float motorZero = 0.0f;           // encoder angle when the camera faces forward
unsigned long lastMicros = 0;

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 12;
  driver.init();
  motor.linkDriver(&driver);

  motor.controller = MotionControlType::angle;
  motor.torque_controller = TorqueControlType::voltage;

  // Gimbal motors: low voltage, stiff-but-smooth loops
  motor.voltage_limit = 4.0f;
  motor.PID_velocity.P = 0.15f;
  motor.PID_velocity.I = 4.0f;
  motor.PID_velocity.D = 0.0f;
  motor.PID_velocity.output_ramp = 1000.0f;
  motor.LPF_velocity.Tf = 0.01f;
  motor.P_angle.P = 18.0f;
  motor.velocity_limit = 20.0f;   // rad/s, the stabilization loop needs headroom

  motor.init();
  motor.initFOC();

  motorZero = motor.shaft_angle;
  follow.reset(readHandleYaw());
  lastMicros = micros();
}

// ---------------- Loop ----------------
void loop() {
  motor.loopFOC();                          // run as fast as possible

  // Follow/stabilization update at ~1 kHz
  unsigned long now = micros();
  float dt = (now - lastMicros) * 1e-6f;
  if (dt >= 0.001f) {
    lastMicros = now;
    float target = follow.update(readHandleYaw(), dt);
    motor.move(motorZero + target);
  } else {
    motor.move();                           // keep previous target
  }
}

// ---------------- IMU stub ----------------
float readHandleYaw() {
  // Return fused world-frame yaw (rad) of the handle from your IMU
  // (e.g. a Mahony/Madgwick filter on an MPU6050/BMI270/ICM-42688).
  return 0.0f;
}
```

## Tuning guide

| Parameter | Effect | Pocket-like starting point |
|---|---|---|
| `deadband` | Jitter ignored before following starts | 3–5° |
| `knee` | Softness of the follow onset | 2–4° |
| `settleWindow` | Where following stops (hysteresis) | 0.3–1° |
| `tau` | Follow lag. Bigger is lazier and more cinematic | 0.3 s (fast) to 0.8 s (slow) |
| `maxRate` | Top pan speed | 90–180 °/s |
| `rateTau` | Ease-in/out smoothness | 0.08–0.2 s |
| `maxAccel` | Prevents whip on sudden turns | 300–600 °/s² |

## Extending to tilt and roll

- **Tilt:** use a second `SmoothFollow` instance with a larger `tau` and tighter `motorLimit`, for example ±90°.
- **Roll:** don't follow. Lock it to the horizon by setting `cameraHeading = 0` and keeping `rate = 0`.
- **Speed presets:** expose modes by scaling `tau`, `maxRate`, and `rateTau` together:
  - Slow: ×1.6
  - Normal: ×1.0
  - Fast: ×0.6
