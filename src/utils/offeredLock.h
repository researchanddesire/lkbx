#ifndef LOCKBOX_OFFERED_LOCK_H
#define LOCKBOX_OFFERED_LOCK_H
#include <stdint.h>

inline bool preserveOfferedDecision(bool sameSession, bool accepted, bool declined) {
    return sameSession && (accepted || declined);
}

inline uint64_t acceptedOfferDeadline(bool pending, uint64_t now, uint64_t duration) {
    constexpr uint64_t kEarliestValidClockMs = 1577836800000ULL; // 2020-01-01
    if (!pending || now < kEarliestValidClockMs || duration == 0 ||
        duration > UINT64_MAX - now) return 0;
    return now + duration;
}

inline bool offeredLockTimedOut(uint32_t started, uint32_t now) {
    return uint32_t(now - started) >= 5 * 60 * 1000;
}
#endif
