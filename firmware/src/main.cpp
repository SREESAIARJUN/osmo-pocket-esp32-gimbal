/**
 * ESP32-S3 3-Axis Ultra-Smooth Gimbal Controller — Revision 59.0
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
 *
 * Rev 57.0 attitude:
 *   core 0: imu_spi task (prio 6, 1 kHz vTaskDelayUntil) drains both ICM-42688-P
 *           FIFOs (20-bit records + device timestamps), subtracts the
 *           temperature-modelled gyro bias, runs a Madgwick AHRS per IMU and
 *           publishes AttitudeState through a seqlock.
 *   NVS:    the learned gyro bias(T) model is restored at boot and saved on
 *           the 'B' serial command while all motors are idle (flash writes
 *           stall both cores' caches, so never while commutating).
 *
 * Rev 59.0 thumb joystick:
 *   core 0: joy_adc task (prio 3, 200 Hz) reads JOY_X/JOY_Y on ADC1, runs the
 *           dynamic-deadband + exponential-curve + ease/accel-limit shaper
 *           (joystick_shaper.h) and publishes rad/s rate commands via seqlock.
 *   core 1: loop() integrates the rates into the PAN / PITCH angle targets
 *           with soft travel limits around the power-on pose; a command older
 *           than JOY_STALE_LIMIT_US is treated as zero rate (no runaway if the
 *           task ever stops). 'P' toggles precision mode.
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
#include "icm42688.h"
#include "imu_pipeline.h"
#include "joystick_input.h"
#include <Preferences.h>

// ---------------------------------------------------------------------------
// IMUs: 2x ICM-42688-P on SPI2 (FSPI), one owner task on core 0
// ---------------------------------------------------------------------------
static SPIClass         imuSpi(FSPI);
static ArduinoSpiDevice imuDevCam(imuSpi, pins::IMU_CS_CAMERA, IMU_SPI_HZ);
static ArduinoSpiDevice imuDevHandle(imuSpi, pins::IMU_CS_HANDLE, IMU_SPI_HZ);
static ICM42688         imuCam(imuDevCam);
static ICM42688         imuHandle(imuDevHandle);

static constexpr ImuMount kCamMount{IMU_CAM_MOUNT};
static constexpr ImuMount kHandleMount{IMU_HANDLE_MOUNT};
static_assert(kCamMount.valid(),    "IMU_CAM_MOUNT must be a proper signed axis permutation");
static_assert(kHandleMount.valid(), "IMU_HANDLE_MOUNT must be a proper signed axis permutation");

static ImuChannel::Config imu_config() {
    ImuChannel::Config c;
    c.ahrs.beta        = AHRS_BETA;
    c.ahrs.beta_init   = AHRS_BETA_INIT;
    c.ahrs.init_time_s = AHRS_INIT_TIME_S;
    c.ahrs.zeta        = AHRS_ZETA;
    c.ahrs.gate_lo_g   = AHRS_GATE_LO_G;
    c.ahrs.gate_hi_g   = AHRS_GATE_HI_G;
    c.bias.b0_sigma0   = GYRO_BIAS_B0_SIGMA_DPS * icm42688::DEG2RAD;
    c.bias.b1_sigma0   = GYRO_BIAS_B1_SIGMA_DPS_C * icm42688::DEG2RAD;
    return c;
}
static ImuChannel imuChanCam(imuCam, kCamMount, imu_config());
static ImuChannel imuChanHandle(imuHandle, kHandleMount, imu_config());
static ImuChannel* const kImuChannels[] = {&imuChanCam, &imuChanHandle};
static const char* const kImuNames[]    = {"CAMERA", "HANDLE"};
static const char* const kImuNvsKeys[]  = {"gtm_cam", "gtm_hdl"};
static constexpr uint8_t kNumImus = sizeof(kImuChannels) / sizeof(kImuChannels[0]);

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
    int8_t joy_axis;   // joystick axis driving this motor (-1: none)
    float  joy_limit;  // rad around `home` the joystick may move the target (0: unlimited)
    float  home;       // power-on pose (set by init_axis)
};

static Axis axes[EncoderBus::NUM_AXES] = {
    {"PAN",   motorPan,   driverPan,   sensorPan,   PAN_KP,   PAN_KI,   PAN_KD,   PAN_VOLTAGE_LIMIT,   0.0f, false,
     0,  JOY_PAN_LIMIT_RAD,   0.0f},
    {"ROLL",  motorRoll,  driverRoll,  sensorRoll,  ROLL_KP,  ROLL_KI,  ROLL_KD,  ROLL_VOLTAGE_LIMIT,  0.0f, false,
     -1, 0.0f,                0.0f},   // roll holds the horizon; never joystick-driven
    {"PITCH", motorPitch, driverPitch, sensorPitch, PITCH_KP, PITCH_KI, PITCH_KD, PITCH_VOLTAGE_LIMIT, 0.0f, false,
     1,  JOY_PITCH_LIMIT_RAD, 0.0f},
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
    // Hold the power-on pose. shaftAngle() is in the absolute AS5600 frame
    // (sensor_offset = 0), so a 0 rad target made every axis slew at full
    // voltage to an arbitrary magnet zero right after initFOC (Rev 7 review).
    ax.target = ax.motor.shaft_angle;
    ax.home   = ax.target;  // Rev 59: joystick travel limits are relative to this pose
    return true;
}

static void imu_bias_load() {
    Preferences nvs;
    if (!nvs.begin("imu_bias", true)) return;
    for (uint8_t i = 0; i < kNumImus; ++i) {
        GyroTempModel m;
        const bool ok = nvs.getBytes(kImuNvsKeys[i], &m, sizeof m) == sizeof m &&
                        kImuChannels[i]->importBiasModel(m, 3600.0f);  // assume >= 1 h since saved
        Serial.printf("[IMU] %-6s bias(T) model %s\n", kImuNames[i], ok ? "restored from NVS" : "not stored -> factory prior");
    }
    nvs.end();
}

static void imu_bias_save() {
    for (uint8_t i = 0; i < EncoderBus::NUM_AXES; ++i) {
        if (axes[i].active) {
            Serial.println(F("[IMU] bias save refused: motors active (flash write stalls both cores)"));
            return;
        }
    }
    Preferences nvs;
    if (!nvs.begin("imu_bias", false)) return;
    for (uint8_t i = 0; i < kNumImus; ++i) {
        GyroTempModel m;
        if (!kImuChannels[i]->latestModel(m) || !m.valid()) continue;
        nvs.putBytes(kImuNvsKeys[i], &m, sizeof m);
        Serial.printf("[IMU] %-6s bias(T) model saved (%lu windows, %.1f degC span)\n", kImuNames[i],
                      (unsigned long)m.updates, m.t_max - m.t_min);
    }
    nvs.end();
}

static void imu_setup() {
    // Both chip selects high before the bus clocks, so neither IMU sees a
    // spurious transaction while the other is configured.
    imuDevCam.begin();
    imuDevHandle.begin();
    imuSpi.begin(pins::IMU_SPI_SCK, pins::IMU_SPI_MISO, pins::IMU_SPI_MOSI, -1);
    imu_bias_load();
    for (uint8_t i = 0; i < kNumImus; ++i) {
        const ImuStatus st = kImuChannels[i]->begin(micros());
        Serial.printf("[IMU] %-6s ICM-42688-P: %s\n", kImuNames[i], imu_status_str(st));
    }
    if (!start_imu_task(kImuChannels, kNumImus, IMU_TASK_CORE, IMU_TASK_PRIORITY))
        Serial.println(F("[IMU] FATAL: could not start IMU task"));
}

#if JOY_ENABLE
static joy::Config joy_config() {
    constexpr float D2R = 0.017453292519943f;
    joy::Config c;
    c.db_base            = JOY_DB_BASE;
    c.db_noise_k         = JOY_DB_NOISE_K;
    c.db_min             = JOY_DB_MIN;
    c.db_max             = JOY_DB_MAX;
    c.release_radius     = JOY_RELEASE_RADIUS;
    c.release_tau_s      = JOY_RELEASE_TAU_S;
    c.expo_k[0]          = JOY_PAN_EXPO_K;
    c.expo_k[1]          = JOY_PITCH_EXPO_K;
    c.max_rate[0]        = JOY_PAN_MAX_RATE_DPS * D2R;
    c.max_rate[1]        = JOY_PITCH_MAX_RATE_DPS * D2R;
    c.max_accel[0]       = JOY_PAN_MAX_ACCEL_DPS2 * D2R;
    c.max_accel[1]       = JOY_PITCH_MAX_ACCEL_DPS2 * D2R;
    c.sign[0]            = JOY_PAN_INVERT ? -1.0f : 1.0f;
    c.sign[1]            = JOY_PITCH_INVERT ? -1.0f : 1.0f;
    c.smooth_tau_s       = JOY_SMOOTH_TAU_S;
    c.precision_scale    = JOY_PRECISION_SCALE;
    return c;
}

/**
 * Integrate the joystick rate commands into the joystick-driven axis targets.
 * Called once per loop() pass with the measured pass period.
 */
static void joystick_apply(uint32_t now_us, float dt) {
    static bool stale_reported = false;
    float rate[joy::AXES] = {0.0f, 0.0f};
    joy::Command jc;
    if (joystick_latest(jc)) {
        // Signed age: the producer on core 0 may stamp a time just after `now_us`.
        const int32_t age = (int32_t)(now_us - jc.t_us);
        if (age <= (int32_t)JOY_STALE_LIMIT_US) {
            for (uint8_t a = 0; a < joy::AXES; ++a) rate[a] = jc.rate[a];
            stale_reported = false;
        } else if (!stale_reported) {
            stale_reported = true;
            Serial.printf("[JOY] command stale (%ld us) -> rate 0\n", (long)age);
        }
    }
    for (uint8_t i = 0; i < EncoderBus::NUM_AXES; ++i) {
        Axis& ax = axes[i];
        if (!ax.active || ax.joy_axis < 0) continue;
        const float r = joy::soft_limit_rate(rate[ax.joy_axis], ax.target - ax.home, ax.joy_limit,
                                             JOY_SOFT_ZONE_RAD);
        ax.target += r * dt;
    }
}

#if JOY_TELEMETRY_PERIOD_MS > 0
static void joy_telemetry() {
    static uint32_t last_ms = 0;
    const uint32_t now = millis();
    if (now - last_ms < JOY_TELEMETRY_PERIOD_MS) return;
    last_ms = now;
    joy::Command c;
    if (!joystick_latest(c)) return;
    Serial.printf("[JOY] u=%+.3f,%+.3f s=%+.3f,%+.3f w=%+7.2f,%+7.2f dps r=%.3f sig=%.4f,%.4f c=%.0f,%.0f fl=0x%02lx\n",
                  c.norm[0], c.norm[1], c.shaped[0], c.shaped[1], c.rate[0] * RAD_TO_DEG, c.rate[1] * RAD_TO_DEG,
                  c.radius, c.sigma[0], c.sigma[1], c.center_mv[0], c.center_mv[1], (unsigned long)c.flags);
}
#endif
#endif  // JOY_ENABLE

#if IMU_TELEMETRY_PERIOD_MS > 0
static void imu_telemetry() {
    static uint32_t last_ms = 0;
    const uint32_t now = millis();
    if (now - last_ms < IMU_TELEMETRY_PERIOD_MS) return;
    last_ms = now;
    for (uint8_t i = 0; i < kNumImus; ++i) {
        AttitudeState a;
        if (!kImuChannels[i]->latest(a)) continue;
        const uint32_t age = micros() - a.t_us;
        Serial.printf("[IMU] %-6s r=%+7.3f p=%+7.3f y=%+8.3f deg T=%5.2fC bz=%+.4fdps w=%.2f clk=%.5f fl=0x%02x%s\n",
                      kImuNames[i], a.roll * RAD_TO_DEG, a.pitch * RAD_TO_DEG, a.yaw * RAD_TO_DEG, a.temp_c,
                      a.gyro_bias[2] * RAD_TO_DEG, a.accel_weight, a.clock_scale, a.flags,
                      age > IMU_STALE_LIMIT_US ? " STALE" : "");
    }
}
#endif

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("[GIMBAL] Rev %s ESP32-S3 3-Axis SimpleFOC controller\n", GIMBAL_REVISION);

    // 0. IMUs (SPI2, own task on core 0). Independent of the motor bring-up.
    imu_setup();

#if JOY_ENABLE
    // 0b. Joystick (ADC1 owner task on core 0). Started before the multi-second
    // FOC alignment so the 0.5 s centre calibration is done long before any
    // command could be applied; nothing is published until it completes.
    if (!start_joystick_task(joy_config(), JOY_TASK_CORE, JOY_TASK_PRIORITY))
        Serial.println(F("[JOY] could not start joystick task -> joystick disabled"));
#endif

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
    if (Serial.available()) {
        switch (Serial.read()) {
            case 'B': imu_bias_save(); break;
#if JOY_ENABLE
            case 'P':
                joystick_set_precision(!joystick_precision());
                Serial.printf("[JOY] precision mode %s\n", joystick_precision() ? "ON" : "OFF");
                break;
#endif
            default: break;
        }
    }
#if IMU_TELEMETRY_PERIOD_MS > 0
    imu_telemetry();
#endif
#if JOY_ENABLE && JOY_TELEMETRY_PERIOD_MS > 0
    joy_telemetry();
#endif
    // One pass services all axes, so the next loopFOC() of an axis is one loop period away.
    const uint32_t now = micros();
    const float dt = (float)(now - g_last_loop_us) * 1e-6f;
    g_last_loop_us = now;
    if (dt > 0.0f && dt < 0.01f) g_loop_dt_s += 0.05f * (dt - g_loop_dt_s);
#if JOY_ENABLE
    // Integrate over the real pass period; a pathological pass (USB-CDC stall)
    // is bounded so a held stick never produces a target jump.
    joystick_apply(now, (dt > 0.0f && dt < 0.02f) ? dt : g_loop_dt_s);
#endif

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
