# SimpleFOC Smooth Follow Mode with Exponential Rate Easing and Deadband (Cycle 20)

This is a complete follow-mode controller for a handheld gimbal axis. It mimics the DJI Osmo Pocket feel:

- **Small handle jitters are ignored.** A deadband window with hysteresis absorbs them.
- **Deliberate pans engage smoothly.** The follow rate grows exponentially with how far the handle is outside the window.
- **Starts and stops are cinematic.** The rate is eased through a first-order filter and an acceleration cap, so there are no jerky stops.
- **The camera re-centres.** Once triggered, it keeps following until it is nearly aligned, then settles.

## Architecture

```
IMU (camera world angle) ──┐
                           ├─► handle_world = camera_world − joint_angle
Encoder (joint angle) ─────┘
                                   │
           ┌───────────────────────▼────────────────────────┐
           │ FollowAxis                                      │
           │  error = wrap(handle_world − target_world)      │
           │  deadband + hysteresis  → engaged?              │
           │  rate_cmd = maxRate·(1 − e^(−excess/k))         │
           │  rate = rate + (rate_cmd − rate)(1 − e^(−dt/τ)) │
           │  accel limit → target_world += rate·dt          │
           └───────────────────────┬────────────────────────┘
                                   ▼
   joint_cmd = target_world − handle_world  →  motor.move()  (angle mode)
```

## Code

```cpp
// =====================================================================
//  SimpleFOC Smooth Follow Mode – Exponential Rate Easing + Deadband
//  Osmo-Pocket-style handheld gimbal, Cycle 20
// =====================================================================
#include <SimpleFOC.h>
#include <math.h>

// ---------------------------------------------------------------------
//  Utility
// ---------------------------------------------------------------------
static inline float wrapPi(float a) {
  while (a >  _PI) a -= _2PI;
  while (a < -_PI) a += _2PI;
  return a;
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// ---------------------------------------------------------------------
//  Follow parameters (per axis)
// ---------------------------------------------------------------------
struct FollowParams {
  float deadbandEnter;   // rad – error needed to START following
  float deadbandExit;    // rad – error at which following STOPS (< enter)
  float maxRate;         // rad/s – asymptotic max follow speed
  float rateScale;       // rad – exponential knee (smaller = more aggressive)
  float easeTau;         // s – time constant for rate smoothing
  float maxAccel;        // rad/s^2 – hard cap on rate change
  float settleRate;      // rad/s – below this + inside exit window = settled
  float softLimit;       // rad – max joint deflection (0 = unlimited / yaw)
};

// Osmo-like defaults
const FollowParams YAW_PARAMS   = { 0.087f, 0.010f, 2.5f, 0.35f, 0.18f, 6.0f, 0.02f, 0.0f   };
const FollowParams PITCH_PARAMS = { 0.120f, 0.015f, 1.8f, 0.30f, 0.22f, 5.0f, 0.02f, 1.40f };

// ---------------------------------------------------------------------
//  FollowAxis – the core algorithm
// ---------------------------------------------------------------------
class FollowAxis {
public:
  explicit FollowAxis(const FollowParams& p) : P(p) {}

  void reset(float handleWorld) {
    target  = handleWorld;
    rate    = 0.0f;
    engaged = false;
  }

  // Returns the camera world-frame target angle.
  float update(float handleWorld, float dt) {
    if (dt <= 0.0f || dt > 0.1f) return target;      // guard against bad dt

    float err  = wrapPi(handleWorld - target);
    float aErr = fabsf(err);

    // ---- 1. Deadband window with hysteresis -------------------------
    if (!engaged && aErr > P.deadbandEnter) engaged = true;
    if ( engaged && aErr < P.deadbandExit && fabsf(rate) < P.settleRate)
      engaged = false;

    // ---- 2. Exponential rate curve ----------------------------------
    // While engaged, the trigger window collapses to the exit window,
    // so the camera re-centres fully instead of stopping at the edge.
    float rateCmd = 0.0f;
    if (engaged) {
      float excess = fmaxf(aErr - P.deadbandExit, 0.0f);
      float mag    = P.maxRate * (1.0f - expf(-excess / P.rateScale));
      // Never command more than can be covered this tick (no overshoot)
      mag     = fminf(mag, excess / dt);
      rateCmd = copysignf(mag, err);
    }

    // ---- 3. Exponential easing of the rate (first-order filter) -----
    float alpha   = 1.0f - expf(-dt / P.easeTau);
    float newRate = rate + (rateCmd - rate) * alpha;

    // ---- 4. Acceleration limit (keeps starts/stops buttery) ---------
    float dMax = P.maxAccel * dt;
    rate = rate + clampf(newRate - rate, -dMax, dMax);

    // ---- 5. Integrate the target ------------------------------------
    target = wrapPi(target + rate * dt);

    // ---- 6. Soft mechanical limit (pitch/roll) ----------------------
    if (P.softLimit > 0.0f) {
      float rel = wrapPi(target - handleWorld);
      if (fabsf(rel) > P.softLimit) {
        target = wrapPi(handleWorld + copysignf(P.softLimit, rel));
        rate   = 0.0f;                                // hit the wall: kill motion
      }
    }
    return target;
  }

  bool  isEngaged() const { return engaged; }
  float getRate()   const { return rate; }

  FollowParams P;
private:
  float target  = 0.0f;
  float rate    = 0.0f;
  bool  engaged = false;
};

// ---------------------------------------------------------------------
//  Hardware: one axis shown (yaw); replicate for pitch/roll
// ---------------------------------------------------------------------
BLDCMotor       motor  = BLDCMotor(7);                 // pole pairs
BLDCDriver3PWM  driver = BLDCDriver3PWM(9, 5, 6, 8);
MagneticSensorSPI encoder = MagneticSensorSPI(AS5048_SPI, 10);

FollowAxis yawFollow(YAW_PARAMS);

// Replace with your IMU fusion (Mahony/Madgwick) – camera yaw in world frame
extern float imuCameraYaw();
extern void  imuUpdate();

float    jointZero = 0.0f;                             // encoder offset at centre
uint32_t lastUs    = 0;

void setup() {
  Serial.begin(115200);

  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 12;
  driver.init();
  motor.linkDriver(&driver);

  motor.controller           = MotionControlType::angle;
  motor.foc_modulation       = FOCModulationType::SpaceVectorPWM;
  motor.voltage_limit        = 6;
  motor.velocity_limit       = 20;
  motor.PID_velocity.P       = 0.15f;
  motor.PID_velocity.I       = 8.0f;
  motor.LPF_velocity.Tf      = 0.005f;
  motor.P_angle.P            = 25.0f;

  motor.init();
  motor.initFOC();

  jointZero = motor.shaft_angle;
  float joint  = motor.shaft_angle - jointZero;
  yawFollow.reset(wrapPi(imuCameraYaw() - joint));

  lastUs = micros();
}

void loop() {
  motor.loopFOC();                                     // run as fast as possible

  // Outer follow loop at ~500 Hz
  uint32_t now = micros();
  float dt = (now - lastUs) * 1e-6f;
  if (dt < 0.002f) return;
  lastUs = now;

  imuUpdate();

  float joint       = motor.shaft_angle - jointZero;
  float cameraWorld = imuCameraYaw();
  float handleWorld = wrapPi(cameraWorld - joint);     // where the user points

  float targetWorld = yawFollow.update(handleWorld, dt);

  // Joint command that places the camera at targetWorld
  float jointCmd = wrapPi(targetWorld - handleWorld);
  motor.move(jointZero + motor.shaft_angle - joint + jointCmd - (motor.shaft_angle - jointZero - joint));
  // ^ simplifies to jointZero + jointCmd; written out for clarity of frames

  // Debug at ~20 Hz
  static uint8_t dbg = 0;
  if (++dbg >= 25) {
    dbg = 0;
    Serial.print(handleWorld, 3); Serial.print('\t');
    Serial.print(targetWorld, 3); Serial.print('\t');
    Serial.print(yawFollow.getRate(), 3); Serial.print('\t');
    Serial.println(yawFollow.isEngaged());
  }
}
```

The `motor.move()`