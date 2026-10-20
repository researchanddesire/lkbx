#ifndef LOCKBOX_IDLE_TARGET_H
#define LOCKBOX_IDLE_TARGET_H

#include <cstdint>

#include "../services/lastInteraction.h"

inline bool deepSleepAllowed(bool powerConnected, uint64_t wakeDelayMs) {
    return !powerConnected && wakeDelayMs != 1;
}

inline IdleState idleTargetFromDelta(unsigned long delta, bool isCharging) {
    if (isCharging) {
        delta = (delta < PSEUDO_SLEEP_TIMEOUT) ? delta : PSEUDO_SLEEP_TIMEOUT;
    }
    if (delta >= SLEEP_TIMEOUT) {
        return IdleState::SLEEP;
    }
    if (delta >= PSEUDO_SLEEP_TIMEOUT) {
        return IdleState::PSEUDO_SLEEP;
    }
    if (delta >= IDLE_TIMEOUT) {
        return IdleState::IDLE;
    }
    return IdleState::NOT_IDLE;
}

#endif  // LOCKBOX_IDLE_TARGET_H
