// SimpleFOC Tuning & Configuration Profile - Revision 7.0 / 150
#ifndef GIMBAL_CONFIG_H
#define GIMBAL_CONFIG_H

#define GIMBAL_REVISION "7.0"

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
#define MOTOR_POLE_PAIRS    14     // 12N14P gimbal motors

#define FOC_PWM_FREQ 20000
#define ANTI_COGGING_ORDER 12

// ---- Encoder bus (Rev 7.0: TCA9548A address-conflict resolution) ----
#define ENC_I2C_HZ          400000  // TCA9548A max (Fast-mode); AS5600 alone could do 1 MHz
#define ENC_MUX_ADDR        0x70    // A2..A0 = GND
#define ENC_MUX_CH_PAN      0
#define ENC_MUX_CH_ROLL     1
#define ENC_MUX_CH_PITCH    2
#define ENC_TASK_CORE       0       // FOC runs in loop() on core 1
#define ENC_TASK_PRIORITY   5
#define ENC_STALE_LIMIT_US  10000   // FOC watchdog: latch-disable an axis whose sample is older than this.
                                    // 10 ms @ 14 pole pairs and 1 rad/s = 8 deg electrical error worst case;
                                    // must exceed one 2 ms Wire timeout + bus-clear recovery (~3 ms).

#endif  // GIMBAL_CONFIG_H
