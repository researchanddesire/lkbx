#pragma once
#include <cstdint>

namespace lockbox::security {
// TLS needs a calendar hint on cold boot. Hide that hint from application
// deadlines until the authenticated server supplies the authoritative date.
class ClockAuthority {
 public:
    bool begin(int64_t wallMs, int64_t monotonicMs) {
        if (active_) return false;
        wall_ = wallMs;
        monotonic_ = monotonicMs;
        active_ = true;
        return true;
    }
    int64_t read(int64_t systemWallMs, int64_t monotonicMs) const {
        return active_ ? wall_ + (monotonicMs - monotonic_) : systemWallMs;
    }
    bool active() const { return active_; }
    void finish() { active_ = false; }
 private:
    bool active_ = false;
    int64_t wall_ = 0;
    int64_t monotonic_ = 0;
};
}  // namespace lockbox::security
