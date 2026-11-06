// SimpleFOC Tuning & Configuration Profile - Revision 8.0 / 150
#ifndef GIMBAL_CONFIG_H
#define GIMBAL_CONFIG_H

#define GIMBAL_REVISION "8.0"

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

#endif  // GIMBAL_CONFIG_H
