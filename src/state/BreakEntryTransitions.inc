// Exact production transitions, also exercised by the native dispatch tests.
// A dashboard request bypasses the local preflight page, so report the break
// from the common entry after startBreakTimer persists its timing metadata.
"locked"_s + breakE = "locked.break"_s,
"locked.timer"_s + breakE = "locked.break"_s,
"lockMenu"_s + breakE = "locked.break"_s,
"locked.breakPreflight"_s + breakE = "locked.break"_s,
"locked.break"_s + on_entry<_> / (movePaddleToUnlock, startBreakTimer,
                                clearLockedReport,
                                sendMessage(Str::Protocol::BREAK)),
