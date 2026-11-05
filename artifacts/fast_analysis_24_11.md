# SimpleFOC Smooth Follow Mode: Exponential Rate Easing + Deadband Window

I'm not sure what "Cycle 24" refers to (a revision tag, a sprint, or a hardware spin), so I've treated it as a version label and noted it in the header. Tell me if it implies specific parameters or hardware.

## How it works

On a handheld gimbal like the Osmo Pocket, **follow mode** means the camera stays stabilized against small hand shake but smoothly catches up when you deliberately turn the handle. The algorithm has four parts:

1. **Estimate handle heading.** `handle_yaw = camera_yaw(IMU) − joint_angle(encoder)`.
2. **Deadband window with hysteresis.**
   - While the handle stays within ±`deadband_enter` of the camera setpoint, the setpoint does not move. Hand jitter is rejected.
   - Once the handle exceeds that window, follow engages and continues until the error falls below `deadband_exit` (a smaller value). This prevents chatter at the window edge.
   - The window is *soft*: the effective error is `|e| − window`, so motion ramps up from zero instead of snapping.
3. **Exponential rate easing.**
   - The desired follow rate is `e_eff / τ`, clamped to `max_rate`. This gives first-order exponential convergence.
   - The actual rate is itself eased toward that desired rate with time constant `τ_rate`. This makes starts and stops S-curved, which is the "cinematic" feel.
   - The easing coefficients use `1 − exp(−dt/τ)`, so the behavior stays the same if the loop rate jitters.
4. **Motor command.** `joint_target = setpoint − handle_yaw`. SimpleFOC's angle loop drives the joint so that `camera_yaw → setpoint`.

## Code

```cpp
// =====================================================================
//  SimpleFOC Smooth Follow Mode  —  Cycle 24
//  Exponential rate easing + hysteretic soft deadband window
//  Target: handheld 3-axis gimbal (Osmo Pocket style), pan/tilt follow,
//          roll locked. Shown for pan; tilt uses the same FollowAxis.
// =====================================================================
#include <Arduino.h>
#include <SimpleFOC.h>
#include <math.h>

// ---------------- Hardware (adapt to your board) ---------------------
BLDCMotor        motor = BLDCMotor(7);                 // pole pairs
BLDCDriver3PWM   driver = BLDCDriver3PWM(PA8, PA9, PA10, PB12);
MagneticSensorSPI encoder = MagneticSensorSPI(AS5048_SPI, PA4);

// ---- IMU stub: return camera yaw (rad) from your AHRS (Mahony/Madgwick)
float imuCameraYaw();          // implement with your IMU driver
float imuCameraYawRate();      // rad/s, gyro z (optional, for damping)

// ---------------- Utilities -----------------------------------------
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}
static inline float sgn(float x) { return (x > 0) - (x < 0); }
static inline float easeAlpha(float dt, float tau) {
  return (tau <= 0.0f) ? 1.0f : 1.0f - expf(-dt / tau);
}

// ---------------- Follow axis ---------------------------------------
struct FollowParams {
  float deadband_enter = 0.0873f;  // 5.0 deg  : start following beyond this
  float deadband_exit  = 0.0087f;  // 0.5 deg  : stop following inside this
  float tau_follow     = 0.45f;    // s : position catch-up time constant
  float tau_rate       = 0.18f;    // s : rate easing (S-curve start/stop)
  float max_rate       = 2.6f;     // rad/s (~150 deg/s) follow speed cap
  float max_accel      = 8.0f;     // rad/s^2 hard accel clamp (safety)
  float joint_limit    = 2.6f;     // rad, mechanical stop margin (0 = none)
};

class FollowAxis {
public:
  FollowParams p;
  float setpoint = 0.0f;   // desired camera heading (world frame), rad
  float rate     = 0.0f;   // current eased follow rate, rad/s
  bool  following = false;

  void reset(float camera_yaw) {
    setpoint = camera_yaw; rate = 0.0f; following = false;
  }

  // handle_yaw: world heading of the handle (rad). Returns new setpoint.
  float update(float handle_yaw, float dt) {
    if (dt <= 0.0f || dt > 0.05f) dt = 0.001f;           // guard glitches

    float e  = wrapPi(handle_yaw - setpoint);
    float ae = fabsf(e);

    // --- Hysteretic deadband window ---
    if (!following && ae > p.deadband_enter) following = true;
    if ( following && ae < p.deadband_exit && fabsf(rate) < 0.02f)
      following = false;

    // --- Soft window: effective error ramps from 0 at the edge ---
    float e_eff = 0.0f;
    if (following) {
      // While engaged, track all the way in (down to exit window),
      // so the camera fully re-centres behind the handle.
      float win = p.deadband_exit;
      e_eff = (ae > win) ? sgn(e) * (ae - win) : 0.0f;
    }

    // --- Desired rate: first-order exponential approach, capped ---
    float rate_des = e_eff / p.tau_follow;
    rate_des = constrain(rate_des, -p.max_rate, p.max_rate);

    // --- Exponential rate easing (S-curve), plus accel clamp ---
    float a = easeAlpha(dt, p.tau_rate);
    float d_rate = (rate_des - rate) * a;
    float d_max  = p.max_accel * dt;
    d_rate = constrain(d_rate, -d_max, d_max);
    rate += d_rate;

    // --- Integrate; never overshoot the handle ---
    float step = rate * dt;
    if (following && fabsf(step) > fabsf(e)) { step = e; rate = 0.0f; }
    setpoint = wrapPi(setpoint + step);
    return setpoint;
  }
};

// ---------------- Globals -------------------------------------------
FollowAxis pan;
float joint_zero = 0.0f;     // encoder reading when camera faces forward
uint32_t t_prev_us = 0;
uint32_t t_follow_us = 0;
const uint32_t FOLLOW_PERIOD_US = 1000;   // 1 kHz follow loop

// ---------------- Setup ---------------------------------------------
void setup() {
  Serial.begin(115200);

  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 8.4f;   // 2S Li-ion
  driver.init();
  motor.linkDriver(&driver);

  motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
  motor.controller     = MotionControlType::angle;
  motor.torque_controller = TorqueControlType::voltage;

  // Gimbal motors: high-resistance, low-speed -> soft, well-damped loops
  motor.voltage_limit   = 4.0f;
  motor.velocity_limit  = 6.0f;
  motor.P_angle.P       = 18.0f;
  motor.PID_velocity.P  = 0.25f;
  motor.PID_velocity.I  = 4.0f;
  motor.PID_velocity.D  = 0.0f;
  motor.PID_velocity.output_ramp = 400.0f;
  motor.LPF_velocity.Tf = 0.008f;

  motor.init();
  motor.initFOC();

  joint_zero = motor.shaft_angle;
  pan.reset(imuCameraYaw());
  t_prev_us = micros();
}

// ---------------- Loop ----------------------------------------------
void loop() {
  motor.loopFOC();                         // run as fast as possible

  uint32_t now = micros();
  if (now - t_follow_us >= FOLLOW_PERIOD_US) {
    float dt = (now - t_prev_us) * 1e-6f;
    t_prev_us = t_follow_us = now;

    float joint      = wrapPi(motor.shaft_angle - joint_zero);
    float cam_yaw    = imuCameraYaw();
    float handle_yaw = wrapPi(cam_yaw - joint);

    