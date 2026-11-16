#ifndef LOCKBOX_COUNTDOWN_WATCH_H
#define LOCKBOX_COUNTDOWN_WATCH_H

#include <stdint.h>

// A locked page has a countdown task. It draws the time left, and at the
// deadline it does the things that matter: it releases a local timer, moves a
// session to Ready to unlock, or ends the emergency lockout. Its stack is one
// block of internal RAM, and on R8 that block is at times not there
// (2026-11-16 rig run: the task could not be created 22 times in a row, with
// at most 2.4 to 3.8 KB in one piece). It is tried again every two seconds,
// but for as long as it is missing nothing watched the deadline: a local timer
// would not release.
//
// So while the task is missing, the delay task, which always exists, looks at
// the deadline once a second and sends the event the countdown task would
// have sent (LKBX::serviceCountdownWatch). Only the drawing waits for the
// task.
//
// The task is not given a stack of its own for good instead: that would take
// about 5 KB of internal RAM from every box at all times, including during
// boot, when a few KB are all that is left for TLS.
//
// This is the decision only, the same one the countdown task makes on each
// pass (LKBX::startUnlockTimerTask). No Arduino dependencies, so it is
// unit-tested natively. See test/test_timer_expiry.

enum class CountdownDue : uint8_t {
    NOTHING,
    // The session's deadline has passed: TimerExpired. The state machine
    // releases a local timer and shows Ready to unlock for any other.
    TIMER_EXPIRED,
    // The emergency lockout has run out: Unlock.
    LOCKOUT_OVER,
    // On Ready to unlock with time on the clock again (it was added, or the
    // session is held): LockTimer, which goes back to the countdown.
    BACK_TO_COUNTDOWN,
};

struct CountdownView {
    // The emergency lockout is in effect.
    bool lockout = false;
    // The page is Ready to unlock or its Add time page.
    bool readyPage = false;
    // A Chaster freeze or unlock hold holds the session's countdown.
    bool frozen = false;
    // The lockout's end while it is in effect, the session's end otherwise.
    uint64_t endMs = 0;
    uint64_t nowMs = 0;
};

inline CountdownDue countdownDue(const CountdownView &view) {
    // A hold never applies to the lockout: emergency unlock keeps its own
    // clock.
    const bool held = view.frozen && !view.lockout;
    if (!held && view.endMs <= view.nowMs) {
        return view.lockout ? CountdownDue::LOCKOUT_OVER
                            : CountdownDue::TIMER_EXPIRED;
    }
    return view.readyPage ? CountdownDue::BACK_TO_COUNTDOWN
                          : CountdownDue::NOTHING;
}

#endif  // LOCKBOX_COUNTDOWN_WATCH_H
