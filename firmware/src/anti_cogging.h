#ifndef ANTI_COGGING_H
#define ANTI_COGGING_H

#include <Arduino.h>

#define COGGING_LUT_SIZE 512

void init_anti_cogging();
float get_anti_cogging_torque(uint8_t axis, float electrical_angle);

#endif
