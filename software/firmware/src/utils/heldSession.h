#ifndef LOCKBOX_HELD_SESSION_H
#define LOCKBOX_HELD_SESSION_H

#include <string.h>

// While the box holds a locked session, only that session's own messages may
// change it. A sync for another session (or one that names none) arriving over
// MQTT would otherwise replace the held session with an offer and move the
// paddle to unlock: a wearer who reactivated an old self-lock on the dashboard
// opened the box under a keyholder's running lock that way.
//
// Such a sync is not applied. The box re-reads its state from the dashboard
// over HTTPS instead, which reports the one session it considers active, so a
// session that really ended while the box was offline is still replaced.
enum class HeldSessionSync {
    APPLY,    // the sync may change the stored session as before
    CONFIRM,  // re-read the state from the dashboard instead
};

// A locked session, including a break or a break being ended, keeps the
// wearer locked in: the box must not drop it for another one.
inline bool holdsLockedSession(const char* heldSessionId, const char* heldState) {
    if (heldSessionId == nullptr || heldSessionId[0] == '\0' ||
        heldState == nullptr) {
        return false;
    }
    return strcmp(heldState, "locked") == 0 || strcmp(heldState, "break") == 0 ||
           strcmp(heldState, "break_ending") == 0;
}

inline HeldSessionSync classifyHeldSessionSync(const char* incomingSessionId,
                                               const char* heldSessionId,
                                               const char* heldState) {
    if (!holdsLockedSession(heldSessionId, heldState)) return HeldSessionSync::APPLY;
    if (incomingSessionId != nullptr && strcmp(incomingSessionId, heldSessionId) == 0) {
        return HeldSessionSync::APPLY;
    }
    return HeldSessionSync::CONFIRM;
}

#endif  // LOCKBOX_HELD_SESSION_H
