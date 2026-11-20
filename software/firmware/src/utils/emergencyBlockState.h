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

enum class EmergencyBlockHold { Free, Held, Unknown };

// Whether a software emergency block is set and has not yet reached its
// stored end on the box's wall clock (the clock the countdown task uses).
// Unknown when storage cannot be read: callers that refuse (clearing the
// block, taking an unlock or a lock) treat it as held, callers that would
// act on the block (declining an offer) wait for a readable answer. No block
// set means Free, so a box that still carries an old end from a block cleared
// by earlier firmware is not affected.
inline EmergencyBlockHold emergencyBlockHoldNow() {
    EmergencyBlockState state;
    if (!readEmergencyBlock(&state)) return EmergencyBlockHold::Unknown;
    if (!state.blocked) return EmergencyBlockHold::Free;
    const int64_t now = currentWallClockMs();
    return emergencyBlockExpired(now > 0 ? (uint64_t)now : 0, state.endMs)
               ? EmergencyBlockHold::Free
               : EmergencyBlockHold::Held;
}

// Safe-side answer: anything but a readable Free keeps the block.
inline bool emergencyBlockHeldNow() {
    return emergencyBlockHoldNow() != EmergencyBlockHold::Free;
}

// Readable and running: the only answer that justifies acting on the block.
inline bool emergencyBlockKnownHeldNow() {
    return emergencyBlockHoldNow() == EmergencyBlockHold::Held;
}

#endif  // LOCKBOX_EMERGENCY_BLOCK_STATE_H
