// Minimal host replica of SimpleFOC v2.3.x `Sensor` (src/common/base_classes/Sensor.{h,cpp})
// — only the members MuxedAS5600 touches, with the same semantics.
#pragma once
#include "Arduino.h"

#define _2PI 6.28318530718f
#define _micros micros

class Sensor {
public:
    virtual ~Sensor() = default;
    virtual void update() {
        float val = getSensorAngle();
        if (val < 0) return;
        angle_prev_ts = _micros();
        float d_angle = val - angle_prev;
        if (fabsf(d_angle) > (0.8f * _2PI)) full_rotations += (d_angle > 0) ? -1 : 1;
        angle_prev = val;
    }
    virtual void init() {
        getSensorAngle();
        delayMicroseconds(1);
        vel_angle_prev = getSensorAngle();
        vel_angle_prev_ts = _micros();
        delay(1);
        getSensorAngle();
        delayMicroseconds(1);
        angle_prev = getSensorAngle();
        angle_prev_ts = _micros();
    }
    virtual float getSensorAngle() = 0;
    float getMechanicalAngle() { return angle_prev; }
    float getAngle() { return (float)full_rotations * _2PI + angle_prev; }
    long  timestamp() const { return angle_prev_ts; }

protected:
    float   angle_prev = 0;
    long    angle_prev_ts = 0;
    float   vel_angle_prev = 0;
    long    vel_angle_prev_ts = 0;
    int32_t full_rotations = 0;
    int32_t vel_full_rotations = 0;
};
