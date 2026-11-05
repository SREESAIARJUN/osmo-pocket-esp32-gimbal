# SimpleFOC Smooth Follow Mode: Exponential Rate Easing and Deadband Window

This is a follow-mode controller for an Osmo Pocket–style handheld gimbal. Small handle jitter is ignored. Deliberate pans are tracked with a slow, cinematic ease-in and ease-out. The controller re-centers without overshoot or hunting.

I couldn't tell what "Cycle 30" refers to. I've treated it as a revision label and tagged the config with it. If you meant a 30 Hz follow-update rate, set `FOLLOW_HZ = 30`. The math is dt-independent, so the motion feel stays the same at any rate.

---

## 1. Algorithm Overview

Each follow tick runs this chain for every axis:

```
handle_angle (IMU, world frame)
        │
        ▼
 error = wrap(handle_angle − cam_setpoint)
        │
        ▼
 ┌──────────────────────────┐
 │ Deadband window          │  inner/outer hysteresis + C¹-smooth soft edge
 └──────────────────────────┘
        │  shaped_error
        ▼
 desired_rate = shaped_error / τ_follow      (clamped to ±max_rate)
        │
        ▼
 ┌──────────────────────────┐
 │ Exponential rate easing  │  rate += (desired − rate)·(1 − e^(−dt/τ_ease))
 │ + acceleration limit     │
 └──────────────────────────┘
        │  cam_rate
        ▼
 cam_setpoint += cam_rate · dt                (world-frame camera heading)
        │
        ▼
 joint_target = wrap(cam_setpoint − handle_angle)   → motor.move()
```

The design rests on four ideas:

- **The camera setpoint lives in world frame.** The motor counter-rotates against the handle, which provides stabilization. The follow logic only moves the world setpoint, which provides the follow behaviour.
- **The deadband window uses hysteresis.**
  - In the `SETTLED` state, the camera stays still until the error exceeds `outer` (for example 6°).
  - In the `FOLLOWING` state, it tracks until the error drops below `inner` (for example 0.8°) and the rate has decayed.
  - This prevents the start-stop "hunting" you see with a single threshold.
- **The soft deadband is C¹-continuous.** The shaped error ramps in quadratically across a blend zone. Rate demand therefore starts at zero slope, with no kick when the error crosses the threshold.
- **Easing is exponential.** It uses `α = 1 − exp(−dt/τ)` rather than a fixed alpha, so tuning doesn't change if the loop rate changes. An acceleration cap guarantees a smooth ease-in even on a sudden large error.

---

## 2. Code (Arduino / SimpleFOC)

```cpp
// ============================================================================
//  Smooth Follow Mode — SimpleFOC handheld gimbal (pan + tilt)
//  Rev: Cycle 30
// ============================================================================
#include <SimpleFOC.h>
#include <math.h>

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
static inline float wrapPi(float a) {            // wrap to (-PI, PI]
  while (a >  PI) a -= _2PI;
  while (a <= -PI) a += _2PI;
  return a;
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float signf(float x) { return (x > 0) - (x < 0); }

// ----------------------------------------------------------------------------
// Follow configuration (per axis)
// ----------------------------------------------------------------------------
struct FollowConfig {
  float inner_deg      = 0.8f;   // settle threshold (exit FOLLOWING)
  float outer_deg      = 6.0f;   // engage threshold (enter FOLLOWING)
  float blend_deg      = 3.0f;   // soft-edge width for C1-smooth ramp-in
  float tau_follow     = 0.45f;  // s  — error→rate time constant ("follow speed")
  float tau_ease       = 0.25f;  // s  — rate easing time constant ("smoothness")
  float max_rate_dps   = 90.0f;  // deg/s cap on camera rate
  float max_accel_dps2 = 180.0f; // deg/s² cap — guarantees ease-in
  float settle_rate_dps= 1.5f;   // must be slower than this to declare settled
  bool  wrap360        = true;   // pan: true (continuous), tilt: false
  float min_deg        = -90.0f; // soft limits for non-wrapping axes
  float max_deg        =  30.0f;
};

// ----------------------------------------------------------------------------
// Smooth follow axis controller
// ----------------------------------------------------------------------------
class SmoothFollowAxis {
public:
  enum State : uint8_t { SETTLED, FOLLOWING, LOCKED };

  explicit SmoothFollowAxis(const FollowConfig& c) : cfg(c) {}

  void reset(float handle_angle) {
    cam_sp   = handle_angle;
    cam_rate = 0.0f;
    state    = SETTLED;
  }

  void setLocked(bool lock) {               // lock mode: hold world heading
    state = lock ? LOCKED : SETTLED;
    if (lock) cam_rate = 0.0f;
  }

  void recenter() { recenter_req = true; }  // e.g. double-tap button

  // Returns world-frame camera setpoint (rad). dt in seconds.
  float update(float handle_angle, float dt) {
    if (dt <= 0.0f || dt > 0.1f) dt = 0.001f;   // guard against stalls

    const float inner = cfg.inner_deg * DEG_TO_RAD;
    const float outer = cfg.outer_deg * DEG_TO_RAD;
    const float blend = cfg.blend_deg * DEG_TO_RAD;
    const float maxR  = cfg.max_rate_dps   * DEG_TO_RAD;
    const float maxA  = cfg.max_accel_dps2 * DEG_TO_RAD;
    const float setR  = cfg.settle_rate_dps* DEG_TO_RAD;

    float err = handle_angle - cam_sp;
    if (cfg.wrap360) err = wrapPi(err);
    const float absErr = fabsf(err);

    // ---- 1. Deadband window state machine (hysteresis) ----------------
    if (recenter_req) { state = FOLLOWING; recenter_req = false; recentering = true; }

    switch (state) {
      case SETTLED:
        if (absErr > outer) state = FOLLOWING;
        break;
      case FOLLOWING:
        if (absErr < inner && fabsf(cam_rate) < setR) {
          state = SETTLED;
          recentering = false;
        }
        break;
      case LOCKED:
        break;
    }

    // ---- 2. Shaped error (soft deadband, C1-continuous) ---------------
    float desired_rate = 0.0f;
    if (state == FOLLOWING) {
      // While recentering, the window collapses to `inner` so we fully align.
      const float db = recentering ? inner : inner;     // active-window edge
      float shaped = softDeadband(err, db, blend);
      desired_rate = clampf(shaped / cfg.tau_follow, -maxR, maxR);
    }

    // ---- 3. Exponential rate easing (dt-independent) ------------------
    const float alpha = 1.0f - expf(-dt / cfg.tau_ease);
    float eased = cam_rate + (desired_rate - cam_rate) * alpha;

    // ---- 4. Acceleration limit (guaranteed smooth ease-in/out) --------
    const float dv = clampf(eased - cam_rate, -maxA * dt, maxA * dt);
    cam_rate += dv;

    // Snap tiny residual rates to zero once settled (no creep)
    if (state != FOLLOWING && fabsf(cam_rate) < 1e-4f) cam_rate = 0.0f;

    // ---- 5. Integrate world setpoint ----------------------------------
    cam_sp += cam_rate * dt;
    if (cfg.wrap360) {
      cam_sp = wrapPi(cam_sp);
    } else {
      const float lo = cfg.min_deg * DEG_TO_RAD, hi = cfg.max_deg * DEG_TO_RAD;
      if (cam_sp < lo) { cam_sp = lo; if (cam_rate < 0) cam_rate = 0; }
      if (cam_sp > hi) { cam_sp = hi; if (cam_rate > 0) cam_rate = 0; }
    }
    return cam_sp;
  }

  float setpoint() const { return cam_sp; }
  float rate()     const { return cam_rate; }
  State getState() const { return state; }

  FollowConfig cfg;

private:
  // Soft deadband: 0 inside |e|<db, quadratic ramp across [db, db+blend],
  // then linear (e - db - blend/2). Value AND slope are continuous.
  static float softDeadband(float e, float db, float blend) {
    const float a = fabsf(e);
    if (a <