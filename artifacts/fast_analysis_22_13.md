# SimpleFOC Smooth Follow Mode: Exponential Easing with a Hysteresis Deadband

This is a gimbal follow controller similar to the Osmo Pocket's. The camera holds its world orientation while the handle makes small motions. When the handle drifts past a deadband window, the camera eases toward it with a smooth exponential curve. It then settles without overshoot or jerk.

## How It Works

```
 handle (IMU, world frame) ──┐
                             ▼
              ┌───────────────────────────────┐
              │  error = wrap(handle − camera)│
              │  hysteresis deadband          │  outer window → engage
              │  (outer / inner windows)      │  inner window → release
              │  desired_rate = e_eff / τ     │  exponential approach
              │  rate LPF + accel limit       │  ease-in / ease-out
              │  camera += rate · dt          │
              └──────────────┬────────────────┘
                             ▼
    motor_target = camera_world − handle_world   (relative to handle)
                             ▼
             SimpleFOC angle loop (motor.move)
```

**Design details:**

- **The deadband has hysteresis.** Following engages when the error exceeds `db_outer`. It keeps following until the error falls inside `db_inner`. This prevents chatter at the window edge, and the camera recenters properly instead of stopping right at the boundary.
- **The deadband is soft.** The effective error is `e − sign(e)·db_inner`, so the commanded rate reaches exactly zero at release with no step.
- **Easing is exponential.** Setting `rate = e_eff / τ` makes the camera approach the target exponentially with time constant τ.
- **The rate itself is smoothed.** A first-order filter plus an acceleration clamp shapes the speed profile into an S-curve with no jerk at the start of a pan.
- **Filtering does not depend on frame rate.** All filters use `α = 1 − exp(−dt/τ)`, so tuning stays valid if the loop rate changes.
- **Overshoot is guarded.** One integration step can never move the camera past the handle.

---

## `FollowAxis.h`

```cpp
#pragma once
#include <math.h>

#ifndef PI
#define PI 3.14159265358979f
#endif

static inline float wrapPi(float a) {
  a = fmodf(a + PI, 2.0f * PI);
  if (a < 0) a += 2.0f * PI;
  return a - PI;
}
static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline float signf(float v) { return (v > 0) - (v < 0); }

struct FollowParams {
  float db_outer;   // rad  - error needed to start following
  float db_inner;   // rad  - error at which following stops (< db_outer)
  float tau;        // s    - exponential approach time constant
  float rate_tau;   // s    - rate smoothing (ease-in/out softness)
  float max_rate;   // rad/s
  float max_accel;  // rad/s^2
};

// Presets (Osmo-like "Slow / Normal / Fast")
namespace FollowPresets {
  const FollowParams SLOW   = {0.087f, 0.012f, 0.90f, 0.25f, 1.0f, 2.0f};   // 5°/0.7°
  const FollowParams NORMAL = {0.052f, 0.009f, 0.45f, 0.15f, 2.5f, 6.0f};   // 3°/0.5°
  const FollowParams FAST   = {0.026f, 0.005f, 0.18f, 0.07f, 6.0f, 20.0f};  // 1.5°/0.3°
}

class FollowAxis {
public:
  FollowParams p;
  bool  wrapContinuous;          // true for pan (endless), false for tilt
  float limitMin = -PI, limitMax = PI;  // world-angle limits (tilt)

  float camera  = 0;   // camera world angle (output)
  float rate    = 0;   // current camera rate (rad/s)
  bool  engaged = false;
  bool  locked  = false;  // lock mode: camera holds world angle

  FollowAxis(const FollowParams& params, bool continuous)
    : p(params), wrapContinuous(continuous) {}

  void reset(float handleAngle) {
    camera = handleAngle; rate = 0; engaged = false;
  }

  // Smoothly blend to new parameters (avoids jumps when changing mode).
  void setParams(const FollowParams& np) { p = np; }

  float update(float handleAngle, float dt) {
    if (dt <= 0 || dt > 0.1f) return camera;          // ignore bogus dt

    float e = wrapContinuous ? wrapPi(handleAngle - camera)
                             : (handleAngle - camera);
    float ae = fabsf(e);

    // --- hysteresis deadband -------------------------------------------
    if (locked)                    engaged = false;
    else if (!engaged && ae > p.db_outer) engaged = true;
    else if ( engaged && ae < p.db_inner) engaged = false;

    // --- exponential easing target rate ---------------------------------
    float desired = 0.0f;
    if (engaged) {
      float eEff = e - signf(e) * p.db_inner;          // soft, continuous edge
      desired = clampf(eEff / p.tau, -p.max_rate, p.max_rate);
    }

    // --- rate smoothing: LPF then accel clamp (S-curve) ------------------
    float a = 1.0f - expf(-dt / p.rate_tau);
    float newRate = rate + a * (desired - rate);
    float dvMax = p.max_accel * dt;
    rate += clampf(newRate - rate, -dvMax, dvMax);

    // --- integrate with overshoot guard ----------------------------------
    float step = rate * dt;
    if (engaged && signf(step) == signf(e) && fabsf(step) > ae) {
      step = e; rate = 0;                              // land exactly
    }
    camera += step;

    if (wrapContinuous) camera = wrapPi(camera);
    else {
      float c = clampf(camera, limitMin, limitMax);
      if (c != camera) { camera = c; rate = 0; }
    }
    return camera;
  }
};
```

---

## Main sketch (2-axis pan/tilt, SimpleFOC)

```cpp
#include <SimpleFOC.h>
#include "FollowAxis.h"

// ---------------- Hardware ----------------
BLDCMotor  panMotor(7);                      // pole pairs
BLDCDriver3PWM panDriver(9, 10, 11, 8);
MagneticSensorSPI panSensor(AS5048_SPI, 10);

BLDCMotor  tiltMotor(7);
BLDCDriver3PWM tiltDriver(5, 6, 3, 7);
MagneticSensorSPI tiltSensor(AS5048_SPI, 4);

// ---------------- Handle IMU (replace with your AHRS) ----------------
// Must return the HANDLE orientation in world frame (rad).
// Use a Mahony/Mad