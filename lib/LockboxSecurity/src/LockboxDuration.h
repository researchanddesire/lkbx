#pragma once

#include <cstdint>
#include <limits>

namespace lockbox::security {

enum class DurationUnit { Seconds, Minutes, Hours, Days, Weeks, Months, Years };

constexpr uint64_t millisecondsPerUnit(DurationUnit unit) {
    switch (unit) {
        case DurationUnit::Seconds: return 1000ULL;
        case DurationUnit::Minutes: return 60000ULL;
        case DurationUnit::Hours: return 3600000ULL;
        case DurationUnit::Days: return 86400000ULL;
        case DurationUnit::Weeks: return 604800000ULL;
        case DurationUnit::Months: return 2629746000ULL;
        case DurationUnit::Years: return 31556952000ULL;
    }
    return 0;
}

// Preserve the timer selector's existing maximum of 1000 years.
constexpr uint64_t MAX_TIMER_DURATION_MS = 1000ULL * 31556952000ULL;

inline bool durationMilliseconds(uint64_t count, DurationUnit unit,
                                 uint64_t& result) {
    const uint64_t multiplier = millisecondsPerUnit(unit);
    if (multiplier == 0 || count > MAX_TIMER_DURATION_MS / multiplier) {
        return false;
    }
    result = count * multiplier;
    return true;
}

inline bool localTimerDeadline(int64_t wallMs, uint64_t durationMs,
                               uint64_t& result) {
    // Epoch zero remains valid for a local timer created before clock sync.
    if (wallMs < 0 || durationMs == 0 || durationMs > MAX_TIMER_DURATION_MS) {
        return false;
    }
    const uint64_t now = static_cast<uint64_t>(wallMs);
    if (durationMs > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - now) {
        return false;
    }
    result = now + durationMs;
    return true;
}

}  // namespace lockbox::security
