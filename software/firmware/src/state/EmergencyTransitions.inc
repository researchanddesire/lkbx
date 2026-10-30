// Production Boost.SML rows, included inside make_transition_table in LKBX.h
// and in the native fixture with hardware actions replaced by counters.
// The emergency block ends on the clock only. While it holds, the blocked
// screen and its Wi-Fi screen decline every offer and ignore remote unlocks
// and locks, and the accept screen declines instead of locking; once the
// stored end has passed the same events behave as before. A block that cannot
// be read matches neither guard: the event is ignored and retried later.
"emergency.blocked"_s + lockOffered[isEmergencyKnownHeld] / declineLockAction,
"emergency.blocked"_s + lockOffered[!isEmergencyHeld] / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"emergency.blocked"_s + unlock[!isEmergencyHeld] / (clearEBlock, clearLocalPlay, unlockAction) = "home"_s,
"emergency.blocked"_s + lock[!isEmergencyHeld] / (clearEBlock, clearLocalPlay, lockAction) = "locking"_s,
"emergency.blocked"_s + locktimer[!isEmergencyHeld] / (clearEBlock, clearLocalPlay, lockTimerAction) = "locking"_s,
"emergency.wifi"_s + lockOffered[isEmergencyKnownHeld] / declineLockAction,
"emergency.wifi"_s + lockOffered[!isEmergencyHeld] / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"emergency.wifi"_s + unlock[!isEmergencyHeld] / (clearEBlock, clearLocalPlay, unlockAction) = "home"_s,
"emergency.wifi"_s + lock[!isEmergencyHeld] / (clearEBlock, clearLocalPlay, lockAction) = "locking"_s,
"emergency.wifi"_s + locktimer[!isEmergencyHeld] / (clearEBlock, clearLocalPlay, lockTimerAction) = "locking"_s,
"acceptLock"_s + buttonPress[isEmergencyKnownHeld] / declineLockAction = "home"_s,
