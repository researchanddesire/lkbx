// Production Boost.SML rows, included inside make_transition_table in LKBX.h
// (before the other "locked.ready" rows) and in the native fixture with
// hardware actions replaced by counters. See test/test_chaster_ask.
//
// While Chaster controls a session the box asks the dashboard before it opens
// for a break or unlocks at the end of its timer (utils/chasterAsk.h). It
// opens only on a granted answer; a refusal or no answer keeps it locked on a
// page that says so. Emergency unlock and a clear from the dashboard never
// ask, and every page here leads back to the lock menu.

// Unlock at the end of the timer. The first row wins for a Chaster session, so
// such a session never reaches the wearer's own unlock in the second.
"locked.ready"_s + buttonPress[isChasterSession] = "locked.unlockAsk"_s,
"locked.ready"_s + buttonPress[acceptNaturalUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,

"locked.unlockAsk"_s + on_entry<_> / (drawAskingChaster, startUnlockAsk),
// The deadline, a freeze and a pending offer are checked again on the answer.
"locked.unlockAsk"_s + askGranted[acceptNaturalUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.unlockAsk"_s + askGranted = "locked.return"_s,
"locked.unlockAsk"_s + askRefused = "locked.unlockRefused"_s,
"locked.unlockAsk"_s + askUnanswered = "locked.unlockNoAnswer"_s,

"locked.unlockRefused"_s + on_entry<_> / drawUnlockRefused,
"locked.unlockRefused"_s + buttonPress[isReadyToUnlock] = "locked.unlockAsk"_s,
"locked.unlockRefused"_s + buttonPress = "locked.return"_s,
"locked.unlockRefused"_s + backPress = "lockMenu"_s,
"locked.unlockRefused"_s + freezeState = "locked.return"_s,

"locked.unlockNoAnswer"_s + on_entry<_> / drawUnlockNoAnswer,
"locked.unlockNoAnswer"_s + buttonPress[isReadyToUnlock] = "locked.unlockAsk"_s,
"locked.unlockNoAnswer"_s + buttonPress = "locked.return"_s,
"locked.unlockNoAnswer"_s + backPress = "lockMenu"_s,
"locked.unlockNoAnswer"_s + freezeState = "locked.return"_s,

// A break from the wearer's menu. startBreakPreflight sends AskChaster instead
// of Break once the box's own rules allow the break.
"locked.breakPreflight"_s + askChaster = "locked.breakAsk"_s,
"locked.breakAsk"_s + on_entry<_> / (drawAskingChaster, startBreakAsk),
"locked.breakAsk"_s + askRefused = "locked.breakRefused"_s,
"locked.breakAsk"_s + askUnanswered = "locked.breakNoAnswer"_s,

"locked.breakRefused"_s + on_entry<_> / drawBreakRefused,
"locked.breakRefused"_s + buttonPress = "lockMenu"_s,
"locked.breakRefused"_s + backPress = "lockMenu"_s,

"locked.breakNoAnswer"_s + on_entry<_> / drawBreakNoAnswer,
"locked.breakNoAnswer"_s + buttonPress = "locked.breakPreflight"_s,
"locked.breakNoAnswer"_s + backPress = "lockMenu"_s,

// A break the dashboard (or Chaster through it) starts opens the box from every
// locked page, not only from the ones in BreakEntryTransitions.inc. A granted
// break arrives as the same event.
"locked.ready"_s + breakE = "locked.break"_s,
"locked.ready.addTime"_s + breakE = "locked.break"_s,
"locked.breaking"_s + breakE = "locked.break"_s,
"locked.breakError"_s + breakE = "locked.break"_s,
"locked.breakCooldown"_s + breakE = "locked.break"_s,
"lockMenu.wifi"_s + breakE = "locked.break"_s,
"locked.unlockAsk"_s + breakE = "locked.break"_s,
"locked.unlockRefused"_s + breakE = "locked.break"_s,
"locked.unlockNoAnswer"_s + breakE = "locked.break"_s,
"locked.breakAsk"_s + breakE = "locked.break"_s,
"locked.breakRefused"_s + breakE = "locked.break"_s,
"locked.breakNoAnswer"_s + breakE = "locked.break"_s,

// Messages from the dashboard on the pages added here. A clear and a new offer
// are handled everywhere; the pages that stay up also follow a new lock.
"locked.unlockAsk"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.unlockAsk"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"locked.breakAsk"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.breakAsk"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"locked.unlockRefused"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.unlockRefused"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"locked.unlockNoAnswer"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.unlockNoAnswer"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"locked.breakRefused"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.breakRefused"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"locked.breakNoAnswer"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.breakNoAnswer"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"locked.unlockRefused"_s + locktimer / switchToTimer = "locking"_s,
"locked.unlockRefused"_s + lock / switchToLock = "locking"_s,
"locked.unlockNoAnswer"_s + locktimer / switchToTimer = "locking"_s,
"locked.unlockNoAnswer"_s + lock / switchToLock = "locking"_s,
"locked.breakRefused"_s + locktimer / switchToTimer = "locking"_s,
"locked.breakRefused"_s + lock / switchToLock = "locking"_s,
"locked.breakNoAnswer"_s + locktimer / switchToTimer = "locking"_s,
"locked.breakNoAnswer"_s + lock / switchToLock = "locking"_s,
