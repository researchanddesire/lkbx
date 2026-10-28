#ifndef LOCKBOX_SYNC_EFFECT_H
#define LOCKBOX_SYNC_EFFECT_H

#include <stdint.h>
#include <string.h>

enum class SyncEffect : int8_t {
    NONE, TIMER_META, MOTOR_LOCK, MOTOR_UNLOCK, OFFER, BREAK,
};

// Runtime dispatch only. Boot still restores the saved physical state through
// preflight. Repeated reports from an accepted, locked session must not replay
// that physical transition; returning from a break must still re-lock.
inline SyncEffect classifySyncEffect(bool sameSession, const char* savedState,
                                     const char* incomingState,
                                     bool displayChanged) {
    if (strcmp(incomingState, "pending") == 0) return SyncEffect::OFFER;
    if (strcmp(incomingState, "unlocked") == 0) return SyncEffect::MOTOR_UNLOCK;
    if (strcmp(incomingState, "break") == 0 ||
        strcmp(incomingState, "break_ending") == 0) return SyncEffect::BREAK;
    if (strcmp(incomingState, "locked") != 0) return SyncEffect::NONE;
    if (sameSession && strcmp(savedState, "locked") == 0)
        return displayChanged ? SyncEffect::TIMER_META : SyncEffect::NONE;
    return SyncEffect::MOTOR_LOCK;
}

#endif
