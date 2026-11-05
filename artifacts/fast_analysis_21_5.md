# SimpleFOC Smooth Follow Mode: Exponential Easing with Deadband (Cycle 21)

This design is for a handheld 3-axis gimbal like the Osmo Pocket. The camera holds its world-frame heading. When the operator turns the handle past a deadband, the heading target eases toward the handle's heading with an exponential rate profile. The motion is frame-rate independent, rate-limited and acceleration-limited.

## Concept

```
handle yaw ψh ──► [wrap error] ──► [soft deadband + hysteresis] ──► [exp easing → rate]
                                                                        │
                                              [rate limit] ◄────────────┘
                                                   │
                                              [accel limit]
                                                   │
                                     ψcam_target += ω·dt
                                                   │
                    joint target = wrap(ψcam_target − ψh) ──► SimpleFOC angle loop
```

Key design points:

- **Soft deadband.** The deadband width is subtracted from the error rather than used as a hard gate. Output therefore starts from zero at the window edge, with no step.
- **Hysteresis.** Following engages above `db_enter` and only releases below `db_exit`. This prevents chatter at the boundary.
- **Exponential easing.** The commanded rate is `ω = e_eff / τ`. Because the rate is proportional to the remaining error, the motion is a true exponential approach rather than a fixed per-update blend. A per-update blend changes behaviour when the loop rate jitters.
- **Rate and acceleration limits.** These give cinematic starts and stops. The rate is also clamped so it never overshoots the remaining error in one step.
- **Settle exit.** Once the camera has caught up, the follower returns to "hold" and the deadband re-arms.

## Code

### `SmoothFollow.h`

```cpp
#pragma once
#include <Arduino.h>
#include <math.h>

static inline float wrapPi(float a) {
  a = fmodf(a + PI, 2.0f * PI);
  if (a < 0) a += 2.0f * PI;
  return a - PI;
}

struct FollowParams {
  float db_enter   = radians(6.0f);   // start following beyond this error
  float db_exit    = radians(1.0f);   // stop following once error is below this
  float tau        = 0.35f;           // easing time constant [s]
  float max_rate   = radians(120.0f); // [rad/s]
  float max_accel  = radians(400.0f); // [rad/s^2]
  float settle_rate= radians(0.5f);   // rate considered "stopped"
};

class SmoothFollowAxis {
public:
  FollowParams p;

  void reset(float handle_angle) {
    target_ = handle_angle;
    rate_ = 0;
    following_ = false;
  }

  // handle_angle: world-frame handle angle (rad). dt in seconds.
  // returns world-frame camera target angle.
  float update(float handle_angle, float dt) {
    if (dt <= 0 || dt > 0.1f) return target_;          // guard bad dt

    float err = wrapPi(handle_angle - target_);
    float aerr = fabsf(err);

    // --- hysteresis state machine ---
    if (!following_ && aerr > p.db_enter) following_ = true;
    if (following_ && aerr < p.db_exit && fabsf(rate_) < p.settle_rate)
      following_ = false;

    // --- soft deadband: continuous at the window edge ---
    float db = following_ ? p.db_exit : p.db_enter;
    float e_eff = 0.0f;
    if (aerr > db) e_eff = copysignf(aerr - db, err);

    // --- exponential easing: rate proportional to remaining error ---
    float rate_cmd = following_ ? e_eff / p.tau : 0.0f;

    // never overshoot the remaining effective error in a single step
    float max_step_rate = fabsf(e_eff) / dt;
    rate_cmd = constrain(rate_cmd, -max_step_rate, max_step_rate);

    // --- rate limit ---
    rate_cmd = constrain(rate_cmd, -p.max_rate, p.max_rate);

    // --- acceleration limit (smooth start/stop) ---
    float dv = constrain(rate_cmd - rate_, -p.max_accel * dt, p.max_accel * dt);
    rate_ += dv;

    target_ = wrapPi(target_ + rate_ * dt);
    return target_;
  }

  bool  following() const { return following_; }
  float rate()      const { return rate_; }
  float target()    const { return target_; }

private:
  float target_ = 0, rate_ = 0;
  bool following_ = false;
};
```

### `main.cpp` (SimpleFOC integration, yaw axis)

```cpp
#include <SimpleFOC.h>
#include "SmoothFollow.h"

// --- hardware (adjust to your board) ---
BLDCMotor motor(7);                         // pole pairs (gimbal motor)
BLDCDriver3PWM driver(9, 5, 6, 8);
MagneticSensorSPI encoder(AS5048_SPI, 10);

SmoothFollowAxis yawFollow;

// Provided by your IMU fusion (Mahony/Madgwick), world frame, radians
extern float imuHandleYaw();   // handle IMU, or camera IMU yaw minus joint angle

uint32_t lastUs;
const uint32_t FOLLOW_PERIOD_US = 2000;     // 500 Hz follow loop

void setup() {
  Serial.begin(115200);
  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 8.4;        // 2S
  driver.init();
  motor.linkDriver(&driver);

  motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
  motor.controller = MotionControlType::angle;
  motor.torque_controller = TorqueControlType::voltage;

  // gimbal-motor tuning: soft, low-noise
  motor.voltage_limit = 4.0;
  motor.PID_velocity.P = 0.15;
  motor.PID_velocity.I = 2.0;
  motor.PID_velocity.D = 0;
  motor.PID_velocity.output_ramp = 300;
  motor.LPF_velocity.Tf = 0.01;
  motor.P_angle.P = 18;
  motor.velocity_limit = 8;                 // rad/s, above follow max_rate

  motor.init();
  motor.initFOC();

  // tune follow feel
  yawFollow.p.tau       = 0.35f;
  yawFollow.p.db_enter  = radians(6);
  yawFollow.p.db_exit   = radians(1);
  yawFollow.p.max_rate  = radians(120);
  yawFollow.p.max_accel = radians(400);
  yawFollow.reset(imuHandleYaw());

  lastUs = micros();
}

void loop() {
  motor.loopFOC();                          // run as fast as possible

  uint32_t now = micros();
  if (now - lastUs >= FOLLOW_PERIOD_US) {
    float dt = (now - lastUs) * 1e-6f;
    lastUs = now;

    float psiHandle = imuHandleYaw();
    float psiCam    = yawFollow.update(psiHandle, dt);

    // joint angle = camera world yaw relative to handle
    float jointTarget = wrapPi(psiCam - psiHandle);

    // unwrap relative to the current shaft angle to avoid 2π jumps
    float shaft = motor.shaft_angle;
    jointTarget = shaft + wrapPi(jointTarget - shaft);

    motor.target = jointTarget;
  }

  motor.move();
}
```

## Tuning Guide (Osmo-style feel)

| Parameter | Effect | Typical range |
|---|---|---|
| `tau` | Larger values give a lazier, more cinematic catch-up. | 0.2 s (fast) to 0.8 s (slow) |
| `db_enter` | How much handle jitter is ignored. | 3–10° |
| `db_exit` | How tightly the camera re-centres before holding. | 0.5–2° |
| `max_rate` | Top pan speed. | 60–180 °/s |
| `max_accel` | Start/stop softness. | 200–800 °/s² |

**Presets:**

- **Walk / vlog:** `tau=0.5`, `db_enter=8°`, `max_accel=250°/s²`. Ignores footsteps and pans softly.
- **Fast / sport:** `tau=0.15`, `db_enter=3°`, `max_rate=240°/s`.

## Notes

- **Pitch axis.** Run a second `SmoothFollowAxis` instance with a larger `db_enter`.
- **Roll axis.** Roll is usually locked to the horizon rather than followed.
- **Recentre / handle mode switch.** Call `reset()`, then let the follower ease in naturally. Do not snap the target.
- **Yaw drift.** Keep the IMU yaw drift-corrected (magnetometer or gyro bias estimation). Otherwise the follow target slowly creeps even with the handle still. The deadband hides small drift only until it accumulates past `db_enter`.