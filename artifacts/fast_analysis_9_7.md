# SimpleFOC Smooth Follow Mode: Pan Axis, Cycle 9

This is a follow-mode controller for a 3-axis handheld gimbal's pan (yaw) axis. When you turn the handle, the camera heading glides after it like an Osmo Pocket. Small hand jitter inside a deadband window is ignored. Larger turns trigger a follow whose angular rate eases in and out exponentially, so there are no visible starts or stops in the footage.

## Control Concept

```
 handle yaw ψh (IMU) ──┐
                       ▼
            e = wrap(ψh − ψc)            ψc = camera heading target (world)
                       │
          ┌────────────┴─────────────┐
          │ Deadband window w/ hyst. │  idle until |e| > DB_ENTER,
          │ + smooth (C1) edge       │  keep following until |e| < DB_EXIT
          └────────────┬─────────────┘
                       ▼
         ω_des = ω_max · tanh(K·e_eff / ω_max)    (soft rate saturation)
                       ▼
         ω += (ω_des − ω)·(1 − e^(−dt/τ))          (exponential rate easing,
                       ▼                            τ_accel ≠ τ_decel)
                 ψc += ω·dt
                       ▼
     motor target θm = wrap(ψc − ψh) + offset → SimpleFOC angle loop
```

## Code (Arduino / SimpleFOC ≥ 2.3)

```cpp
#include <Arduino.h>
#include <SimpleFOC.h>

// ---------------------------------------------------------------------------
// Hardware (adjust to your board)
// ---------------------------------------------------------------------------
BLDCMotor        motor  = BLDCMotor(7);                       // pole pairs
BLDCDriver3PWM   driver = BLDCDriver3PWM(9, 5, 6, 8);
MagneticSensorSPI sensor = MagneticSensorSPI(AS5048_SPI, 10);

// Provide this from your handle IMU fusion (Mahony/Madgwick), radians, world yaw.
extern float imuHandleYaw();

// ---------------------------------------------------------------------------
// Follow-mode tuning
// ---------------------------------------------------------------------------
struct FollowTuning {
  float dbEnter    = radians(4.0f);   // start following beyond this error
  float dbExit     = radians(0.6f);   // stop following once inside this
  float dbSoftness = radians(2.0f);   // width of smooth deadband edge
  float gain       = 2.2f;            // rad/s per rad of effective error
  float maxRate    = radians(120.0f); // pan speed ceiling
  float tauAccel   = 0.35f;           // s, easing time constant speeding up
  float tauDecel   = 0.20f;           // s, easing time constant slowing down
  float maxAccel   = radians(400.0f); // rad/s^2 hard jerk guard
  float mechOffset = 0.0f;            // motor angle at camera-forward
  float mechLimit  = radians(170.0f); // relative angle soft limit (cable/hardstop)
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static inline float wrapPi(float a) {
  while (a >  PI) a -= _2PI;
  while (a < -PI) a += _2PI;
  return a;
}

// C1-continuous soft deadband: 0 inside db, quadratic blend over `soft`,
// then linear (e - db - soft/2). No derivative kink -> no rate jerk at the edge.
static inline float softDeadband(float e, float db, float soft) {
  float a = fabsf(e);
  float s = (e >= 0.0f) ? 1.0f : -1.0f;
  if (a <= db) return 0.0f;
  float x = a - db;
  if (x < soft) return s * (x * x) / (2.0f * soft);
  return s * (x - 0.5f * soft);
}

// ---------------------------------------------------------------------------
// Follow controller
// ---------------------------------------------------------------------------
class SmoothFollow {
public:
  FollowTuning t;

  void reset(float handleYaw, float motorAngle) {
    // Initialize camera heading from current geometry so nothing jumps.
    psiCam   = wrapPi(handleYaw + (motorAngle - t.mechOffset));
    rate     = 0.0f;
    following = false;
  }

  // Returns motor angle target (rad, shaft frame).
  float update(float handleYaw, float dt) {
    if (dt <= 0.0f || dt > 0.05f) dt = 0.001f;   // guard against stalls

    float e = wrapPi(handleYaw - psiCam);
    float ae = fabsf(e);

    // --- Deadband window with hysteresis -----------------------------------
    if (!following && ae > t.dbEnter) following = true;
    if ( following && ae < t.dbExit && fabsf(rate) < radians(2.0f))
      following = false;

    // When following, use the small exit band so the move completes cleanly;
    // when idle, desired rate is zero and we ease to rest.
    float omegaDes = 0.0f;
    if (following) {
      float eEff = softDeadband(e, t.dbExit, t.dbSoftness);
      // Soft saturation: linear for small errors, asymptotic to maxRate.
      omegaDes = t.maxRate * tanhf(t.gain * eEff / t.maxRate);
    }

    // --- Exponential rate easing ------------------------------------------
    bool speedingUp = fabsf(omegaDes) > fabsf(rate) &&
                      (omegaDes * rate >= 0.0f);
    float tau   = speedingUp ? t.tauAccel : t.tauDecel;
    float alpha = 1.0f - expf(-dt / tau);
    float dOmega = (omegaDes - rate) * alpha;

    // Hard acceleration clamp (protects against IMU glitches)
    float dMax = t.maxAccel * dt;
    dOmega = constrain(dOmega, -dMax, dMax);
    rate += dOmega;

    // --- Integrate camera heading -----------------------------------------
    psiCam = wrapPi(psiCam + rate * dt);

    // --- Convert to motor-relative target ---------------------------------
    float rel = wrapPi(psiCam - handleYaw);

    // Mechanical soft limit: if the handle outruns the follow, drag the
    // camera heading along so the motor never hits the stop.
    if (rel >  t.mechLimit) { rel =  t.mechLimit; psiCam = wrapPi(handleYaw + rel); }
    if (rel < -t.mechLimit) { rel = -t.mechLimit; psiCam = wrapPi(handleYaw + rel); }

    return rel + t.mechOffset;
  }

  // Osmo-style "recenter" (double-tap trigger): snap target to handle forward,
  // still eased because the rate filter does the work.
  void recenter() { following = true; }

  float cameraHeading() const { return psiCam; }
  float followRate()    const { return rate;   }
  bool  isFollowing()   const { return following; }

private:
  float psiCam = 0.0f;
  float rate   = 0.0f;
  bool  following = false;
};

SmoothFollow follow;
Commander command(Serial);
void onMotor(char* cmd) { command.motor(&motor, cmd); }

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  sensor.init();
  motor.linkSensor(&sensor);

  driver.voltage_power_supply = 12;
  driver.init();
  motor.linkDriver(&driver);

  motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
  motor.controller     = MotionControlType::angle;
  motor.torque_controller = TorqueControlType::voltage;

  // Gimbal motors: high-resistance, low-speed -> voltage mode, gentle loops
  motor.voltage_limit   = 6;
  motor.velocity_limit  = 6;        // rad/s, above follow maxRate
  motor.PID_velocity.P  = 0.15f;
  motor.PID_velocity.I  = 4.0f;
  motor.PID_velocity.D  = 0.0f;
  motor.PID_velocity.output_ramp = 1000;
  motor.LPF_velocity.Tf = 0.01f;
  motor.P_angle.P       = 20.0f;

  motor.init();
  motor.initFOC();

  follow.reset(imuHandleYaw(), motor.shaft_angle);
  command.add('M', onMotor, "motor");
}

// ---------------------------------------------------------------------------
// Loop: FOC every pass, follow update at ~1 kHz
// ---------------------------------------------------------------------------
void loop() {
  motor.loopFOC();

  static uint32_t last = micros();
  uint32_t now = micros();
  if (now - last >= 1000) {
    float dt = (now - last) * 1e-6f;
    