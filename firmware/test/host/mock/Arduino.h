// Host-side Arduino shim for unit-testing the Rev 7.0 encoder stack with g++.
#pragma once
#include <stdint.h>
#include <math.h>
#include <map>

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define OUTPUT_OPEN_DRAIN 0x13
#ifndef PI
#define PI 3.14159265358979f
#endif

namespace mock {
extern uint32_t now_us;
extern std::map<int, int> pin_mode, pin_level;
extern int reset_pulses;          // falling edges seen on the mux reset pin
extern int reset_pin;
}

inline unsigned long micros() { return mock::now_us; }
inline unsigned long millis() { return mock::now_us / 1000; }
inline void delayMicroseconds(unsigned us) { mock::now_us += us; }
inline void delay(unsigned ms) { mock::now_us += ms * 1000; }
inline void pinMode(int p, int m) {
    mock::pin_mode[p] = m;
    if (m == INPUT_PULLUP || !mock::pin_level.count(p)) mock::pin_level[p] = HIGH;  // released line floats high
}
inline void digitalWrite(int p, int v) {
    if (p == mock::reset_pin && mock::pin_level[p] == HIGH && v == LOW) ++mock::reset_pulses;
    mock::pin_level[p] = v;
}
inline int digitalRead(int p) { return mock::pin_level.count(p) ? mock::pin_level[p] : HIGH; }
