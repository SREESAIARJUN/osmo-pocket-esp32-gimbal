// SimpleFOC Tuning & Configuration Profile - Revision 59.0 / 150
#ifndef GIMBAL_CONFIG_H
#define GIMBAL_CONFIG_H

#define GIMBAL_REVISION "59.0"

// ---- Angle-loop PID (consumed by main.cpp; previously hard-coded there) ----
#define PAN_KP   18.00f
#define PAN_KI    0.80f
#define PAN_KD    0.1200f
#define ROLL_KP  22.30f
#define ROLL_KI   1.20f
#define ROLL_KD   0.1860f
#define PITCH_KP 16.00f
#define PITCH_KI  0.60f
#define PITCH_KD  0.1000f

#define PAN_VOLTAGE_LIMIT   4.5f
#define ROLL_VOLTAGE_LIMIT  4.5f
#define PITCH_VOLTAGE_LIMIT 4.0f
#define SUPPLY_VOLTAGE      7.4f   // 2S Li-ion nominal
#define MOTOR_STATOR_SLOTS  12     // 12N14P: 12 stator teeth ...
#define MOTOR_POLE_PAIRS    7      // ... 14 magnet POLES = 7 pole pairs (Rev <= 7.0 wrongly used 14)
#define MOTOR_KT            0.035f // N*m/A  (matches simulation/gimbal_physics_sim.py)
#define MOTOR_PHASE_R       8.5f   // Ohm

#define FOC_PWM_FREQ 20000
// ---- Anti-cogging feed-forward (Rev 8.0) ----
// Cogging fundamental for 12N14P = lcm(12,14)/7 = 12th ELECTRICAL harmonic
// (84 cogging cycles per mechanical rev). Retain 12th + 24th.
#define ANTI_COGGING_ORDER            24       // highest electrical harmonic kept by calibration fit
#define ANTI_COGGING_VQ_LIMIT         0.50f    // V, hard clamp on the feed-forward term
#define ANTI_COGGING_FADE_START_RAD_S 20.0f    // |omega_e| below: full compensation (~164 deg/s mech)
#define ANTI_COGGING_FADE_END_RAD_S   120.0f   // |omega_e| above: none (cogging @ 229 Hz, beyond loop BW)
#define ANTI_COGGING_MAX_LATENCY_S    0.002f   // clamp on phase-advance horizon
// Optional per-unit calibration sweep at boot (moves each axis +/- the span below).
#ifndef ANTI_COGGING_CALIBRATE_ON_BOOT
#define ANTI_COGGING_CALIBRATE_ON_BOOT 0
#endif
#define ANTI_COGGING_CAL_ELEC_PERIODS 2.0f     // 2 x 51.4 deg mech: stays inside roll/pitch stops
#define ANTI_COGGING_CAL_RATE_RAD_S   0.20f    // mech rad/s: quasi-static (omega_e = 1.4 rad/s)
// Factory seed cogging-torque model (N*m), sin(k*theta_e + COG_SEED_PHASE_RAD).
// Amplitudes are the Rev 7.0 Vq table values (V) converted via tau = V*Kt/R;
// phase pi because Rev 7.0 stored the *correction* (= -cogging).
#define COG_SEED_PHASE_RAD   3.14159265f
#define COG_SEED_PAN_H1_NM   (0.08f * MOTOR_KT / MOTOR_PHASE_R)
#define COG_SEED_PAN_H2_NM   (0.03f * MOTOR_KT / MOTOR_PHASE_R)
#define COG_SEED_ROLL_H1_NM  (0.09f * MOTOR_KT / MOTOR_PHASE_R)
#define COG_SEED_ROLL_H2_NM  (0.04f * MOTOR_KT / MOTOR_PHASE_R)
#define COG_SEED_PITCH_H1_NM (0.07f * MOTOR_KT / MOTOR_PHASE_R)
#define COG_SEED_PITCH_H2_NM (0.02f * MOTOR_KT / MOTOR_PHASE_R)

// ---- Encoder bus (Rev 7.0: TCA9548A address-conflict resolution) ----
#define ENC_I2C_HZ          400000  // TCA9548A max (Fast-mode); AS5600 alone could do 1 MHz
#define ENC_MUX_ADDR        0x70    // A2..A0 = GND
#define ENC_MUX_CH_PAN      0
#define ENC_MUX_CH_ROLL     1
#define ENC_MUX_CH_PITCH    2
#define ENC_TASK_CORE       0       // FOC runs in loop() on core 1
#define ENC_TASK_PRIORITY   5
#define ENC_STALE_LIMIT_US  10000   // FOC watchdog: latch-disable an axis whose sample is older than this.
                                    // 10 ms @ 7 pole pairs and 1 rad/s = 4 deg electrical error worst case;
                                    // must exceed one 2 ms Wire timeout + bus-clear recovery (~3 ms).

// ---- IMU attitude pipeline (Rev 57.0): 2x ICM-42688-P on SPI2 ----
#define IMU_SPI_HZ          8000000  // ICM-42688-P max 24 MHz; 8 MHz keeps margin on the cradle flex
#define IMU_TASK_CORE       0        // with the encoder sweep, away from FOC on core 1
#define IMU_TASK_PRIORITY   6        // > ENC_TASK_PRIORITY: 1 kHz tick preempts the I2C sweep
#define IMU_STALE_LIMIT_US  5000     // consumer watchdog: attitude older than this is unusable
// Sensor-to-body axis maps, body[i] = sign * sensor[|m|-1] (must be a proper rotation;
// checked at compile time in main.cpp). Body frame: X forward (lens), Y left, Z up.
#define IMU_CAM_MOUNT       {1, 2, 3}
#define IMU_HANDLE_MOUNT    {1, 2, 3}
// Madgwick gains. beta = sqrt(3/4) * gyro error; 0.02 rad/s tolerates ~1.3 dps of
// residual error while limiting handle-acceleration leakage into the horizon.
#define AHRS_BETA           0.02f
#define AHRS_BETA_INIT      1.0f     // rad/s during the first AHRS_INIT_TIME_S (fast alignment)
#define AHRS_INIT_TIME_S    0.5f
#define AHRS_ZETA           0.0f     // Madgwick gyro-bias integrator; bias comes from imu_bias instead
#define AHRS_GATE_LO_G      0.05f    // accel weight 1 below | |a|-1g |, 0 above AHRS_GATE_HI_G
#define AHRS_GATE_HI_G      0.20f
// Gyro temperature-bias model priors (ICM-42688-P: ZRO +/-0.5 dps, tempco +/-0.005 dps/degC typ.)
#define GYRO_BIAS_B0_SIGMA_DPS     1.0f
#define GYRO_BIAS_B1_SIGMA_DPS_C   0.01f
#ifndef IMU_TELEMETRY_PERIOD_MS
#define IMU_TELEMETRY_PERIOD_MS    0        // >0: print attitude/bias from loop() at this period
#endif

// ---- Thumb joystick rate control (Rev 59.0): joy_adc task, joystick_shaper.h ----
#define JOY_ENABLE                1
#define JOY_TASK_CORE             0        // with the IMU/encoder tasks, away from FOC on core 1
#define JOY_TASK_PRIORITY         3        // < ENC (5) < IMU (6): a late joystick tick is harmless
#define JOY_PERIOD_MS             5        // 200 Hz: 10x the ~20 Hz bandwidth of a thumb
#define JOY_OVERSAMPLE            4
#define JOY_CAL_SAMPLES           100      // 0.5 s boot centre calibration, thumb off the stick
#define JOY_CENTER_NOMINAL_MV     1650.0f  // VDD/2 of the ratiometric pot
#define JOY_CENTER_MAX_OFFSET_MV  350.0f
#define JOY_CENTER_MAX_STD_MV     20.0f
#define JOY_STALE_LIMIT_US        50000    // consumer watchdog: older command => rate 0 (10 missed ticks)
// Curves: f(s) = (e^{k s} - 1)/(e^k - 1). Centre gain k/(e^k-1): k=3 -> 0.157, k=2.5 -> 0.218.
#define JOY_PAN_EXPO_K            3.0f
#define JOY_PITCH_EXPO_K          2.5f
#define JOY_PAN_MAX_RATE_DPS      90.0f
#define JOY_PITCH_MAX_RATE_DPS    60.0f
#define JOY_PAN_MAX_ACCEL_DPS2    180.0f   // 0 -> 90 deg/s in >= 0.5 s: no visible "lurch"
#define JOY_PITCH_MAX_ACCEL_DPS2  120.0f
#define JOY_PAN_INVERT            0
#define JOY_PITCH_INVERT          0
#define JOY_SMOOTH_TAU_S          0.08f    // per pole; 2 poles -> ~0.27 s 10-90% ease on a step
#define JOY_PRECISION_SCALE       0.30f    // 'P' serial command toggles precision mode
// Dynamic deadband (normalised stick units, 1.0 = full deflection)
#define JOY_DB_BASE               0.025f
#define JOY_DB_NOISE_K            5.0f
#define JOY_DB_MIN                0.03f
#define JOY_DB_MAX                0.20f
#define JOY_RELEASE_RADIUS        0.22f
#define JOY_RELEASE_TAU_S         0.12f
// Travel limits around the power-on pose (rad, 0 = unlimited) with a soft decel zone.
#define JOY_PAN_LIMIT_RAD         2.967f   // +/-170 deg: protects the yoke harness (no slip ring)
#define JOY_PITCH_LIMIT_RAD       1.047f   // +/-60 deg: inside the cradle hard stops
#define JOY_SOFT_ZONE_RAD         0.26f    // ~15 deg linear rate fade into each limit
#ifndef JOY_TELEMETRY_PERIOD_MS
#define JOY_TELEMETRY_PERIOD_MS   0        // >0: print joystick state from loop() at this period
#endif

#endif  // GIMBAL_CONFIG_H
