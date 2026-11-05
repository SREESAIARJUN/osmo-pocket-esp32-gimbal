# SimpleFOC Smooth Follow Mode: Exponential Easing + Soft Deadband

I don't have context for what "Cycle 12" refers to in your project, so this is a complete, self-contained implementation. If it is meant to build on earlier iterations, tell me which parameters or behavior to carry over.

## Architecture

```
 IMU (on camera) ──► camera world angle ─┐
                                         ├─► handle angle = cam − joint
 Encoder (motor) ──► joint angle ────────┘            │
                                                      ▼
                            ┌─────────── FollowAxis ───────────┐
                            │ 1. wrapped error (handle − sp)   │
                            │ 2. soft deadband window          │
                            │ 3. exponential easing (τ)        │
                            │ 4. velocity + accel limiting     │
                            └──────────────┬───────────────────┘
                                           ▼ setpoint (world)
                 joint target = setpoint − handle angle
                                           ▼
                    SimpleFOC motor.move() (angle mode)
```

The camera is commanded to a **world-frame setpoint**. The handle drags that setpoint along lazily:

- **Inside the deadband** the setpoint holds still, so walking jitter and hand tremor are ignored.
- **Outside the deadband** the setpoint eases toward the handle exponentially.
- **The deadband edge is blended**, so the camera never visibly "catches" when you cross it.

## Code (Arduino / SimpleFOC 2.3+)

```cpp
#include <SimpleFOC.h>
#include <math.h>

// ============================================================================
//  Utilities
// ============================================================================
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float smoothstep01(float x) {
  x = clampf(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

// ============================================================================
//  FollowAxis: smooth follow with exponential easing + soft deadband window
// ============================================================================
struct FollowConfig {
  float deadband;        // rad, half-width of the "ignore" window
  float blendWidth;      // rad, soft transition zone beyond the deadband
  float tau;             // s, exponential easing time constant
  float maxRate;         // rad/s, cap on setpoint velocity
  float maxAccel;        // rad/s^2, cap on setpoint acceleration
  float minLimit;        // rad, world-frame clamp (pitch); use -INF/INF for yaw
  float maxLimit;
  bool  wrap;            // true for continuous yaw
};

class FollowAxis {
public:
  explicit FollowAxis(const FollowConfig& c) : cfg(c) {}

  void reset(float handleAngle) {
    setpoint = handleAngle;
    rate = 0.0f;
    initialized = true;
  }

  // handleAngle: world-frame handle angle (rad). dt in seconds.
  float update(float handleAngle, float dt) {
    if (!initialized) reset(handleAngle);
    if (dt <= 0.0f || dt > 0.1f) return setpoint;    // guard against stalls

    // 1. Error between where the handle points and where the camera is held
    float err = handleAngle - setpoint;
    if (cfg.wrap) err = wrapPi(err);

    // 2. Soft deadband: remove the window, blend smoothly over blendWidth
    float mag  = fabsf(err);
    float over = mag - cfg.deadband;
    float effErr = 0.0f;
    if (over > 0.0f) {
      float w = (cfg.blendWidth > 1e-6f) ? smoothstep01(over / cfg.blendWidth) : 1.0f;
      effErr = copysignf(over * w, err);
    }

    // 3. Exponential easing: dt-independent first-order approach
    //    alpha = 1 - e^(-dt/tau), so the response is identical at any loop rate
    float alpha = 1.0f - expf(-dt / cfg.tau);
    float desiredRate = (effErr * alpha) / dt;

    // 4. Rate and acceleration limiting (removes start/stop jerk)
    desiredRate = clampf(desiredRate, -cfg.maxRate, cfg.maxRate);
    float dv = clampf(desiredRate - rate, -cfg.maxAccel * dt, cfg.maxAccel * dt);
    rate += dv;

    setpoint += rate * dt;
    if (cfg.wrap) setpoint = wrapPi(setpoint);
    else {
      float s = clampf(setpoint, cfg.minLimit, cfg.maxLimit);
      if (s != setpoint) { setpoint = s; rate = 0.0f; }
    }
    return setpoint;
  }

  // "Recenter" (e.g. triple-tap): let the setpoint snap-ease back to the handle
  void recenter() { recentering = true; }

  float getSetpoint() const { return setpoint; }
  float getRate()     const { return rate; }

  FollowConfig cfg;
private:
  float setpoint = 0.0f;
  float rate = 0.0f;
  bool  initialized = false;
  bool  recentering = false;
};

// ============================================================================
//  Hardware: adjust pins/pole pairs for your board
// ============================================================================
BLDCMotor   yawMotor(7),   pitchMotor(7);
BLDCDriver3PWM yawDrv(9, 5, 6, 8), pitchDrv(10, 11, 3, 8);
MagneticSensorI2C yawEnc(AS5600_I2C), pitchEnc(AS5600_I2C);  // use 2 buses/mux in practice

// Joint zero offsets (camera level and centered), found at calibration
const float YAW_JOINT_ZERO   = 0.0f;
const float PITCH_JOINT_ZERO = 0.0f;

// ---- Follow tuning (Osmo Pocket-like feel) ----
FollowAxis yawFollow({
  /*deadband*/   radians(3.0f),
  /*blendWidth*/ radians(4.0f),
  /*tau*/        0.45f,
  /*maxRate*/    radians(120.0f),
  /*maxAccel*/   radians(400.0f),
  /*min/max*/   -INFINITY, INFINITY,
  /*wrap*/       true
});

FollowAxis pitchFollow({
  radians(4.0f), radians(5.0f), 0.60f,
  radians(90.0f), radians(300.0f),
  radians(-80.0f), radians(45.0f),   // tilt range
  false
});

// ============================================================================
//  IMU stub: replace with your fused AHRS (Mahony/Madgwick/BMI270 + FIFO)
//  Must return CAMERA world yaw/pitch in radians.
// ============================================================================
struct Attitude { float yaw, pitch, roll; };
Attitude readCameraAttitude();   // implement for your IMU

// ============================================================================
void setup() {
  Serial.begin(115200);

  yawEnc.init();   pitchEnc.init();
  yawMotor.linkSensor(&yawEnc);       pitchMotor.linkSensor(&pitchEnc);
  yawDrv.voltage_power_supply = 8.4;  pitchDrv.voltage_power_supply = 8.4;
  yawDrv.init();   pitchDrv.init();
  yawMotor.linkDriver(&yawDrv);       pitchMotor.linkDriver(&pitchDrv);

  for (BLDCMotor* m : { &yawMotor, &pitchMotor }) {
    m->controller = MotionControlType::angle;
    m->torque_controller = TorqueControlType::voltage;
    m->foc_modulation = FOCModulationType::SpaceVectorPWM;
    m->voltage_limit = 4.0f;
    m->velocity_limit = 20.0f;
    m->PID_velocity.P = 0.15f;  m->PID_velocity.I = 4.0f;  m->PID_velocity.D = 0.0f;
    m->PID_velocity.output_ramp = 1000.0f;
    m->LPF_velocity.Tf = 0.01f;
    m->P_angle.P = 18.0f;
    m->init();
    m->initFOC();
  }

  // Initialize follow setpoints to wherever the handle currently points
  Attitude cam = readCameraAttitude();
  float yawJ   = yawMotor.shaft_angle   - YAW_JOINT_ZERO;
  float pitchJ = pitchMotor.shaft_angle - PITCH_JOINT_ZERO;
  yawFollow.reset(wrapPi(cam.yaw - yawJ));
  pitchFollow.reset(cam.pitch - pitchJ);
}

// ============================================================================
void loop() {
  // FOC inner loop: run as fast as possible
  yawMotor.loopFOC();
  pitchMotor.loopFOC();

  // Outer follow loop: about 500 Hz
  static uint32_t last = micros();
  uint32_t now = micros();
  float dt = (now