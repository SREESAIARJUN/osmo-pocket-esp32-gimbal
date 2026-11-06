/**
 * imu_pipeline.h — per-IMU 1 kHz attitude pipeline (Revision 57.0)
 *
 *   ICM42688::readFifo()            20-bit records, sensor frame, device timestamp
 *     -> dt from timestamp deltas   x clock_scale (device RC/PLL vs ESP32 crystal)
 *     -> die-temp low-pass          GyroTempBias::filterTemp()
 *     -> gyro - b(T)                temperature bias model, sensor frame
 *     -> StillnessDetector          still windows feed GyroTempBias::update()
 *     -> axis remap                 signed permutation sensor -> body (PCB mounting)
 *     -> MadgwickAHRS::update()     gated accel correction
 *     -> SeqLock<AttitudeState>     lock-free hand-off to the FOC/stabiliser core
 *
 * One ImuChannel per ICM-42688-P (camera cradle, handle). Both are serviced
 * from one FreeRTOS task (shared SPI bus, single owner) paced at 1 kHz by
 * vTaskDelayUntil on core 0, priority above the encoder sweep task.
 *
 * Timing: every record carries the device's 16-bit 1 us timestamp, so dt is
 * the true sample spacing even when the task is late by a tick and drains 2+
 * records at once (the FIFO absorbs up to 102 ms of scheduling jitter). The
 * device clock ratio is measured against micros() over 2 s windows and applied
 * to dt, because the ICM's internal oscillator tolerance becomes a gyro-
 * integration scale error (1 % = 0.9 deg on a 90 deg pan).
 */
#ifndef IMU_PIPELINE_H
#define IMU_PIPELINE_H

#include <stdint.h>
#include "icm42688.h"
#include "madgwick_ahrs.h"
#include "imu_bias.h"
#include "rt_seqlock.h"

/** body[i] = sign(map[i]) * sensor[|map[i]| - 1]; map entries are +/-1..3. */
struct ImuMount {
    int8_t map[3];
    constexpr bool valid() const {
        return map[0] != 0 && map[1] != 0 && map[2] != 0 &&
               map[0] >= -3 && map[0] <= 3 && map[1] >= -3 && map[1] <= 3 && map[2] >= -3 && map[2] <= 3 &&
               abs8(map[0]) != abs8(map[1]) && abs8(map[1]) != abs8(map[2]) && abs8(map[0]) != abs8(map[2]) &&
               det() == 1;  // proper rotation: a mirrored mapping would flip the attitude handedness
    }
    void apply(const float s[3], float b[3]) const {
        for (int i = 0; i < 3; ++i) {
            const int k = (map[i] > 0 ? map[i] : -map[i]) - 1;
            b[i] = map[i] > 0 ? s[k] : -s[k];
        }
    }

private:
    static constexpr int abs8(int8_t v) { return v < 0 ? -v : v; }
    static constexpr int sgn(int8_t v) { return v < 0 ? -1 : 1; }
    // Determinant of the signed permutation matrix = sign product * permutation parity.
    constexpr int det() const {
        return sgn(map[0]) * sgn(map[1]) * sgn(map[2]) *
               ((abs8(map[0]) == 1 && abs8(map[1]) == 2) || (abs8(map[0]) == 2 && abs8(map[1]) == 3) ||
                (abs8(map[0]) == 3 && abs8(map[1]) == 1) ? 1 : -1);
    }
};

enum ImuFlags : uint16_t {
    IMU_FLAG_ALIGNED        = 1u << 0,  // AHRS initialised and past its start-up gain window
    IMU_FLAG_BIAS_LEARNED   = 1u << 1,  // >= 1 still window fused (b0 anchored)
    IMU_FLAG_SLOPE_LEARNED  = 1u << 2,  // temperature span >= 3 degC seen while still
    IMU_FLAG_STILL          = 1u << 3,  // last completed window was still
    IMU_FLAG_ACCEL_GATED    = 1u << 4,  // last sample's accel weight < 1
    IMU_FLAG_GAP            = 1u << 5,  // FIFO overflow / resync since last publish
    IMU_FLAG_CLOCK_LOCKED   = 1u << 6,  // device clock ratio measured
};

struct AttitudeState {
    float    q[4];            // w x y z, body -> earth
    float    roll, pitch, yaw;// rad
    float    rate[3];         // rad/s, body frame, bias-corrected
    float    accel[3];        // g, body frame
    float    gyro_bias[3];    // rad/s, sensor frame, b(T) currently subtracted
    float    temp_c;          // low-passed die temperature
    float    accel_weight;
    float    clock_scale;     // host us per device us
    uint32_t t_us;            // micros() when the newest sample was drained
    uint32_t samples;         // records processed since boot
    uint32_t overflows;
    uint16_t flags;
    uint8_t  status;          // ImuStatus
    uint8_t  reinits;
};

class ImuChannel {
public:
    struct Config {
        MadgwickAHRS::Config      ahrs;
        StillnessDetector::Config still;
        GyroTempBias::Config      bias;
        uint32_t no_data_reinit_us = 50000;   // re-run begin() after this long without a record
        float    clock_window_s    = 2.0f;
        float    clock_tol         = 0.05f;   // reject ratio outside 1 +/- 5 %
        float    slope_span_c      = 3.0f;
    };

    ImuChannel(ICM42688& dev, const ImuMount& mount, const Config& cfg)
        : dev_(dev), mount_(mount), cfg_(cfg), ahrs_(cfg.ahrs), still_(cfg.still), bias_(cfg.bias) {}

    /** Initialise the device. Safe to call again (re-init). */
    ImuStatus begin(uint32_t now_us);

    /** Restore a persisted temperature model; call before the task starts. */
    bool importBiasModel(const GyroTempModel& m, float age_s = 0.0f) {
        const bool ok = bias_.importModel(m, age_s);
        if (ok) publishModel();
        return ok;
    }

    /** Drain the FIFO, run the pipeline on every record, publish. Returns records processed. */
    size_t service(uint32_t now_us);

    /** Lock-free snapshot, callable from any core. */
    bool latest(AttitudeState& s) const { return box_.read(s); }
    bool latestModel(GyroTempModel& m) const { return model_box_.read(m); }

    // Direct access for the owning task / host tests only.
    const MadgwickAHRS& ahrs() const { return ahrs_; }
    const GyroTempBias& bias() const { return bias_; }
    float clockScale() const { return clock_scale_; }

private:
    void process(const ImuRecord& r, bool gap);
    void publish(uint32_t now_us);
    void publishModel();

    ICM42688&          dev_;
    ImuMount           mount_;
    Config             cfg_;
    MadgwickAHRS       ahrs_;
    StillnessDetector  still_;
    GyroTempBias       bias_;

    bool     have_tmst_ = false;
    uint16_t last_tmst_ = 0;
    float    clock_scale_ = 1.0f;
    bool     clock_locked_ = false;
    uint32_t clk_dev_ticks_ = 0;
    uint32_t clk_host0_ = 0;
    bool     clk_armed_ = false;
    float    predict_pending_s_ = 0.0f;
    uint32_t last_record_us_ = 0;
    uint32_t samples_ = 0;
    uint8_t  reinits_ = 0;
    bool     gap_since_publish_ = false;
    bool     last_status_ok_ = false;

    float    rate_b_[3] = {0, 0, 0};
    float    accel_b_[3] = {0, 0, 1};
    float    bias_now_[3] = {0, 0, 0};

    SeqLock<AttitudeState> box_;
    SeqLock<GyroTempModel> model_box_;
};

#if defined(ARDUINO_ARCH_ESP32)
/** Start the 1 kHz IMU task servicing @p n channels (shared SPI bus). */
bool start_imu_task(ImuChannel* const* channels, uint8_t n, uint8_t core, uint8_t priority);
#endif

#endif  // IMU_PIPELINE_H
