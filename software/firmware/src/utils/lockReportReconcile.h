#ifndef LOCKBOX_LOCK_REPORT_RECONCILE_H
#define LOCKBOX_LOCK_REPORT_RECONCILE_H

#include <stdint.h>
#include <string.h>

// The accept report is the only message that tells the server when a lock
// began. When it is lost, the server keeps the session as an offer, or lets a
// later break report move it to locked without dates, and from then on answers
// every state pull with an end time of 0. The box witnessed the accept and
// holds the deadline, so the box is the one that can repair the server: it
// carries its own dates in every report, and repeats the report whenever a
// pull shows the server never recorded the lock.

inline bool serverStateHoldsLock(const char* state) {
    return strcmp(state, "locked") == 0 || strcmp(state, "break") == 0 ||
           strcmp(state, "break_ending") == 0;
}

// Whether the server's answer shows it never recorded a lock the box holds
// for the same session: the session is still an offer there, or it is locked
// with no end time. Only a plainly locked box repeats its report; a box on a
// break must not, since a LOCKED report ends the break on the server.
inline bool serverMissedLock(bool sameSession, const char* localState,
                             bool localOnBreak, uint64_t localLockEnd,
                             const char* serverState, uint64_t serverLockEnd) {
    if (!sameSession || localOnBreak || localLockEnd == 0) return false;
    if (strcmp(localState, "locked") != 0) return false;
    if (strcmp(serverState, "pending") == 0) return true;
    return serverStateHoldsLock(serverState) && serverLockEnd == 0;
}

// Whether a sync must keep the deadline the box already holds: same session,
// still locked on the server, but the server carries no end time. A new
// session without an end time is a permanent lock and keeps the server's 0.
inline bool keepLocalDeadline(bool sameSession, const char* serverState,
                              uint64_t serverLockEnd, uint64_t localLockEnd) {
    return sameSession && localLockEnd != 0 && serverLockEnd == 0 &&
           serverStateHoldsLock(serverState);
}

#endif  // LOCKBOX_LOCK_REPORT_RECONCILE_H
