// Production Boost.SML rows, included inside make_transition_table in LKBX.h
// (before the other "locked.ready" rows) and in the native fixture with
// hardware actions replaced by counters. See test/test_chaster_ask.
//
// While Chaster controls a session the box asks the dashboard before it opens
// for a break or unlocks at the end of its timer (utils/chasterAsk.h). It
// opens only on a granted answer; a refusal or no answer keeps it locked on a
// page that says so. Emergency unlock and a clear from the dashboard never
// ask, and the answer page leads to the lock menu.
//
// Keep this fragment small. Every row, state and event type adds to the
// memory the compiler needs for the whole table, and the build runners have
// little to spare: the answers arrive as existing events (Break, Unlock,
// Error) and both questions share one asking state and one answer page.

// Unlock at the end of the timer. The first row wins for a Chaster session, so
// such a session never reaches the wearer's own unlock in the second.
"locked.ready"_s + buttonPress[isChasterSession] / setAskUnlock = "locked.asking"_s,
"locked.ready"_s + buttonPress[acceptNaturalUnlock] / (clearLocalPlay, unlockAction) = "locking"_s,
// A break from the wearer's menu: startBreakPreflight reports ASK_CHASTER
// instead of opening once the box's own rules allow the break.
"locked.breakPreflight"_s + error[isError(ErrorT::ASK_CHASTER)] / setAskBreak = "locked.asking"_s,

// Waiting for the answer. A granted break arrives as Break and a granted
// unlock as Unlock, the events the dashboard's own break and clear use.
"locked.asking"_s + on_entry<_> / (drawAskingChaster, startChasterAsk),
"locked.asking"_s + breakE = "locked.break"_s,
"locked.asking"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.asking"_s + error[isError(ErrorT::ASK_RETURN)] = "locked.return"_s,
"locked.asking"_s + error[isError(ErrorT::ASK_ANSWERED)] / drawAskAnswer = "locked.askAnswer"_s,
"locked.asking"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,

// Refused, or no answer: the box stays locked. Either button opens the lock
// menu, where Go Back returns to the page the wearer asked from.
"locked.askAnswer"_s + buttonPress = "lockMenu"_s,
"locked.askAnswer"_s + backPress = "lockMenu"_s,
"locked.askAnswer"_s + breakE = "locked.break"_s,
"locked.askAnswer"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
"locked.askAnswer"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,

// A break the dashboard (or Chaster through it) starts opens the box from the
// locked pages BreakEntryTransitions.inc left out.
"locked.ready"_s + breakE = "locked.break"_s,
"locked.ready.addTime"_s + breakE = "locked.break"_s,
"locked.breaking"_s + breakE = "locked.break"_s,
"locked.breakError"_s + breakE = "locked.break"_s,
"locked.breakCooldown"_s + breakE = "locked.break"_s,
