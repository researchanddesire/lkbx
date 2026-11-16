// Production Boost.SML rows, included inside make_transition_table in LKBX.h
// and in the native fixture with hardware actions replaced by counters. See
// test/test_local_timer.
//
// Pressing Lock on the timer page starts a lock the wearer cannot end early,
// so it must have an end. saveLocalTimer is a guard that commits: it stores
// the deadline for the duration the page shows and is true only once that
// deadline is stored. When it is false (the page never drew, so there is no
// duration, or the deadline could not be saved) the motor does not move, the
// box stays unlocked and the page says the timer could not be set.
//
// 2026-11-15, R8: the timer page's task could not be created, the page stayed
// blank, and Lock then locked the box with no deadline at all.
"timer"_s + buttonPress[saveLocalTimer] / lockTimerAction = "locking"_s,
"timer"_s + buttonPress / showTimerUnavailable,
