# SimpleFOC Smooth Follow Mode: Exponential Easing + Deadband Window

This is a pan/tilt follow controller for a handheld gimbal like the Osmo Pocket. It is built on **SimpleFOC** and produces cinematic camera motion:

- **Deadband window with hysteresis.** Small handle jitters are ignored. Once the camera starts following, it keeps moving smoothly until it is nearly centered, so it doesn't stutter at the edge of the deadband.
- **Exponential rate easing.** The camera's world angle approaches the handle angle on a first-order exponential curve. The update is frame-rate independent, using `1 - exp(-dt/τ)`.
- **Velocity and acceleration limits.** Pans start and stop smoothly instead of snapping.
- **Stabilization.** The motor angle is always `camera_world_target − handle_angle`, so handle shake is cancelled while the follow motion stays smooth.

```cpp
// ============================================================================
//  Smooth Follow Mode – SimpleFOC gimbal axis controller  (Cycle 15)
//  Exponential rate easing + hysteretic soft deadband window
// ============================================================================
#include <SimpleFOC.h>
#include <math.h>

// ---------------- Hardware (adapt to your board) ----------------------------
BLDCMotor        panMotor  = BLDCMotor(7);                 // pole pairs
BLDCDriver3PWM   panDriver = BLDCDriver3PWM(9, 5, 6, 8);
MagneticSensorSPI panSensor = MagneticSensorSPI(AS5048_SPI, 10);

// Replace with your IMU fusion output (radians, world frame, unwrapped OK)
extern float readHandleYaw();      // e.g. from Mahony/Madgwick on handle IMU

// ---------------- Helpers ---------------------------------------------------
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float signf(float x) { return (x > 0) - (x < 0); }

// ---------------- Follow axis -----------------------------------------------
struct FollowParams {
  float deadbandEnter = 0.087f;  // rad (~5°): error needed to START following
  float deadbandExit  = 0.010f;  // rad (~0.6°): stop following below this
  float softKnee      = 0.035f;  // rad: blend width so motion ramps in gently
  float tau           = 0.45f;   // s: exponential easing time constant
  float maxVel        = 2.0f;    // rad/s: pan speed ceiling
  float maxAccel      = 4.0f;    // rad/s^2: start/stop smoothness
  float mechLimit     = 2.6f;    // rad: motor travel limit (cable/hard stop)
};

class SmoothFollowAxis {
public:
  FollowParams p;

  void reset(float handleAngle, float motorAngle) {
    camTarget_ = handleAngle + motorAngle;   // current camera world angle
    vel_       = 0.0f;
    engaged_   = false;
  }

  // Returns the desired MOTOR angle (relative to handle)
  float update(float handleAngle, float dt) {
    if (dt <= 0.0f || dt > 0.1f) dt = 0.001f;   // guard against stalls

    float err  = wrapPi(handleAngle - camTarget_);
    float aerr = fabsf(err);

    // ---- Hysteretic deadband window ----
    if (!engaged_ && aerr > p.deadbandEnter) engaged_ = true;
    if ( engaged_ && aerr < p.deadbandExit && fabsf(vel_) < 0.02f) engaged_ = false;

    float effErr = 0.0f;
    if (engaged_) {
      // Soft deadband: subtract the exit window, then smoothstep the knee
      float excess = fmaxf(aerr - p.deadbandExit, 0.0f);
      float k = clampf(excess / p.softKnee, 0.0f, 1.0f);
      k = k * k * (3.0f - 2.0f * k);             // smoothstep
      effErr = signf(err) * excess * k;
    }

    // ---- Exponential rate easing (frame-rate independent) ----
    float alpha   = 1.0f - expf(-dt / p.tau);
    float desVel  = (effErr * alpha) / dt;       // velocity that closes alpha of error
    desVel        = clampf(desVel, -p.maxVel, p.maxVel);

    // ---- Acceleration slew (ease-in / ease-out) ----
    float dvMax = p.maxAccel * dt;
    vel_ += clampf(desVel - vel_, -dvMax, dvMax);

    camTarget_ = wrapPi(camTarget_ + vel_ * dt);

    // ---- Stabilization: motor cancels handle motion ----
    float motorCmd = wrapPi(camTarget_ - handleAngle);

    // Mechanical limit: if hit, drag camera target along with the handle
    if (fabsf(motorCmd) > p.mechLimit) {
      motorCmd   = signf(motorCmd) * p.mechLimit;
      camTarget_ = wrapPi(handleAngle + motorCmd);
      vel_       = 0.0f;
    }
    return motorCmd;
  }

  // Osmo-style "recenter" (double-tap trigger)
  void recenter(float handleAngle) { camTarget_ = handleAngle; vel_ = 0; engaged_ = false; }

  bool  engaged()  const { return engaged_; }
  float velocity() const { return vel_; }

private:
  float camTarget_ = 0.0f;
  float vel_       = 0.0f;
  bool  engaged_   = false;
};

SmoothFollowAxis panFollow;

// ---------------- Setup -----------------------------------------------------
void setup() {
  Serial.begin(115200);

  panSensor.init();
  panMotor.linkSensor(&panSensor);

  panDriver.voltage_power_supply = 8.4f;   // 2S Li-ion
  panDriver.init();
  panMotor.linkDriver(&panDriver);

  panMotor.controller       = MotionControlType::angle;
  panMotor.torque_controller = TorqueControlType::voltage;
  panMotor.foc_modulation   = FOCModulationType::SpaceVectorPWM;

  // Inner loops: stiff for stabilization; smoothness comes from follow layer
  panMotor.PID_velocity.P = 0.25f;
  panMotor.PID_velocity.I = 8.0f;
  panMotor.PID_velocity.D = 0.0f;
  panMotor.LPF_velocity.Tf = 0.008f;
  panMotor.P_angle.P      = 18.0f;
  panMotor.velocity_limit = 12.0f;         // must exceed handle shake rates
  panMotor.voltage_limit  = 4.0f;

  panMotor.init();
  panMotor.initFOC();

  // Zero motor angle = camera aligned with handle
  panFollow.reset(readHandleYaw(), panMotor.shaft_angle);
}

// ---------------- Loop ------------------------------------------------------
void loop() {
  static uint32_t lastUs = micros();
  static uint8_t  div    = 0;

  panMotor.loopFOC();                       // run as fast as possible

  // Follow layer at ~1 kHz
  if (++div >= 4) {
    div = 0;
    uint32_t now = micros();
    float dt = (now - lastUs) * 1e-6f;
    lastUs = now;

    float cmd = panFollow.update(readHandleYaw(), dt);
    panMotor.move(cmd);
  } else {
    panMotor.move();                        // keep last target
  }
}
```

## How the stages fit together

| Stage | Purpose | Key parameter |
|---|---|---|
| Hysteretic deadband | Ignores grip tremor, then commits to a move | `deadbandEnter` > `deadbandExit` |
| Soft knee (smoothstep) | Removes the velocity step when following engages | `softKnee` |
| Exponential easing | Natural "lagging" follow feel | `tau` |
| Velocity clamp | Caps speed on fast whip pans | `maxVel` |
| Accel slew | Smooth ease-in and ease-out | `maxAccel` |
| Stabilization | Motor = target − handle | handled by the inner angle loop |

## Tuning presets (Osmo-like feel)

| Mode | deadbandEnter | tau | maxVel | maxAccel |
|---|---|---|---|---|
| Slow / cinematic | 8° | 0.8 s | 1.0 | 2.0 |
| Normal | 5° | 0.45 s | 2.0 | 4.0 |
| Fast / sport | 2° | 0.15 s | 5.0 | 15.0 |

## Tips

- Use the same structure for the tilt axis. Lock roll, meaning no follow on that axis, so the horizon stays level.
- Feed `readHandleYaw()` from a fused IMU that runs at 500 Hz or more. Gyro noise on that input goes straight into the stabilization path.
- If the camera "hunts" around the end of a move, increase `deadbandExit` slightly or lower `maxAccel`.
