/**
 * rt_seqlock.h — single-writer / multi-reader cross-core mailbox (Revision 9.0)
 *
 * Rev 9.0 splits the controller across both LX7 cores:
 *   core 0: 20 kHz FOC task (hard real-time, never blocks on a lock)
 *   core 1: encoder I2C sweep + IMU SPI + attitude MEKF + stabiliser
 * Every value that crosses the core boundary goes through one of these. A
 * mutex/queue would let a core-1 task holding the lock (or being preempted
 * while holding it) stall the FOC tick; a seqlock never blocks the writer and
 * a reader only retries while a write is literally in flight (< 1 us).
 *
 * The payload is stored as relaxed std::atomic<uint32_t> words so the pattern
 * is data-race-free under the C++ memory model (ThreadSanitizer-clean on the
 * host test); on Xtensa LX7 each word is a plain aligned 32-bit load/store and
 * the fences become MEMW. T must be trivially copyable.
 *
 * Exactly ONE writer per instance (enforced by design: each mailbox is owned
 * by one task on one core). Readers may be on either core.
 */
#ifndef RT_SEQLOCK_H
#define RT_SEQLOCK_H

#include <stdint.h>
#include <string.h>
#include <atomic>
#include <type_traits>

template <typename T>
class SeqLock {
    static_assert(std::is_trivially_copyable<T>::value, "SeqLock payload must be trivially copyable");

public:
    static constexpr size_t WORDS = (sizeof(T) + sizeof(uint32_t) - 1) / sizeof(uint32_t);

    /** Publish a new value. Wait-free. Single writer only. */
    void write(const T& v) {
        uint32_t buf[WORDS] = {};
        memcpy(buf, &v, sizeof(T));
        const uint32_t q = seq_.load(std::memory_order_relaxed);
        seq_.store(q + 1, std::memory_order_relaxed);          // odd: write in progress
        std::atomic_thread_fence(std::memory_order_release);
        for (size_t i = 0; i < WORDS; ++i) data_[i].store(buf[i], std::memory_order_relaxed);
        seq_.store(q + 2, std::memory_order_release);          // even: stable
    }

    /**
     * Copy the latest value into @p out. Returns false if nothing was ever
     * published. Retries (bounded by the writer's few-word critical section)
     * until it observes a consistent snapshot.
     */
    bool read(T& out) const {
        uint32_t buf[WORDS];
        uint32_t q1, q2;
        do {
            q1 = seq_.load(std::memory_order_acquire);
            for (size_t i = 0; i < WORDS; ++i) buf[i] = data_[i].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            q2 = seq_.load(std::memory_order_relaxed);
        } while ((q1 & 1u) || q1 != q2);
        if (q1 == 0) return false;
        memcpy(&out, buf, sizeof(T));
        return true;
    }

    /** Number of completed writes (0 = never published). */
    uint32_t version() const { return seq_.load(std::memory_order_acquire) >> 1; }

private:
    std::atomic<uint32_t> seq_{0};
    std::atomic<uint32_t> data_[WORDS] = {};
};

#endif  // RT_SEQLOCK_H
