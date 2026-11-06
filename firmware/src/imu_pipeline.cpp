// Per-IMU 1 kHz attitude pipeline — Revision 57.0. See imu_pipeline.h.
#include "imu_pipeline.h"
#include <Arduino.h>
#include <math.h>

using namespace icm42688;

ImuStatus ImuChannel::begin(uint32_t now_us) {
    const ImuStatus st = dev_.begin();
    have_tmst_ = false;
    clk_armed_ = false;
    still_.reset();
    (void)now_us;
    // Stamp AFTER dev_.begin(): it blocks ~52 ms, and a pre-delay stamp made the
    // very next service() see "no data for 50 ms" and re-initialise again.
    last_record_us_ = static_cast<uint32_t>(micros());
    last_status_ok_ = (st == ImuStatus::OK);
    publish(now_us);
    return st;
}

void ImuChannel::process(const ImuRecord& r, bool gap) {
    // ---- dt from device timestamps -------------------------------------
    uint32_t dt_dev = ODR_PERIOD_US;
    const uint16_t dtick = static_cast<uint16_t>(r.tmst - last_tmst_);
    // FIFO holds 102 ms but the 16-bit timestamp wraps at 65.5 ms, so a delta
    // across an overflow is ambiguous: fall back to the nominal period there.
    const bool ok_delta = have_tmst_ && !gap && dtick > ODR_PERIOD_US / 2 && dtick < 8 * ODR_PERIOD_US;
    if (ok_delta) {
        dt_dev = dtick;
        if (clk_armed_) clk_dev_ticks_ += dtick;
    } else {
        clk_armed_ = false;            // restart the clock-ratio window
        if (have_tmst_) gap_since_publish_ = true;
    }
    have_tmst_ = true;
    last_tmst_ = r.tmst;
    const float dt = static_cast<float>(dt_dev) * 1e-6f * clock_scale_;

    // ---- temperature-compensated gyro bias -----------------------------
    const float t_f = bias_.filterTemp(r.temp_c, dt);
    bias_.bias(t_f, bias_now_);
    float g_s[3];
    for (int i = 0; i < 3; ++i) g_s[i] = r.gyro[i] - bias_now_[i];

    predict_pending_s_ += dt;
    StillnessDetector::Window w;
    if (still_.push(r.gyro, r.accel, t_f, w)) {
        bias_.predict(predict_pending_s_);
        predict_pending_s_ = 0.0f;
        // A commanded slow pan is indistinguishable from bias to the IMU alone.
        if (!learn_inhibit_.load(std::memory_order_relaxed) && bias_.update(w) > 0) publishModel();
    } else if (predict_pending_s_ >= 1.0f) {
        bias_.predict(predict_pending_s_);
        predict_pending_s_ = 0.0f;
    }

    // ---- body frame + AHRS ---------------------------------------------
    mount_.apply(g_s, rate_b_);
    mount_.apply(r.accel, accel_b_);
    ahrs_.update(rate_b_[0], rate_b_[1], rate_b_[2], accel_b_[0], accel_b_[1], accel_b_[2], dt);
    ++samples_;
}

size_t ImuChannel::service(uint32_t now_us) {
    if (dev_.status() != ImuStatus::OK) {
        // Retry a missing / faulted IMU at the no-data cadence, never faster.
        if (now_us - last_record_us_ >= cfg_.no_data_reinit_us) {
            if (reinits_ < 255) ++reinits_;
            begin(now_us);
        }
        return 0;
    }

    ImuRecord rec[ICM42688::MAX_BATCH];
    size_t total = 0;
    uint16_t remaining = 0;
    // Bounded drain: at most 4 bursts (64 records = 64 ms of backlog) per call,
    // so one call never monopolises the task; the rest is read next tick.
    for (int pass = 0; pass < 4; ++pass) {
        remaining = 0;
        const size_t n = dev_.readFifo(rec, ICM42688::MAX_BATCH, &remaining);
        const bool gap = dev_.takeResync();
        if (gap) gap_since_publish_ = true;
        for (size_t k = 0; k < n; ++k) process(rec[k], gap && k == 0);
        total += n;
        if (n == 0 || remaining == 0) break;
    }

    if (total > 0) {
        last_record_us_ = now_us;
        // ---- device clock ratio (host us per device us) --------------------
        // Only open/close the window when the FIFO is drained: then the newest
        // record is < 1 ODR period old and now_us is a fair stamp for it. With a
        // backlog the stamp is up to 64+ ms late (Rev 57 review: ~2 % error).
        if (remaining != 0) {
            // keep the window as is; it is closed on a later drained call
        } else if (!clk_armed_) {
            clk_armed_ = true;
            clk_dev_ticks_ = 0;
            clk_host0_ = now_us;
        } else if (static_cast<float>(clk_dev_ticks_) >= cfg_.clock_window_s * 1e6f) {
            const float ratio = static_cast<float>(now_us - clk_host0_) / static_cast<float>(clk_dev_ticks_);
            if (fabsf(ratio - 1.0f) <= cfg_.clock_tol) {
                clock_scale_ = clock_locked_ ? clock_scale_ + 0.25f * (ratio - clock_scale_) : ratio;
                clock_locked_ = true;
            }
            clk_dev_ticks_ = 0;
            clk_host0_ = now_us;
        }
    } else if (now_us - last_record_us_ >= cfg_.no_data_reinit_us) {
        // Device stopped producing data (brown-out reset, CS glitch): re-initialise.
        if (reinits_ < 255) ++reinits_;
        begin(now_us);
        return 0;
    }
    publish(now_us);
    return total;
}

void ImuChannel::publish(uint32_t now_us) {
    AttitudeState s{};
    const Quat& q = ahrs_.quat();
    s.q[0] = q.w; s.q[1] = q.x; s.q[2] = q.y; s.q[3] = q.z;
    s.roll = ahrs_.roll();
    s.pitch = ahrs_.pitch();
    s.yaw = ahrs_.yaw();
    for (int i = 0; i < 3; ++i) {
        s.rate[i] = rate_b_[i];
        s.accel[i] = accel_b_[i];
        s.gyro_bias[i] = bias_now_[i];
    }
    s.temp_c = bias_.filteredTemp();
    s.accel_weight = ahrs_.accelWeight();
    s.clock_scale = clock_scale_;
    s.t_us = last_record_us_;
    (void)now_us;
    s.samples = samples_;
    s.overflows = dev_.overflows();
    uint16_t f = 0;
    if (ahrs_.initialized() && ahrs_.time() >= cfg_.ahrs.init_time_s) f |= IMU_FLAG_ALIGNED;
    if (bias_.updates() > 0) f |= IMU_FLAG_BIAS_LEARNED;
    if (bias_.tempSpan() >= cfg_.slope_span_c) f |= IMU_FLAG_SLOPE_LEARNED;
    if (still_.lastStill()) f |= IMU_FLAG_STILL;
    if (ahrs_.accelWeight() < 1.0f) f |= IMU_FLAG_ACCEL_GATED;
    if (gap_since_publish_) f |= IMU_FLAG_GAP;
    if (clock_locked_) f |= IMU_FLAG_CLOCK_LOCKED;
    gap_since_publish_ = false;
    s.flags = f;
    s.status = static_cast<uint8_t>(dev_.status());
    s.reinits = reinits_;
    box_.write(s);
}

void ImuChannel::publishModel() {
    GyroTempModel m;
    bias_.exportModel(m);
    model_box_.write(m);
}

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static_assert(configTICK_RATE_HZ == 1000, "IMU task paces itself with a 1-tick vTaskDelayUntil: needs a 1 kHz FreeRTOS tick");

namespace {
struct ImuTaskArgs {
    ImuChannel* const* ch;
    uint8_t n;
};
ImuTaskArgs g_imu_args;

void imu_task(void* arg) {
    const ImuTaskArgs* a = static_cast<const ImuTaskArgs*>(arg);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        for (uint8_t i = 0; i < a->n; ++i) a->ch[i]->service(static_cast<uint32_t>(micros()));
        // 1 ms period. If we overran (re-init), vTaskDelayUntil returns at once
        // and the FIFO timestamps keep dt exact regardless.
        if (xTaskDelayUntil(&last, 1) == pdFALSE) last = xTaskGetTickCount();
    }
}
}  // namespace

bool start_imu_task(ImuChannel* const* channels, uint8_t n, uint8_t core, uint8_t priority) {
    g_imu_args = ImuTaskArgs{channels, n};
    return xTaskCreatePinnedToCore(imu_task, "imu_spi", 6144, &g_imu_args, priority, nullptr, core) == pdPASS;
}
#endif
