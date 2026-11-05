# SimpleFOC Smooth Follow Mode: Exponential Easing + Deadband (Cycle 18)

This is a pan/tilt follow controller for a pocket-style handheld gimbal. The camera holds still against small hand jitter. When the handle moves deliberately, the camera eases toward the new heading with a critically-damped-feeling exponential response. Velocity and acceleration are capped to prevent "whip" on fast pans.

## Control Architecture

```
            IMU (on camera)            Encoder (motor)
                 │                          │
           cam_angle (world)          motor_angle (rel)
                 └──────────┬───────────────┘
                handle = cam − motor        ← reconstructed handle pose
                            │
              err = wrap(handle − follow)
                            │
               ┌── Soft deadband window ──┐  (C1-continuous, no snap at edge)
                            │
               v_des = e_db / τ             ← exponential easing (1st-order)
                            │
               slew-limit accel, clamp vel   ← cinematic limits
                            │
            follow += v · dt                 ← smoothed world target
                            │
        motor_target = follow − handle       ← feed-forward placement
                            │
          SimpleFOC angle loop (P_angle → velocity PID → FOC)
```

The key identity is `cam = handle + motor`. To make the camera point at `follow`, command `motor = follow − handle`. Stabilization comes "for free" from feed-forward. The follow filter decides only *where* the camera should look, not how hard to fight the hand.

## Code

```cpp
// ============================================================================
//  SimpleFOC Smooth Follow Mode — Cycle 18
//  Exponential rate easing + soft deadband window, pan & tilt axes
// ============================================================================
#include <Arduino.h>
#include <SimpleFOC.h>
#include <math.h>

// ---------------------------------------------------------------------------
//  Utilities
// ---------------------------------------------------------------------------
static inline float wrapPi(float a) {
  // Wrap to (-PI, PI]
  a = fmodf(a + _PI, _2PI);
  if (a < 0) a += _2PI;
  return a - _PI;
}

static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// Soft deadband: zero inside ±db, then a quadratic blend region of width `knee`
// so the slope ramps 0→1 continuously (no velocity step when leaving the window).
static float softDeadband(float e, float db, float knee) {
  float a = fabsf(e);
  if (a <= db) return 0.0f;
  float s = (e > 0) ? 1.0f : -1.0f;
  if (a < db + knee) {
    float x = a - db;                       // 0..knee
    return s * (x * x) / (2.0f * knee);     // slope goes 0 → 1 across knee
  }
  return s * (a - db - 0.5f * knee);        // linear, continuous with the blend
}

// ---------------------------------------------------------------------------
//  Follow axis filter
// ---------------------------------------------------------------------------
struct FollowParams {
  float deadband;      // rad   — ignore handle motion inside this window
  float knee;          // rad   — soft-edge width of deadband
  float tau;           // s     — easing time constant (bigger = lazier)
  float maxRate;       // rad/s — cinematic pan speed ceiling
  float maxAccel;      // rad/s² — ease-in/out limit
  float limitMin;      // rad   — mechanical motor range (relative)
  float limitMax;
  bool  wraps;         // true for continuous yaw
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float handleAngle) {
    follow = handleAngle;
    vel = 0.0f;
  }

  // Returns the motor angle setpoint (relative, rad)
  float update(float camAngle, float motorAngle, float dt) {
    // 1. Reconstruct handle orientation in world frame
    float handle = camAngle - motorAngle;

    // 2. Error between where the handle points and where we're following
    float err = handle - follow;
    if (P.wraps) err = wrapPi(err);

    // 3. Deadband window (soft edge)
    float e = softDeadband(err, P.deadband, P.knee);

    // 4. Exponential easing → desired rate.
    //    v = e/τ gives a first-order approach: e(t) ∝ exp(-t/τ)
    float vDes = clampf(e / P.tau, -P.maxRate, P.maxRate);

    // 5. Acceleration slew limit (smooth start/stop of pans)
    float dvMax = P.maxAccel * dt;
    vel += clampf(vDes - vel, -dvMax, dvMax);

    // Kill residual creep once settled inside the window
    if (e == 0.0f && fabsf(vel) < 1e-3f) vel = 0.0f;

    // 6. Integrate smoothed target
    follow += vel * dt;
    if (P.wraps) follow = wrapPi(follow);

    // 7. Feed-forward motor placement: cam = handle + motor
    float motorCmd = follow - handle;
    if (P.wraps) motorCmd = wrapPi(motorCmd);

    // 8. Respect mechanical limits; if we hit one, drag follow along
    //    so the error doesn't wind up (prevents a snap when releasing)
    float clamped = clampf(motorCmd, P.limitMin, P.limitMax);
    if (clamped != motorCmd) {
      follow = handle + clamped;
      if (P.wraps) follow = wrapPi(follow);
      vel = 0.0f;
    }
    return clamped;
  }

  void recenter(float handleAngle) { follow = handleAngle; vel = 0.0f; }
  float followAngle() const { return follow; }
  float rate() const { return vel; }

  FollowParams P;
private:
  float follow = 0.0f;
  float vel    = 0.0f;
};

// ---------------------------------------------------------------------------
//  Hardware (adapt pins/sensors to your board)
// ---------------------------------------------------------------------------
BLDCMotor panMotor  (7);                    // pole pairs
BLDCMotor tiltMotor (7);
BLDCDriver3PWM panDrv (9, 10, 11, 8);
BLDCDriver3PWM tiltDrv(5,  6,  3, 7);
MagneticSensorSPI panEnc (AS5048_SPI, 10);
MagneticSensorSPI tiltEnc(AS5048_SPI,  4);

// Replace with your IMU fusion (Mahony/Madgwick/BMI270 + quaternion → Euler)
extern bool  imuUpdate();
extern float imuYaw();     // camera world yaw,   rad
extern float imuPitch();   // camera world pitch, rad

// ---------------------------------------------------------------------------
//  Tuning: pocket-gimbal "cinematic" defaults
// ---------------------------------------------------------------------------
FollowAxis pan({
  .deadband = radians(3.0f),
  .knee     = radians(4.0f),
  .tau      = 0.45f,
  .maxRate  = radians(120.0f),
  .maxAccel = radians(400.0f),
  .limitMin = -1e9f, .limitMax = 1e9f,      // slip-ring / continuous
  .wraps    = true
});

FollowAxis tilt({
  .deadband = radians(4.0f),                // tilt is more distracting → wider
  .knee     = radians(5.0f),
  .tau      = 0.60f,
  .maxRate  = radians(80.0f),
  .maxAccel = radians(300.0f),
  .limitMin = radians(-80.0f), .limitMax = radians(45.0f),
  .wraps    = false
});

// ---------------------------------------------------------------------------
void setupMotor(BLDCMotor& m, BLDCDriver3PWM& d, MagneticSensorSPI& s) {
  s.init();
  m.linkSensor(&s);
  d.voltage_power_supply = 8.0f;            // 2S
  d.init();
  m.linkDriver(&d);

  m.foc_modulation    = FOCModulationType::SpaceVectorPWM;
  m.controller        = MotionControlType::angle;
  m.torque_controller = TorqueControlType::voltage;

  m.PID_velocity.P = 0.15f;
  m.PID_velocity.I = 4.0f;
  m.PID_velocity.D = 0.0f;
  m.PID_velocity.output_ramp = 1000.0f;
  m.LPF_velocity.Tf = 0.01f;
  m.P_angle.P       = 18.0f;
  m.velocity_limit  = 15.0f;                // inner limit > follow maxRate
  m.voltage_limit   = 4.0f;

  m.init();
  m.initFOC();
}

static const uint32_t FOLLOW_PERIO