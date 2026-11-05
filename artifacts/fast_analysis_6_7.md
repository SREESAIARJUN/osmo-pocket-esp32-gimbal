# SimpleFOC Smooth Follow Mode (Osmo Pocket–style)

I don't have context for what "Cycle 6" refers to in your project, so this is a complete, self-contained implementation you can adapt to your existing iteration.

## How It Works

```
handle heading ──► [error vs. camera setpoint] ──► [deadband + hysteresis]
                                                          │
                                                          ▼
                                     [exponential easing: rate = e_eff / τ]
                                                          │
                                                          ▼
                              [rate clamp] ──► [accel/jerk smoothing (EMA)]
                                                          │
                                                          ▼
                              camera setpoint ψ_set += rate·dt
                                                          │
                                                          ▼
           joint target = ψ_set − ψ_handle  ──► SimpleFOC angle loop (+ vel FF)
```

The behavior has four parts:

- **Deadband window.** Small hand jitters and walking sway are ignored, so the camera stays locked.
- **Hysteresis.** Once following starts, the camera keeps easing until it is almost centered (`exitBand`). This prevents the "stop short at the deadband edge" look.
- **Soft deadband edge.** A quadratic blend means there is no velocity step when the error crosses the threshold.
- **Exponential easing.** Rate is proportional to the remaining error, so the camera decelerates asymptotically, like a critically damped pan.

## Code

```cpp
#include <SimpleFOC.h>

// ---------------- Hardware (adjust to your board) ----------------
BLDCMotor motor(7);                                  // pole pairs
BLDCDriver3PWM driver(9, 10, 11, 8);
MagneticSensorSPI encoder(AS5048_SPI, 10);

// Provide these from your IMU fusion (radians, world frame yaw)
extern float imuHandleYaw();   // handle heading
// (joint angle comes from encoder: camera relative to handle)

// ---------------- Helpers ----------------
static inline float wrapPi(float a) {
  a = fmodf(a + _PI, _2PI);
  if (a < 0) a += _2PI;
  return a - _PI;
}
static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------- Follow controller ----------------
struct FollowParams {
  float deadband   = 0.087f;  // rad (~5°)  start following beyond this
  float exitBand   = 0.010f;  // rad (~0.6°) stop following below this (hysteresis)
  float softWidth  = 0.035f;  // rad, quadratic blend width at deadband edge
  float tau        = 0.45f;   // s, easing time constant (bigger = lazier)
  float maxRate    = 3.0f;    // rad/s, pan speed cap
  float tauRate    = 0.08f;   // s, rate smoothing (acceleration softness)
  float jointLimit = _PI;     // rad, mechanical stop (e.g. ±_PI for pan)
};

class SmoothFollow {
public:
  FollowParams p;

  void reset(float handleYaw) {
    setpoint_ = handleYaw;
    rate_ = 0;
    following_ = false;
  }

  // Returns camera world-yaw setpoint; outRate = setpoint velocity (feedforward)
  float update(float handleYaw, float dt, float &outRate) {
    if (dt <= 0 || dt > 0.1f) dt = 0.001f;           // guard against stalls

    float e = wrapPi(handleYaw - setpoint_);
    float ae = fabsf(e);

    // --- Deadband with hysteresis ---
    if (!following_ && ae > p.deadband)  following_ = true;
    if ( following_ && ae < p.exitBand && fabsf(rate_) < 0.02f) following_ = false;

    float eEff = 0.0f;
    if (following_) {
      if (ae >= p.deadband + p.softWidth || ae <= p.deadband) {
        // Inside the active zone: once following, track full error
        // so the camera re-centers completely (Osmo behavior)
        eEff = e;
      } else {
        // Soft edge: quadratic ramp-in avoids a velocity step
        float x = (ae - p.deadband) / p.softWidth;    // 0..1
        float blend = x * x * (3.0f - 2.0f * x);      // smoothstep
        eEff = e * (0.3f + 0.7f * blend);
      }
    }

    // --- Exponential easing: first-order approach to target ---
    float targetRate = clampf(eEff / p.tau, -p.maxRate, p.maxRate);

    // --- Rate smoothing (limits acceleration / jerk) ---
    float a = 1.0f - expf(-dt / p.tauRate);
    rate_ += a * (targetRate - rate_);

    // Don't overshoot the handle in a single step
    float step = rate_ * dt;
    if (fabsf(step) > ae && following_) step = e;
    setpoint_ = wrapPi(setpoint_ + step);

    // --- Mechanical limit: if joint would exceed range, drag setpoint ---
    float joint = wrapPi(setpoint_ - handleYaw);
    if (fabsf(joint) > p.jointLimit) {
      setpoint_ = wrapPi(handleYaw + copysignf(p.jointLimit, joint));
    }

    outRate = rate_;
    return setpoint_;
  }

  bool isFollowing() const { return following_; }

private:
  float setpoint_ = 0;
  float rate_ = 0;
  bool following_ = false;
};

SmoothFollow follow;
float jointZero = 0;     // encoder reading when camera faces forward
uint32_t lastUs = 0;

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 8;
  driver.init();
  motor.linkDriver(&driver);

  motor.controller = MotionControlType::angle;
  motor.torque_controller = TorqueControlType::voltage;
  motor.voltage_limit = 4;

  // Gimbal-friendly tuning: soft, well-damped
  motor.P_angle.P = 18;
  motor.PID_velocity.P = 0.15f;
  motor.PID_velocity.I = 2.0f;
  motor.PID_velocity.D = 0;
  motor.LPF_velocity.Tf = 0.01f;
  motor.velocity_limit = 8;

  motor.init();
  motor.initFOC();

  jointZero = motor.shaft_angle;
  follow.reset(imuHandleYaw());
  lastUs = micros();
}

// ---------------- Loop ----------------
void loop() {
  motor.loopFOC();                       // run as fast as possible

  // Outer follow loop at ~1 kHz
  uint32_t now = micros();
  if (now - lastUs < 1000) return;
  float dt = (now - lastUs) * 1e-6f;
  lastUs = now;

  float handleYaw = imuHandleYaw();
  float camRate;
  float camSetpoint = follow.update(handleYaw, dt, camRate);

  // Camera-relative-to-handle joint target
  float jointTarget = wrapPi(camSetpoint - handleYaw);

  // Unwrap relative to current shaft angle to avoid 2π jumps
  float cur = motor.shaft_angle - jointZero;
  float target = cur + wrapPi(jointTarget - cur);

  motor.move(jointZero + target);
}
```

## Tuning Guide

| Parameter   | Effect                          | Osmo-like start  | Range       |
|-------------|---------------------------------|------------------|-------------|
| `deadband`  | Ignored hand motion             | 5°               | 2–10°       |
| `exitBand`  | How fully it re-centers         | 0.6°             | 0.3–2°      |
| `softWidth` | Smoothness of follow onset      | 2°               | 1–5°        |
| `tau`       | Pan laziness (cinematic feel)   | 0.45 s           | 0.2–1.2 s   |
| `maxRate`   | Fast-whip cap                   | 3 rad/s          | 1–6         |
| `tauRate`   | Acceleration softness           | 0.08 s           | 0.03–0.2 s  |

**Presets:**

- **Slow / cinematic:** `tau=0.9`, `deadband=8°`, `maxRate=1.5`.
- **Fast / FPV-ish:** `tau=0.15`, `deadband=1°`, `tauRate=0.03`.

## Notes

- **Stabilization comes from the inner loop.** If you have a camera-side IMU, close the stabilization loop on camera yaw, using `camSetpoint` as the reference, instead of relying only on the encoder joint. This rejects motor compliance.
- **Velocity feedforward.** `camRate` is available as feedforward. If you run a custom loop, add `camRate − handleYawRate` to the velocity target to reduce lag.
- **Recenter button.** Call `follow.reset(imuHandleYaw())`, or temporarily set `tau` small, to snap back to center.
- **Tilt and roll axes.** Use the same class per axis. Tilt typically uses a larger deadband, and roll is usually locked (follow disabled).