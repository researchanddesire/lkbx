#ifndef LOCKBOX_HELD_LOCK_H
#define LOCKBOX_HELD_LOCK_H

#include <stdint.h>

// The check that follows every motor movement (LKBX::startPreflight) reads the
// paddle and backplate sensors once. It also runs each time the box restores
// a lock it already holds: after a restart, a wake from sleep, and whenever
// the dashboard's answer sends the box through its locking pages again.
//
// A failed check used to open the paddle and show an error page with Retry,
// whatever the box had been doing. That is right for a lock being started:
// the backplate is off or the paddle is blocked, and the wearer has to be
// able to fix it. For a lock the box had already confirmed it opened a locked
// session on one bad sensor reading.
//
// So the box remembers that it holds a confirmed lock (`lockbox/lockHeld`):
// set when the check passes with the box locked, and cleared whenever the
// paddle is told to open (a break, an unlock, an offer, an emergency unlock,
// an error page) or the stored lock is cleared. While it is set, a failed
// check never opens the paddle: the motor presses it home again and the box
// goes to its locked page.

enum class Preflight : uint8_t {
    // Locked, and the sensors agree: the lock is confirmed.
    LOCK_CONFIRMED,
    // Locked and confirmed before, but the sensors disagree now: stay closed.
    KEEP_CLOSED,
    // A lock that was never confirmed could not close.
    CANNOT_LOCK,
    NO_BACKPLATE,
    // Not locked, and the paddle is open.
    PADDLE_OPEN,
    // Not locked, but the paddle still reads closed.
    CANNOT_UNLOCK,
};

inline Preflight decidePreflight(bool locked, bool paddleLocked,
                                 bool backplateClosed, bool lockHeld) {
    if (!locked) {
        return paddleLocked ? Preflight::CANNOT_UNLOCK : Preflight::PADDLE_OPEN;
    }
    if (paddleLocked && backplateClosed) return Preflight::LOCK_CONFIRMED;
    if (lockHeld) return Preflight::KEEP_CLOSED;
    return paddleLocked ? Preflight::NO_BACKPLATE : Preflight::CANNOT_LOCK;
}

#endif  // LOCKBOX_HELD_LOCK_H
