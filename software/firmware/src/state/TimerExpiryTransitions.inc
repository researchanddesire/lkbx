// Shared production timer-expiry transitions, also exercised by native tests.
// Local countdowns release automatically. Managed sessions retain the wearer
// confirmation flow; both paths recheck deadline, pending and frozen state.
"locked.timer"_s + timerExpired[acceptLocalTimerUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,
"lockMenu"_s + timerExpired[acceptLocalTimerUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,
"lockMenu.sleep"_s + timerExpired[acceptLocalTimerUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.ready"_s + timerExpired[acceptLocalTimerUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.ready.addTime"_s + timerExpired[acceptLocalTimerUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.break"_s + timerExpired[acceptLocalTimerUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.timer"_s + timerExpired[isReadyToUnlock] = "locked.ready"_s,
"lockMenu"_s + timerExpired[isReadyToUnlock] = "locked.ready"_s,
// A managed break re-locks once the lock window has ended, but not on a
// Chaster session: Chaster, the keyholder or the wearer ends that break. The
// unlock timer of Ready to unlock and Add time keeps sending TimerExpired, and
// one that lands just after a dashboard break has opened the box would
// otherwise close it again at once.
"locked.break"_s + timerExpired[isReadyToUnlock && !isBreakEndRequested && !isChasterSession] / (clearActiveBreak, movePaddleToLock) = "locking"_s,
"lockMenu.sleep"_s + timerExpired[isReadyToUnlock] = "locked.ready"_s,
