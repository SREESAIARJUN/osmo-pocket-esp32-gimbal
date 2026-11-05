# SimpleFOC Smooth Follow Mode: Exponential Easing + Deadband (Cycle 5)

This is a follow-mode controller for an Osmo Pocket–style handheld gimbal. The camera holds a world-frame heading. When the handle turns, the setpoint glides after it with cinematic easing. Small hand jitters inside a deadband window are ignored.

**Assumptions** (I'm not sure what earlier cycles specified, so these are stated up front):
- SimpleFOC v2.x API.
- One IMU on the camera head.
- An absolute magnetic encoder on each motor.
- Pan and tilt follow, and roll is locked level.
- Control runs as a fast FOC loop plus a 500 Hz follow loop.

---

## 1. Algorithm Overview

```
handle_heading ──► error = wrap(handle - setpoint)
                        │
                 ┌──────▼───────┐
                 │ Soft deadband│  hysteresis + C¹-continuous ramp-in
                 └──────┬───────┘
                 ┌──────▼───────┐
                 │ Exp. approach│  rate_cmd = e_eff / τ_follow   (clamped)
                 └──────┬───────┘
                 ┌──────▼───────┐
                 │ Rate easing  │  1st-order filter, α = 1 - e^(-dt/τ_ease)
                 └──────┬───────┘   + acceleration limit (jerk-free start/stop)
                 ┌──────▼───────┐
                 │  Integrate   │  setpoint += rate · dt
                 └──────┬───────┘
          motor_target = wrap(setpoint - handle_heading)  ──► SimpleFOC angle mode
```

### Why each stage exists
| Stage | Purpose |
|---|---|
| **Deadband window** | Stops walking/breathing micro-motion from creeping into the shot. |
| **Hysteresis** | Prevents chattering at the window edge. Following engages at `db_enter` and releases at `db_exit < db_enter`. |
| **Soft ramp-in** | Removes the step in effective error at the window edge, so there is no visible "kick" when following starts. |
| **Exponential approach** | Rate is proportional to error, so the camera decelerates naturally as it catches up. |
| **Rate easing** | Smooths ease-in and ease-out. It is time-constant based and therefore independent of loop rate. |
| **Accel limit** | Caps the second derivative so fast handle swings still produce a smooth pan. |

---

## 2. Code

```cpp
#include <Arduino.h>
#include <SimpleFOC.h>

// ============================================================
//  Utilities
// ============================================================
static inline float wrapPi(float a) {
  while (a >  _PI) a -= _2PI;
  while (a < -_PI) a += _2PI;
  return a;
}

static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// Frame-rate-independent exponential smoothing coefficient
static inline float expAlpha(float dt, float tau) {
  return (tau <= 0.0f) ? 1.0f : 1.0f - expf(-dt / tau);
}

// ============================================================
//  Follow axis: deadband + exponential easing
// ============================================================
struct FollowParams {
  float db_enter;      // rad: error needed to START following
  float db_exit;       // rad: error at which following STOPS (< db_enter)
  float db_soft;       // rad: width of the smooth ramp-in past the window edge
  float tau_follow;    // s  : exponential approach time constant
  float tau_ease;      // s  : rate easing time constant
  float max_rate;      // rad/s
  float max_accel;     // rad/s^2
  float recenter_gain; // multiplier on tau during recenter (smaller = faster)
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float heading) {
    setpoint = heading;
    rate = 0.0f;
    following = false;
  }

  void requestRecenter() { recentering = true; }

  // handle: handle heading in world frame (rad)
  // returns: camera setpoint in world frame (rad)
  float update(float handle, float dt) {
    if (dt <= 0.0f || dt > 0.05f) dt = 0.002f;   // guard against stalls

    float err = wrapPi(handle - setpoint);
    float aerr = fabsf(err);

    // ---- 1. Deadband with hysteresis ----
    if (!following && aerr > P.db_enter) following = true;
    if ( following && aerr < P.db_exit && fabsf(rate) < 0.02f) following = false;
    if (recentering) following = true;

    // ---- 2. Soft (C1-continuous) effective error ----
    float e_eff = 0.0f;
    if (following) {
      float db = recentering ? 0.0f : P.db_exit;
      float over = aerr - db;
      if (over > 0.0f) {
        // Quadratic ramp-in for 'over' < db_soft, linear beyond.
        // Value and slope match at the junction, so there is no kick.
        float mag = (over < P.db_soft)
                  ? (over * over) / (2.0f * P.db_soft)
                  : over - 0.5f * P.db_soft;
        e_eff = copysignf(mag, err);
      }
    }

    // ---- 3. Exponential approach -> commanded rate ----
    float tau = recentering ? P.tau_follow * P.recenter_gain : P.tau_follow;
    float rate_cmd = clampf(e_eff / tau, -P.max_rate, P.max_rate);

    // ---- 4. Rate easing (exponential) + accel limit ----
    float eased = rate + expAlpha(dt, P.tau_ease) * (rate_cmd - rate);
    float dmax  = P.max_accel * dt;
    rate += clampf(eased - rate, -dmax, dmax);

    // ---- 5. Integrate ----
    setpoint = wrapPi(setpoint + rate * dt);

    if (recentering && aerr < 0.005f && fabsf(rate) < 0.01f) recentering = false;
    return setpoint;
  }

  float getRate() const { return rate; }
  bool  isFollowing() const { return following; }

private:
  FollowParams P;
  float setpoint = 0.0f;
  float rate = 0.0f;
  bool following = false;
  bool recentering = false;
};

// ============================================================
//  Tuning: Pocket-style "cinematic" profile
// ============================================================
FollowParams panParams = {
  /*db_enter*/      radians(4.0f),
  /*db_exit*/       radians(1.5f),
  /*db_soft*/       radians(6.0f),
  /*tau_follow*/    0.60f,
  /*tau_ease*/      0.18f,
  /*max_rate*/      radians(120.0f),
  /*max_accel*/     radians(360.0f),
  /*recenter_gain*/ 0.35f
};

FollowParams tiltParams = {
  radians(5.0f), radians(2.0f), radians(6.0f),
  0.80f, 0.22f, radians(90.0f), radians(250.0f), 0.35f
};

FollowAxis panFollow(panParams);
FollowAxis tiltFollow(tiltParams);

// ============================================================
//  Hardware (adjust pins / pole pairs for your build)
// ============================================================
BLDCMotor panMotor(7);
BLDCDriver3PWM panDriver(9, 5, 6, 8);
MagneticSensorSPI panEnc(AS5048_SPI, 10);

BLDCMotor tiltMotor(7);
BLDCDriver3PWM tiltDriver(3, 11, 10, 7);
MagneticSensorSPI tiltEnc(AS5048_SPI, 4);

// ---- IMU stub: replace with your fusion (Mahony/Madgwick/BMI270 etc.) ----
struct Euler { float yaw, pitch, roll; };
Euler readCameraIMU();   // camera-head orientation in world frame (rad)

// Mechanical zero offsets (motor angle when camera faces straight ahead)
float panZero = 0.0f, tiltZero = 0.0f;

// ============================================================
//  Setup
// ============================================================
void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 8.4f;     // 2S
  d.init();
  m.linkDriver(&d);

  m.foc_modulation = FOCModulationType::SpaceVectorPWM;
  m.controller     = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;

  m.P_angle.P = 18.0f;
  m.PID_velocity.P = 0.12f;
  m.PID_velocity.I = 4.0f;
  m.PID_velocity.D = 0.0f;
  m.LPF_velocity.Tf = 0.008f;
  m.voltage_limit = 4.0f;
  m.velocity_limit = 20.0f;

  m.init();
  m.initFO