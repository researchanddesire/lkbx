#ifndef LOCKBOX_SERVER_CLEAR_H
#define LOCKBOX_SERVER_CLEAR_H

#include <string.h>

// A dashboard clear ends the session the box holds and unlocks it. The
// dashboard names the session that ended, and the box acts only on a clear for
// the session it holds: before, ending or deleting any of the owner's
// sessions, even a throwaway one, unlocked the box while a keyholder's lock
// was still running.
//
// Dashboards older than this firmware send a clear without a session id. The
// box does not act on it directly: it reads its state from the dashboard over
// HTTPS and applies that answer, so it unlocks only when the dashboard has no
// active session for it.
enum class ServerClear {
    APPLY,    // names the held session: clear it and unlock
    IGNORE,   // names another session: nothing changes
    CONFIRM,  // names no session: re-read the state from the dashboard
};

inline ServerClear classifyServerClear(const char* clearSessionId,
                                       const char* heldSessionId) {
    if (clearSessionId == nullptr || clearSessionId[0] == '\0') {
        return ServerClear::CONFIRM;
    }
    if (heldSessionId != nullptr && heldSessionId[0] != '\0' &&
        strcmp(clearSessionId, heldSessionId) == 0) {
        return ServerClear::APPLY;
    }
    return ServerClear::IGNORE;
}

#endif  // LOCKBOX_SERVER_CLEAR_H
