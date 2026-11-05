/**
 * ESP32-S3 3-Axis Ultra-Smooth Gimbal Controller
 * Framework: Arduino-ESP32 / SimpleFOC
 * Hardware: 3x TMC6300 BLDC Drivers, 3x AS5600 14-bit Encoders, Dual ICM-42688-P IMUs
 */

#include <Arduino.h>
#include <SimpleFOC.h>
#include <Wire.h>
#include <SPI.h>
#include "anti_cogging.h"

// BLDC Motor & Driver Instances: Pan, Roll, Pitch
// Motors: 14 pole pairs (28 poles), 12N14P gimbal motors
BLDCMotor motorPan = BLDCMotor(14);
BLDCDriver3PWM driverPan = BLDCDriver3PWM(4, 5, 6, 7); // PWM_A, PWM_B, PWM_C, EN

BLDCMotor motorRoll = BLDCMotor(14);
BLDCDriver3PWM driverRoll = BLDCDriver3PWM(8, 9, 10, 11);

BLDCMotor motorPitch = BLDCMotor(14);
BLDCDriver3PWM driverPitch = BLDCDriver3PWM(12, 13, 14, 15);

// Magnetic Encoders (AS5600 via I2C / multiplexer or SPI)
MagneticSensorI2C sensorPan = MagneticSensorI2C(AS5600_I2C);
MagneticSensorI2C sensorRoll = MagneticSensorI2C(AS5600_I2C);
MagneticSensorI2C sensorPitch = MagneticSensorI2C(AS5600_I2C);

// Cascaded Target Angles
float target_pan = 0.0f;
float target_roll = 0.0f;
float target_pitch = 0.0f;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println(F("[GIMBAL] Initializing ESP32-S3 3-Axis SimpleFOC Controller..."));

    // 1. Initialize Encoders
    Wire.begin(21, 22, 400000); // SDA, SCL @ 400kHz
    sensorPan.init();
    sensorRoll.init();
    sensorPitch.init();

    // 2. Link Encoders to Motors
    motorPan.linkSensor(&sensorPan);
    motorRoll.linkSensor(&sensorRoll);
    motorPitch.linkSensor(&sensorPitch);

    // 3. Driver PWM Setup (20kHz carrier for silent FOC operation)
    driverPan.pwm_frequency = 20000;
    driverRoll.pwm_frequency = 20000;
    driverPitch.pwm_frequency = 20000;
    
    driverPan.voltage_power_supply = 7.4f; // 2S Li-ion
    driverRoll.voltage_power_supply = 7.4f;
    driverPitch.voltage_power_supply = 7.4f;

    driverPan.init();
    driverRoll.init();
    driverPitch.init();

    motorPan.linkDriver(&driverPan);
    motorRoll.linkDriver(&driverRoll);
    motorPitch.linkDriver(&driverPitch);

    // 4. Control Modes & PID Parameters
    motorPan.controller = MotionControlType::angle;
    motorRoll.controller = MotionControlType::angle;
    motorPitch.controller = MotionControlType::angle;

    // Pan PID
    motorPan.P_angle.P = 18.0f;
    motorPan.P_angle.I = 0.8f;
    motorPan.P_angle.D = 0.12f;
    motorPan.voltage_limit = 4.5f;

    // Roll PID (Critical Horizon Lock)
    motorRoll.P_angle.P = 22.0f;
    motorRoll.P_angle.I = 1.2f;
    motorRoll.P_angle.D = 0.18f;
    motorRoll.voltage_limit = 4.5f;

    // Pitch PID
    motorPitch.P_angle.P = 16.0f;
    motorPitch.P_angle.I = 0.6f;
    motorPitch.P_angle.D = 0.10f;
    motorPitch.voltage_limit = 4.0f;

    // 5. Initialize FOC Loops
    motorPan.init();
    motorRoll.init();
    motorPitch.init();

    motorPan.initFOC();
    motorRoll.initFOC();
    motorPitch.initFOC();

    Serial.println(F("[GIMBAL] FOC Alignment Complete. Initializing Anti-Cogging Compensation..."));
    init_anti_cogging();

    Serial.println(F("[GIMBAL] Ready. Closed-loop Stabilization ACTIVE."));
}

void loop() {
    // 1. High-frequency FOC loop execution (~10 kHz)
    motorPan.loopFOC();
    motorRoll.loopFOC();
    motorPitch.loopFOC();

    // 2. Anti-cogging feedforward torque injection
    motorPan.voltage.q += get_anti_cogging_torque(0, sensorPan.getAngle());
    motorRoll.voltage.q += get_anti_cogging_torque(1, sensorRoll.getAngle());
    motorPitch.voltage.q += get_anti_cogging_torque(2, sensorPitch.getAngle());

    // 3. Motion control update
    motorPan.move(target_pan);
    motorRoll.move(target_roll);
    motorPitch.move(target_pitch);
}
