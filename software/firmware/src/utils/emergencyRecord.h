#ifndef LOCKBOX_EMERGENCY_RECORD_H
#define LOCKBOX_EMERGENCY_RECORD_H

#include <stdint.h>
#include <string.h>

// What the box remembers about an emergency unlock, and what the dashboard's
// state may do to it. An emergency unlock opens the box at once, starts the
// 24 hour lockout and sends the EMERGENCY report, which makes the dashboard
// end the session. Three things went wrong when that report did not arrive
// (2026-11-16 rig run on R2):
//
//   - The dashboard still held the session as locked. A box that restarted
//     online took that answer as it takes any other and drove the paddle back
//     to locked, while the screen said "Emergency Unlocked".
//   - The report was sent once and never again.
//   - A dashboard that had ended the session meanwhile could not lift the
//     lockout of a box that came back online.
//
// So the box keeps, with the lockout, the session the emergency unlock ended
// (empty for a local session, which the dashboard never knew) and whether the
// report is still owed, which it is until the broker has acknowledged it.
//
// The rules:
//
//   1. A session this box ended by emergency unlock is never taken up again:
//      an answer or a sync that names it is not stored, does not move the
//      motor and does not offer it. The dashboard has evidently not applied
//      the report, so the report is owed again and sent again.
//
//   2. Once the dashboard is seen without that session as its active one (it
//      reports no session, or another one), it has ended it, and the box
//      forgets the session's name. What that does to the lockout depends on
//      why the dashboard ended it:
//        - The report was still owed: the dashboard ended the session by
//          itself and has never heard of the emergency. The lockout is
//          lifted, as it is for a box that is online when its session is
//          ended, and the report is dropped.
//        - The broker had acknowledged the report: the dashboard ended the
//          session because of it, and it answers every emergency unlock that
//          way. The lockout stays. If it were lifted here, no lockout would
//          outlast the report that announces it.
//
//   3. A lockout that belongs to no dashboard session (a local session, or
//      one already settled by rule 2) is the box's own. The dashboard
//      reporting no session says nothing about it: it runs out after its 24
//      hours or ends when a new lock is accepted on the box.
//
// This is the decision only; services read and write the record
// (utils/emergencyStore.h). No Arduino dependencies, so it is unit-tested
// natively. See test/test_emergency_record.

// Whether the session the dashboard names is the one this box ended by
// emergency unlock (rule 1).
inline bool isEmergencyEndedSession(const char *endedSessionId,
                                    const char *incomingSessionId) {
    if (endedSessionId == nullptr || endedSessionId[0] == '\0' ||
        incomingSessionId == nullptr) {
        return false;
    }
    return strcmp(endedSessionId, incomingSessionId) == 0;
}

enum class EmergencySettle : uint8_t {
    // No dashboard session is on record: nothing changes (rule 3).
    NONE,
    // The dashboard ended the session by itself: the lockout is lifted, the
    // report is dropped and the session's name forgotten.
    LIFT_LOCKOUT,
    // The dashboard ended the session in answer to the box's report: the
    // lockout stays, and the session's name is forgotten.
    KEEP_LOCKOUT,
};

// The dashboard is seen without the emergency-ended session as its active
// one (rule 2). `reportOwed`: the broker has not acknowledged the report.
inline EmergencySettle settleEmergencySession(const char *endedSessionId,
                                              bool reportOwed) {
    if (endedSessionId == nullptr || endedSessionId[0] == '\0') {
        return EmergencySettle::NONE;
    }
    return reportOwed ? EmergencySettle::LIFT_LOCKOUT
                      : EmergencySettle::KEEP_LOCKOUT;
}

// Whether the unlock that follows a dashboard clear may go ahead. It may not
// while a lockout is in effect that the clear did not lift: the lockout page
// stays, and the paddle is open already.
inline bool clearEndsLockout(bool lockoutActive, EmergencySettle settle) {
    return !lockoutActive || settle == EmergencySettle::LIFT_LOCKOUT;
}

// A local session is the box's own. At boot an online, paired box asks the
// dashboard for its session, and the dashboard, which has none for a local
// session, answers "no session": that used to clear the stored lock and open
// the box before its timer had run out (2026-11-16 rig run on R2, twice). A
// box that holds a local lock does not ask; it goes on to its saved lock, as
// it does when it starts offline.
inline bool bootKeepsLocalSession(bool localPlay, bool locked) {
    return localPlay && locked;
}

#endif  // LOCKBOX_EMERGENCY_RECORD_H
