#ifndef LOCKBOX_BREAK_TIMING_H
#define LOCKBOX_BREAK_TIMING_H

#include <cstdint>

// Pure break-timing helpers, factored out of startBreakTimerTask so the logic
// can be unit-tested natively (no hardware). See test/test_break.

// Wait one regularity period from lock start before the first break, then
// from the end of the most recent break. Clamp elapsed cooldowns for display.
inline uint64_t breakCooldownRemaining(uint64_t breakLast, uint64_t lockStart,
                                       uint64_t breakReg, uint64_t now) {
    const uint64_t anchor = breakLast > 0 ? breakLast : lockStart;
    const uint64_t availableAt = anchor + breakReg;
    return now < availableAt ? availableAt - now : 0;
}

// Decide whether to resume a previously-started break (vs. start a fresh one)
// after a wake-from-sleep. A break is resumable whenever it was active and we
// have a persisted start time -- EVEN IF the break window has already elapsed,
// in which case the countdown is negative and the UI shows the "you are over
// your break" screen.
//
// NOTE: intentionally does NOT check expiry. A stale `now < breakStart +
// breakMax` clause here was the cause of the "break restarts at full time on
// wake" bug; it mirrors the same clause removed from isOnActiveBreak in 3649fb9.
inline bool shouldResumeBreak(bool wasActiveBreak, uint64_t savedBreakStart) {
    return wasActiveBreak && savedBreakStart > 0;
}

// Milliseconds remaining in the break window. Negative means the user is over
// their break (drives the red "YOU ARE LATE" screen).
inline int64_t breakTimeRemaining(uint64_t breakStart, uint64_t breakMax,
                                  uint64_t now) {
    return static_cast<int64_t>(breakStart + breakMax) -
           static_cast<int64_t>(now);
}

// Whether the entire lock window has ended (now is strictly past lockEnd). Used
// as a defense-in-depth guard so the device won't (re)start or keep a break for
// a session whose lock has already finished -- even if the server still reports
// "break". A lockEnd of 0 means "unknown" and is never treated as over. Callers
// MUST gate on a trusted clock (isTimeSynced) before acting on this.
inline bool isLockWindowOver(uint64_t lockEnd, uint64_t now) {
    return lockEnd > 0 && now > lockEnd;
}

// Floor for the post-break cooldown (ms): how long after a break ends the device
// ignores a SERVER-pushed break. Larger than the server-propagation window so the
// stale-"break" echo of a just-ended break is caught even when breakReg is
// small/zero; small enough not to interfere with genuinely new breaks.
constexpr uint64_t kServerBreakCooldownFloorMs = 60000;

// True if a server-pushed break should be ignored because the device finished a
// break too recently (the stale echo of the one you just ended). breakLast == 0
// (no break recorded) never suppresses.
inline bool isWithinBreakCooldown(uint64_t breakLast, uint64_t cooldownMs,
                                  uint64_t now) {
    return breakLast > 0 && now < breakLast + cooldownMs;
}

// A server-pushed break is the stale echo of the break the device just ended
// only inside the fixed floor. The wearer's break interval (breakReg) must not
// widen that window: a keyholder may open the lock inside the interval, and
// timing is the only thing that tells a keyholder opening from an echo.
inline bool isServerBreakEcho(uint64_t breakLast, uint64_t now) {
    return isWithinBreakCooldown(breakLast, kServerBreakCooldownFloorMs, now);
}

// Outcome of the wearer pressing the break button.
enum class BreakPreflight : uint8_t {
    ALLOWED,
    NOT_ENABLED,
    KEYHOLDER_ONLY,
    TOO_SOON,
};

// Whether a wearer-initiated break may start. Keyholder-only is checked before
// the interval so the wearer never sees a countdown to a break they cannot
// take. Server-pushed breaks never go through this check.
inline BreakPreflight evaluateBreakPreflight(bool isBreak, bool keyholderOnly,
                                             uint64_t breakLast,
                                             uint64_t lockStart,
                                             uint64_t breakReg, uint64_t now) {
    if (!isBreak) return BreakPreflight::NOT_ENABLED;
    if (keyholderOnly) return BreakPreflight::KEYHOLDER_ONLY;
    if (breakCooldownRemaining(breakLast, lockStart, breakReg, now) > 0) {
        return BreakPreflight::TOO_SOON;
    }
    return BreakPreflight::ALLOWED;
}

#endif  // LOCKBOX_BREAK_TIMING_H
