# SimpleFOC Smooth Follow Mode: Exponential Easing + Deadband (Cycle 28)

This is a follow-mode controller for a 2-axis (pan/tilt) handheld gimbal in the style of the Osmo Pocket. Small hand movements inside a deadband are fully stabilized. Larger movements make the camera glide toward the handle direction with a critically damped exponential ease, with no jerk at start or stop.

## How it works

```
handle IMU ──► handleAngle ──┐
                             ▼
      error = wrap(handle − camTarget)
                             ▼
      soft deadband + hysteresis  (ignore jitter, latch while following)
                             ▼
      desired rate = shapedError / τ_follow    (exponential approach)
                             ▼
      rate easing (1st-order LPF) + accel limit + max rate clamp
                             ▼
      camTarget += rate · dt        (world-frame camera heading)
                             ▼
      motor setpoint = wrap(camTarget − handleAngle) → SimpleFOC angle loop
```

Key design points:
- **Frame-rate independent easing**: `α = 1 − exp(−dt/τ)` instead of a fixed alpha.
- **Soft deadband**: a quadratic transition band, so following starts with zero slope and does not kick.
- **Hysteresis latch**: once following starts, it continues until the camera has nearly caught up (`settleBand`). This avoids the "stutter-follow" common in naive deadbands.
- **Two-stage easing**: the position error drives a rate, and the rate itself is eased and acceleration-limited. The result is an S-curve feel.

---

## Code

```cpp
// ============================================================================
//  Smooth Follow Mode for SimpleFOC handheld gimbal  —  Cycle 28
//  Target: ESP32 / STM32 + SimpleFOC >= 2.3, 2x gimbal BLDC, AS5600/AS5048 encoders
// ============================================================================
#include <SimpleFOC.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
static inline float wrapPi(float a) {
  a = fmodf(a + PI, _2PI);
  if (a < 0) a += _2PI;
  return a - PI;
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float signf(float x) { return (x > 0) - (x < 0); }

// Frame-rate-independent exponential smoothing coefficient
static inline float expAlpha(float dt, float tau) {
  return (tau <= 1e-6f) ? 1.0f : 1.0f - expf(-dt / tau);
}

// ---------------------------------------------------------------------------
// Follow axis parameters
// ---------------------------------------------------------------------------
struct FollowParams {
  float deadband;      // rad  - error ignored inside this window
  float softBand;      // rad  - quadratic blend width past the deadband
  float settleBand;    // rad  - stop following when error drops below this
  float tauFollow;     // s    - exponential approach time constant
  float tauRateEase;   // s    - smoothing of commanded rate (S-curve feel)
  float maxRate;       // rad/s
  float maxAccel;      // rad/s^2
  bool  limited;       // mechanical limits (tilt)
  float minAngle, maxAngle; // rad, world-frame target limits if limited
};

// ---------------------------------------------------------------------------
// Follow axis controller
// ---------------------------------------------------------------------------
class FollowAxis {
public:
  explicit FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float handleAngle) {
    camTarget = handleAngle;
    rate = 0;
    following = false;
  }

  void recenter(float handleAngle, float durationS = 0.6f) {
    // Temporarily force follow with zero deadband for a quick recenter
    recenterTimer = durationS;
    following = true;
    (void)handleAngle;
  }

  // Returns motor setpoint (rad, relative to handle) for SimpleFOC angle mode
  float update(float handleAngle, float dt) {
    if (dt <= 0 || dt > 0.05f) dt = 0.002f;   // guard against stalls

    float err = P.limited ? (handleAngle - camTarget)
                          : wrapPi(handleAngle - camTarget);
    float absErr = fabsf(err);

    // --- Hysteresis latch ---------------------------------------------------
    bool recentering = recenterTimer > 0;
    if (recentering) recenterTimer -= dt;

    if (!following && absErr > P.deadband) following = true;
    if (following && !recentering && absErr < P.settleBand && fabsf(rate) < 0.02f)
      following = false;

    // --- Soft deadband shaping ---------------------------------------------
    float shaped = 0.0f;
    if (recentering) {
      shaped = err;
    } else if (following) {
      // When latched, use the settle band as the inner edge so motion continues
      float db = (absErr > P.deadband) ? P.deadband : P.settleBand;
      shaped = softDeadband(err, db, P.softBand);
    }

    // --- Exponential approach -> desired rate ------------------------------
    float tau = recentering ? 0.15f : P.tauFollow;
    float desiredRate = clampf(shaped / tau, -P.maxRate, P.maxRate);

    // --- Rate easing (1st order) -------------------------------------------
    float a = expAlpha(dt, P.tauRateEase);
    float easedRate = rate + a * (desiredRate - rate);

    // --- Acceleration limit ------------------------------------------------
    float maxDelta = P.maxAccel * dt;
    rate += clampf(easedRate - rate, -maxDelta, maxDelta);

    // --- Integrate world-frame camera target -------------------------------
    camTarget += rate * dt;
    if (P.limited) {
      float c = clampf(camTarget, P.minAngle, P.maxAngle);
      if (c != camTarget) { camTarget = c; rate = 0; }
    } else {
      camTarget = wrapPi(camTarget);
    }

    // Motor holds camera at camTarget in world frame:
    // cameraWorld = handle + motorRel  ->  motorRel = camTarget - handle
    return P.limited ? (camTarget - handleAngle)
                     : wrapPi(camTarget - handleAngle);
  }

  float target() const { return camTarget; }
  float currentRate() const { return rate; }
  bool  isFollowing() const { return following; }

  FollowParams P;

private:
  // Zero inside db, quadratic over softBand (C1 continuous), linear beyond
  static float softDeadband(float e, float db, float sb) {
    float ae = fabsf(e);
    if (ae <= db) return 0.0f;
    float x = ae - db;
    float out = (x < sb) ? (x * x) / (2.0f * sb)   // slope 0 -> 1
                         : x - 0.5f * sb;          // continuous linear
    return signf(e) * out;
  }

  float camTarget = 0;
  float rate = 0;
  bool  following = false;
  float recenterTimer = 0;
};

// ---------------------------------------------------------------------------
// Tuned presets (Osmo-Pocket-like feel)
// ---------------------------------------------------------------------------
const FollowParams PAN_PARAMS = {
  /*deadband*/    radians(3.0f),
  /*softBand*/    radians(4.0f),
  /*settleBand*/  radians(0.4f),
  /*tauFollow*/   0.45f,
  /*tauRateEase*/ 0.12f,
  /*maxRate*/     radians(180.0f),
  /*maxAccel*/    radians(600.0f),
  /*limited*/     false, 0, 0
};

const FollowParams TILT_PARAMS = {
  radians(2.5f), radians(3.0f), radians(0.3f),
  0.55f, 0.15f,
  radians(120.0f), radians(400.0f),
  true, radians(-90.0f), radians(35.0f)
};

// Optional "fast follow" mode (Osmo FPV-ish)
const FollowParams PAN_FAST = {
  radians(1.5f), radians(2.0f), radians(0.3f),
  0.18f, 0.06f, radians(360.0f), radians(1500.0f), false, 0, 0
};

// ---------------------------------------------------------------------------
// Hardware (adjust pins for your board)
// ---------------------------------------------------------------------------
BLDCMotor panMotor(7);    // pole pairs
BLDCMotor tiltMotor(7);
BLDCDriver3PWM panDriver(25, 26, 27, 14);
BLDCDriver3PWM tiltDriver(32, 33, 13, 12);

MagneticSensorI2C panEnc  = MagneticSensorI2C(AS5600_I2C);
MagneticSensorI2C tiltEnc = MagneticSensorI2C(AS5600_I2C);
TwoWire I2Cb = TwoWire(1);

FollowAxis panFollow(PAN_PARAMS);
FollowAxis tiltFollow(TILT_PARAMS);

// ---------------------------------------------------------