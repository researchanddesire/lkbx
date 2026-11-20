#ifndef LOCKBOX_LOCK_MENU_COUNTDOWN_H
#define LOCKBOX_LOCK_MENU_COUNTDOWN_H

// The lock menu shows no time, but the session's countdown task keeps running
// behind it: at the deadline the task releases a local timer, or moves a
// session to Ready to unlock. The task of the page the menu was opened from
// carries on, but a page that is not a countdown page ends it (the break
// pages, "too soon for a break", the emergency page, an answer from Chaster),
// and the menu entered from there had nothing counting: it never moved on at
// the deadline.
//
// So the menu starts the countdown again when it is entered without one. Not
// for a session that is already ready to unlock, though: the countdown would
// send the wearer straight back to Ready to unlock, and the menu, with its
// emergency unlock, could no longer be reached from the page that says
// Chaster refused. A local timer that has run out is released at once
// instead, as it is everywhere else.
//
// No Arduino dependencies: unit-tested natively (test/test_timer_expiry).
inline bool lockMenuCountsDown(bool lockedTimer, bool localPlay,
                               bool readyToUnlock) {
    if (!lockedTimer) return false;
    return localPlay || !readyToUnlock;
}

#endif  // LOCKBOX_LOCK_MENU_COUNTDOWN_H
