// Host-side I2C bus model: one TCA9548A (0x70) and up to 8 AS5600 (all 0x36),
// one per downstream channel. Devices on every *enabled* channel respond to
// 0x36 simultaneously and their SDA bits are wired-AND together — exactly the
// Rev <= 6.0 address-conflict failure mode when the mux is bypassed.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "Arduino.h"

struct SimAS5600 {
    bool     present = true;
    bool     nack = false;             // fault injection: device stops ACKing
    bool     auto_increment = false;   // non-conforming variant: pointer advances past 0x0D
    uint8_t  status = 0x20;            // MD=1
    uint8_t  agc = 0x80;
    uint16_t raw = 0;
    uint8_t  ptr = 0;
    uint8_t reg(uint8_t r) const {
        switch (r) {
            case 0x0B: return status;
            case 0x0C: case 0x0E: return (raw >> 8) & 0x0F;
            case 0x0D: case 0x0F: return raw & 0xFF;
            case 0x1A: return agc;
            default:   return 0x00;
        }
    }
    uint8_t readNext() {
        uint8_t v = reg(ptr);
        if (!auto_increment && (ptr == 0x0D || ptr == 0x0F || ptr == 0x1C)) ptr = ptr - 1;  // datasheet: wraps to high byte
        else ++ptr;
        return v;
    }
};

class TwoWire {
public:
    // ---- bus model ----
    bool      mux_present = true;
    bool      mux_nack = false;
    uint8_t   mux_mask = 0;
    SimAS5600 enc[8];
    uint32_t  hz = 0;
    bool      begun = false;
    uint64_t  bits = 0;             // bits clocked, for timing estimates
    uint32_t  mux_writes = 0, begins = 0, ends = 0, transactions = 0;
    int       reset_pin = -1;

    // ---- Arduino API ----
    bool begin(int, int, uint32_t f) { hz = f; begun = true; ++begins; return true; }
    void end() { begun = false; ++ends; }
    void setTimeOut(uint16_t) {}
    void beginTransmission(uint8_t a) { addr_ = a; tx_.clear(); }
    size_t write(uint8_t b) { tx_.push_back(b); return 1; }
    uint8_t endTransmission(bool = true) {
        ++transactions;
        tick(1 + 9 + 9 * tx_.size() + 1);
        applyResetPin();
        if (addr_ == 0x70 && mux_present && !mux_nack) {
            if (!tx_.empty()) { mux_mask = tx_.back(); ++mux_writes; }
            return 0;
        }
        if (addr_ == 0x36) {
            auto devs = visible();
            if (devs.empty()) return 2;  // address NACK
            for (auto* d : devs) if (!tx_.empty()) d->ptr = tx_[0];
            return 0;
        }
        return 2;
    }
    uint8_t requestFrom(uint8_t a, uint8_t n) {
        ++transactions;
        rx_.clear(); rx_pos_ = 0;
        applyResetPin();
        if (a == 0x70 && mux_present && !mux_nack) {
            tick(1 + 9 + 9 * n + 1);
            for (uint8_t i = 0; i < n; ++i) rx_.push_back(mux_mask);
            return n;
        }
        if (a == 0x36) {
            auto devs = visible();
            if (devs.empty()) { tick(11); return 0; }
            tick(1 + 9 + 9 * n + 1);
            for (uint8_t i = 0; i < n; ++i) {
                uint8_t v = 0xFF;
                for (auto* d : devs) v &= d->readNext();  // open-drain wired-AND
                rx_.push_back(v);
            }
            return n;
        }
        tick(11);
        return 0;
    }
    int read() { return rx_pos_ < rx_.size() ? rx_[rx_pos_++] : -1; }

    /** Upstream view: AS5600s on enabled channels (all 8 if there is no mux). */
    std::vector<SimAS5600*> visible() {
        std::vector<SimAS5600*> v;
        for (int c = 0; c < 8; ++c) {
            bool on = mux_present ? ((mux_mask >> c) & 1) : true;
            if (on && enc[c].present && !enc[c].nack) v.push_back(&enc[c]);
        }
        return v;
    }
    double busTimeUs() const { return hz ? bits * 1e6 / hz : 0; }

private:
    void tick(uint64_t n) { bits += n; if (hz) mock::now_us += (uint32_t)((n * 1000000ull + hz - 1) / hz); }
    void applyResetPin() {
        if (mock::reset_pulses != last_resets_) { mux_mask = 0; last_resets_ = mock::reset_pulses; }
    }
    uint8_t addr_ = 0;
    std::vector<uint8_t> tx_, rx_;
    size_t rx_pos_ = 0;
    int last_resets_ = 0;
};
