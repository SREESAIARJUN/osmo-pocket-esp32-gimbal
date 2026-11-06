// joystick_input.cpp — Rev 59.0 ADC1 joystick task (see joystick_input.h)
#include "joystick_input.h"

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "gimbal_config.h"
#include "gimbal_pins.h"
#include "rt_seqlock.h"

static_assert(pins::is_adc1(pins::JOY_X) && pins::is_adc1(pins::JOY_Y),
              "joystick must be on ADC1 (ADC2 is arbitrated against Wi-Fi)");
static_assert(JOY_PERIOD_MS >= 1 && (1000 % JOY_PERIOD_MS) == 0, "JOY_PERIOD_MS must divide 1 s");

namespace {
SeqLock<joy::Command> g_cmd;
std::atomic<bool>     g_precision{false};
joy::Config           g_cfg;

float read_mv(int8_t pin) {
    // 4x oversampling: the S3 SAR ADC has ~6-8 LSB of code noise; averaging
    // halves it before the noise estimator sees it. ~4 x 12 us per axis.
    uint32_t acc = 0;
    for (int i = 0; i < JOY_OVERSAMPLE; ++i) acc += analogReadMilliVolts(pin);
    return (float)acc / (float)JOY_OVERSAMPLE;
}

void joystick_task(void*) {
    analogReadResolution(12);
    analogSetPinAttenuation(pins::JOY_X, ADC_11db);  // 0 .. ~3.1 V usable range
    analogSetPinAttenuation(pins::JOY_Y, ADC_11db);

    const TickType_t period = pdMS_TO_TICKS(JOY_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();

    // ---- boot centre calibration (thumb off the stick)
    joy::CenterCalibrator calib;
    for (int i = 0; i < JOY_CAL_SAMPLES; ++i) {
        calib.add(read_mv(pins::JOY_X), read_mv(pins::JOY_Y));
        xTaskDelayUntil(&last, period);
    }
    joy::CenterCalibrator::Limits lim;
    lim.nominal_mv    = JOY_CENTER_NOMINAL_MV;
    lim.max_offset_mv = JOY_CENTER_MAX_OFFSET_MV;
    lim.max_std_mv    = JOY_CENTER_MAX_STD_MV;
    joy::AxisCal cal[joy::AXES];
    const bool cal_ok = calib.result(g_cfg, lim, cal);
    Serial.printf("[JOY] centre %s: X=%.0f mV (sd %.1f)  Y=%.0f mV (sd %.1f)\n",
                  cal_ok ? "calibrated" : "REJECTED -> factory centre", calib.mean(0), calib.stddev(0),
                  calib.mean(1), calib.stddev(1));

    joy::Shaper shaper;
    shaper.configure(g_cfg, cal, !cal_ok);

    uint32_t t_prev = micros();
    for (;;) {
        const float x = read_mv(pins::JOY_X);
        const float y = read_mv(pins::JOY_Y);
        const uint32_t now = micros();
        const float dt = (float)(now - t_prev) * 1e-6f;
        t_prev = now;
        joy::Command c = shaper.update(x, y, dt, g_precision.load(std::memory_order_relaxed));
        c.t_us = now;
        g_cmd.write(c);
        // Overrun (flash write stalled the cache, etc.): resynchronise instead of bursting.
        if (xTaskDelayUntil(&last, period) == pdFALSE) last = xTaskGetTickCount();
    }
}
}  // namespace

bool start_joystick_task(const joy::Config& cfg, uint8_t core, uint8_t priority) {
    g_cfg = cfg;
    return xTaskCreatePinnedToCore(joystick_task, "joy_adc", 4096, nullptr, priority, nullptr, core) == pdPASS;
}

bool joystick_latest(joy::Command& out) { return g_cmd.read(out); }

void joystick_set_precision(bool on) { g_precision.store(on, std::memory_order_relaxed); }
bool joystick_precision() { return g_precision.load(std::memory_order_relaxed); }
