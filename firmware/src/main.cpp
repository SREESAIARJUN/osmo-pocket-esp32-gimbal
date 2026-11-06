/**
 * ESP32-S3 3-Axis Ultra-Smooth Gimbal Controller — Revision 7.0
 * Framework: Arduino-ESP32 / SimpleFOC
 * Hardware: 3x TMC6300 BLDC drivers, 3x AS5600 12-bit encoders behind a
 *           TCA9548A I2C switch, dual ICM-42688-P IMUs (SPI)
 *
 * Rev 7.0 encoder architecture:
 *   core 0: EncoderBus task sweeps TCA9548A ch0..2 -> AS5600 RAW ANGLE (~2.6 kHz/axis)
 *   core 1: loop() runs loopFOC()/move() for all three axes on cached samples
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
    Serial.println(F("[GIMBAL] Ready. Closed-loop stabilization ACTIVE."));
}

void loop() {
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
        ax.motor.loopFOC();
        ax.motor.move(ax.target);
        ax.motor.voltage.q = _constrain(
            ax.motor.voltage.q + get_anti_cogging_torque(i, ax.sensor.getMechanicalAngle()),
            -ax.motor.voltage_limit, ax.motor.voltage_limit);
    }
}
