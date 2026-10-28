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
"locked.break"_s + timerExpired[isReadyToUnlock && !isBreakEndRequested] / (clearActiveBreak, movePaddleToLock) = "locking"_s,
"lockMenu.sleep"_s + timerExpired[isReadyToUnlock] = "locked.ready"_s,
