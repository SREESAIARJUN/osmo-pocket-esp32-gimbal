/**
 * joystick_input.h — ESP32-S3 ADC1 thumb-joystick task (Revision 59.0)
 *
 * One FreeRTOS task on core 0 (priority below the IMU and encoder tasks) owns
 * ADC1 exclusively. Every JOY_PERIOD_MS it oversamples JOY_X (GPIO2,
 * ADC1_CH1) and JOY_Y (GPIO7, ADC1_CH6) with the eFuse-calibrated
 * analogReadMilliVolts(), runs joy::Shaper and publishes joy::Command through
 * a SeqLock. The FOC loop on core 1 only ever reads the mailbox (wait-free).
 *
 * Boot: the first JOY_CAL_SAMPLES ticks calibrate the stick centre (thumb
 * must be off the stick); a deflected / noisy / unplugged stick is rejected
 * and the factory centre is used with FLAG_CENTER_DEFAULTED set. Until the
 * calibration completes nothing is published, so consumers see "no command".
 */
#ifndef JOYSTICK_INPUT_H
#define JOYSTICK_INPUT_H

#include <stdint.h>
#include "joystick_shaper.h"

/** Start the joystick task. @p cfg is copied. */
bool start_joystick_task(const joy::Config& cfg, uint8_t core, uint8_t priority);

/** Latest published command; false until the boot centre calibration finished. */
bool joystick_latest(joy::Command& out);

/** Precision ("slow") mode, applied before the smoother so the change eases in. */
void joystick_set_precision(bool on);
bool joystick_precision();

#endif  // JOYSTICK_INPUT_H
