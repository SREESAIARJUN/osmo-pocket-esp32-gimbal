/**
 * ESP32-S3 3-Axis Ultra-Smooth Gimbal Controller — Revision 8.0
 * Framework: Arduino-ESP32 / SimpleFOC
 * Hardware: 3x TMC6300 BLDC drivers, 3x AS5600 12-bit encoders behind a
 *           TCA9548A I2C switch, dual ICM-42688-P IMUs (SPI)
 *
 * Rev 7.0 encoder architecture:
 *   core 0: EncoderBus task sweeps TCA9548A ch0..2 -> AS5600 RAW ANGLE (~2.6 kHz/axis)
 *   core 1: loop() runs loopFOC()/move() for all three axes on cached samples
 *
 * Rev 8.0 anti-cogging:
 *   512-point cogging-torque LUT per axis indexed by the NORMALISED electrical
 *   angle wrap(dir*pp*theta_m - zero_electric_angle); feed-forward
 *   Vq = -tau_cog(theta_e + omega_e*latency) * R/Kt, faded out at speed.
 */

#include <Arduino.h>
#include <SimpleFOC.h>
#include <Wire.h>
#include <SPI.h>
#include "gimbal_pins.h"
#include "gimbal_config.h"
#include "tca9548a.h"
#include "as5600_mux.h"
#include "anti_cogging.h"

// ---------------------------------------------------------------------------
// Encoder bus: one TCA9548A, three AS5600 (all at 0x36) on separate channels
// ---------------------------------------------------------------------------
static TCA9548A encMux(Wire, ENC_MUX_ADDR, pins::ENC_MUX_RST_N);
static const uint8_t kEncChannels[EncoderBus::NUM_AXES] = {
    ENC_MUX_CH_PAN, ENC_MUX_CH_ROLL, ENC_MUX_CH_PITCH};
static EncoderBus encoders(Wire, encMux, pins::ENC_I2C_SDA, pins::ENC_I2C_SCL, kEncChannels);

static MuxedAS5600 sensorPan(encoders, 0);
static MuxedAS5600 sensorRoll(encoders, 1);
static MuxedAS5600 sensorPitch(encoders, 2);

// ---------------------------------------------------------------------------
// BLDC motors & drivers: Pan, Roll, Pitch (12N14P gimbal motors)
// ---------------------------------------------------------------------------
static BLDCMotor motorPan(MOTOR_POLE_PAIRS);
static BLDCDriver3PWM driverPan(pins::PAN_PWM_A, pins::PAN_PWM_B, pins::PAN_PWM_C, pins::PAN_EN);

static BLDCMotor motorRoll(MOTOR_POLE_PAIRS);
static BLDCDriver3PWM driverRoll(pins::ROLL_PWM_A, pins::ROLL_PWM_B, pins::ROLL_PWM_C, pins::ROLL_EN);

static BLDCMotor motorPitch(MOTOR_POLE_PAIRS);
static BLDCDriver3PWM driverPitch(pins::PITCH_PWM_A, pins::PITCH_PWM_B, pins::PITCH_PWM_C, pins::PITCH_EN);

struct Axis {
    const char*     name;
    BLDCMotor&      motor;
    BLDCDriver3PWM& driver;
    MuxedAS5600&    sensor;
    float kp, ki, kd, vlimit;
    float target;
    bool  active;
};

static Axis axes[EncoderBus::NUM_AXES] = {
    {"PAN",   motorPan,   driverPan,   sensorPan,   PAN_KP,   PAN_KI,   PAN_KD,   PAN_VOLTAGE_LIMIT,   0.0f, false},
    {"ROLL",  motorRoll,  driverRoll,  sensorRoll,  ROLL_KP,  ROLL_KI,  ROLL_KD,  ROLL_VOLTAGE_LIMIT,  0.0f, false},
    {"PITCH", motorPitch, driverPitch, sensorPitch, PITCH_KP, PITCH_KI, PITCH_KD, PITCH_VOLTAGE_LIMIT, 0.0f, false},
};

// Loop timing for the anti-cogging phase advance (time until the next loopFOC()).
static uint32_t g_last_loop_us = 0;
static float    g_loop_dt_s    = 0.0005f;  // seeded estimate, IIR-tracked in loop()

static float axis_electrical_angle(Axis& ax) {
    return cogging::electrical_angle(ax.sensor.getMechanicalAngle(), (int)ax.motor.pole_pairs,
                                     (int)ax.motor.sensor_direction, ax.motor.zero_electric_angle);
}

#if ANTI_COGGING_CALIBRATE_ON_BOOT
// Per-unit cogging calibration: track a slow bidirectional ramp over
// ANTI_COGGING_CAL_ELEC_PERIODS electrical periods around the power-on pose and
// bin the Vq the closed loop needs. Forward + reverse passes cancel Coulomb
// friction; the harmonic fit (multiples of the 12th) rejects gravity/DC load.
// Feed-forward is OFF while sampling so the raw cogging is measured.
static void calibrate_axis(Axis& ax, uint8_t idx) {
    static cogging::Calibrator cal;
    cal.reset();
    const float span   = ANTI_COGGING_CAL_ELEC_PERIODS * cogging::TWO_PI_F / (float)ax.motor.pole_pairs;
    const float start  = ax.target;
    const float rate   = ANTI_COGGING_CAL_RATE_RAD_S;  // mechanical rad/s
    const float dt     = 0.0005f;
    const int   steps  = (int)(span / (rate * dt));
    const int   settle = (int)(0.5f / dt);           // 0.5 s at the start pose, not sampled
    for (int pass = -1; pass < 4; ++pass) {  // settle, then +span, -span, -span, +span (symmetric)
        const float dir = (pass == 0 || pass == 3) ? 1.0f : (pass < 0 ? 0.0f : -1.0f);
        for (int s = 0; s < (pass < 0 ? settle : steps); ++s) {
            const uint32_t t0 = micros();
            ax.target += dir * rate * dt;
            ax.motor.loopFOC();
            ax.motor.move(ax.target);
            for (uint8_t j = 0; j < EncoderBus::NUM_AXES; ++j) {  // keep the other axes holding
                if (j == idx || !axes[j].active) continue;
                axes[j].motor.loopFOC();
                axes[j].motor.move(axes[j].target);
            }
            if (pass >= 0 && ax.sensor.healthy() && ax.sensor.sampleAgeUs() <= ENC_STALE_LIMIT_US)
                cal.accumulate(axis_electrical_angle(ax), ax.motor.voltage.q);
            while (micros() - t0 < (uint32_t)(dt * 1e6f)) {}
        }
    }
    ax.target = start;
    cogging::HarmonicSet hs;
    const int k0 = cogging::fundamental_order(MOTOR_STATOR_SLOTS, MOTOR_POLE_PAIRS);
    if (cal.fit(k0, ANTI_COGGING_ORDER, MOTOR_KT, MOTOR_PHASE_R, hs) && anti_cogging_load(idx, hs)) {
        Serial.printf("[COG] %s calibrated (coverage %.0f%%):", ax.name, 100.0f * cal.coverage());
        for (uint8_t k = 0; k < hs.count; ++k)
            Serial.printf(" h%u=%.3fmNm@%.2frad", hs.h[k].order, 1e3f * hs.h[k].amp_nm, hs.h[k].phase_rad);
        Serial.println();
    } else {
        Serial.printf("[COG] %s calibration rejected (coverage %.0f%%) -> keeping seed model\n",
                      ax.name, 100.0f * cal.coverage());
    }
}
#endif

static bool init_axis(Axis& ax, uint8_t idx) {
    if (!ax.sensor.healthy()) {
        Serial.printf("[GIMBAL] %s encoder %s -> axis left DISABLED\n", ax.name,
                      encoder_health_str(encoders.health(idx)));
        return false;
    }
    ax.sensor.init();
    ax.motor.linkSensor(&ax.sensor);

    ax.driver.pwm_frequency        = FOC_PWM_FREQ;
    ax.driver.voltage_power_supply = SUPPLY_VOLTAGE;
    if (!ax.driver.init()) {
        Serial.printf("[GIMBAL] %s driver init FAILED (no PWM timer?)\n", ax.name);
        return false;
    }
    ax.motor.linkDriver(&ax.driver);

    ax.motor.controller    = MotionControlType::angle;
    ax.motor.P_angle.P     = ax.kp;
    ax.motor.P_angle.I     = ax.ki;
    ax.motor.P_angle.D     = ax.kd;
    ax.motor.voltage_limit = ax.vlimit;

    ax.motor.init();
    if (!ax.motor.initFOC()) {
        Serial.printf("[GIMBAL] %s initFOC FAILED\n", ax.name);
        ax.motor.disable();
        return false;
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("[GIMBAL] Rev %s ESP32-S3 3-Axis SimpleFOC controller\n", GIMBAL_REVISION);

    // 1. Encoder bus: I2C0 on GPIO17/18 -> TCA9548A @0x70 -> 3x AS5600 @0x36
    const bool enc_ok = encoders.begin(ENC_I2C_HZ);
    for (uint8_t i = 0; i < EncoderBus::NUM_AXES; ++i) {
        Serial.printf("[ENC] %-5s mux ch%u: %s AGC=%u fast_read=%s\n", axes[i].name,
                      kEncChannels[i], encoder_health_str(encoders.health(i)),
                      encoders.agc(i), encoders.fastReadEnabled(i) ? "yes" : "no");
    }
    if (!enc_ok) Serial.println(F("[ENC] WARNING: not all encoders healthy"));
    if (!encoders.startTask(ENC_TASK_CORE, ENC_TASK_PRIORITY)) {
        Serial.println(F("[ENC] FATAL: could not start encoder task; motors stay disabled"));
        return;
    }
    delay(5);  // let the task publish fresh samples before Sensor::init()

    // 2. Per-axis driver/motor/FOC bring-up. A faulty encoder never energises its motor.
    for (uint8_t i = 0; i < EncoderBus::NUM_AXES; ++i) axes[i].active = init_axis(axes[i], i);

    Serial.println(F("[GIMBAL] FOC alignment complete. Initializing anti-cogging compensation..."));
    init_anti_cogging();
    for (uint8_t i = 0; i < EncoderBus::NUM_AXES; ++i) {
        const cogging::Lut* lut = anti_cogging_lut(i);
        Serial.printf("[COG] %-5s seed LUT %d pts, peak %.3f mN*m (%.3f V)\n", axes[i].name,
                      cogging::LUT_SIZE, 1e3f * lut->peak_nm(), lut->peak_nm() * MOTOR_PHASE_R / MOTOR_KT);
#if ANTI_COGGING_CALIBRATE_ON_BOOT
        if (axes[i].active) calibrate_axis(axes[i], i);
#endif
    }
    g_last_loop_us = micros();
    Serial.println(F("[GIMBAL] Ready. Closed-loop stabilization ACTIVE."));
}

void loop() {
    // One pass services all axes, so the next loopFOC() of an axis is one loop period away.
    const uint32_t now = micros();
    const float dt = (float)(now - g_last_loop_us) * 1e-6f;
    g_last_loop_us = now;
    if (dt > 0.0f && dt < 0.01f) g_loop_dt_s += 0.05f * (dt - g_loop_dt_s);

    for (uint8_t i = 0; i < EncoderBus::NUM_AXES; ++i) {
        Axis& ax = axes[i];
        if (!ax.active) continue;

        // Encoder watchdog: never commutate on a stale or faulted angle.
        if (!ax.sensor.healthy() || ax.sensor.sampleAgeUs() > ENC_STALE_LIMIT_US) {
            ax.motor.disable();
            ax.active = false;
            Serial.printf("[GIMBAL] %s encoder %s / stale -> axis DISABLED\n", ax.name,
                          encoder_health_str(encoders.health(i)));
            continue;
        }

        // loopFOC() applies voltage.q computed by the previous move(); move()
        // recomputes it, then the anti-cogging feed-forward is added so that the
        // NEXT loopFOC() actually applies it (Rev <= 6.0 added it before move(),
        // which overwrote it every cycle).
        // Rev 8.0: the LUT is indexed by the normalised ELECTRICAL angle (Rev 7.0
        // passed the raw mechanical angle -> wrong by a factor of pole_pairs) and
        // phase-advanced by the time until that Vq is actually applied.
        ax.motor.loopFOC();
        ax.motor.move(ax.target);
        const float theta_e = axis_electrical_angle(ax);
        const float omega_e = (float)ax.motor.pole_pairs * ax.motor.shaft_velocity;  // shaft_velocity is direction-corrected
        float latency = g_loop_dt_s + 1e-6f * (float)ax.sensor.sampleAgeUs();
        if (latency > ANTI_COGGING_MAX_LATENCY_S) latency = ANTI_COGGING_MAX_LATENCY_S;
        ax.motor.voltage.q = _constrain(
            ax.motor.voltage.q + get_anti_cogging_voltage(i, theta_e, omega_e, latency),
            -ax.motor.voltage_limit, ax.motor.voltage_limit);
    }
}
