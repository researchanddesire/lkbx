#ifndef LOCKBOX_EMERGENCY_BLOCK_STATE_H
#define LOCKBOX_EMERGENCY_BLOCK_STATE_H

#include <stdint.h>

#include "constants/PerferenceKeys.h"
#include "services/settings.h"
#include "utils/clockCorrection.h"
#include "utils/emergencyBlock.h"

struct EmergencyBlockState {
    bool blocked = false;
    uint64_t endMs = 0;
};

// Reads the stored block flag and end. False when storage cannot be read.
inline bool readEmergencyBlock(EmergencyBlockState* out) {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) {
        return false;
    }
    preferences.begin(Keys::base);
    out->blocked = preferences.getBool(Keys::Base::isEBlocked, false);
    out->endMs = preferences.getULong64(Keys::Base::emergencyEnd, 0);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    return true;
}

// Whether a software emergency block is set and has not yet reached its
// stored end on the box's wall clock (the clock the countdown task uses).
// Storage that cannot be read keeps the block: the safe answer for a state
// that exists to refuse. No block set means nothing is held, so a box that
// still carries an old end from a block cleared by earlier firmware is not
// affected.
inline bool emergencyBlockHeldNow() {
    EmergencyBlockState state;
    if (!readEmergencyBlock(&state)) return true;
    if (!state.blocked) return false;
    const int64_t now = currentWallClockMs();
    return !emergencyBlockExpired(now > 0 ? (uint64_t)now : 0, state.endMs);
}

#endif  // LOCKBOX_EMERGENCY_BLOCK_STATE_H
