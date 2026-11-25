// Production Boost.SML rows, included inside make_transition_table in LKBX.h
// and in the native fixture with hardware actions replaced by counters. See
// test/test_natural_unlock.
//
// The check after an unlock found the paddle still closed (E-LOCK-2). The
// motor presses the paddle home again and the page offers Retry, which
// restarts the box, and Calibrate.
//
// The page stands over two different boxes. After a clear from the dashboard
// the stored lock has ended and nothing is held. After the unlock at the end
// of a session's timer nothing was stored (utils/naturalUnlock.h): the
// session is still running, the lock is stored and held, and a restart
// returns to Ready to unlock, where Unlock tries again.
"locking.preflight"_s + error[isError(ErrorT::CANNOT_UNLOCK)] / (movePaddleToLock) = "locking.errorUnlock"_s,
"locking.errorUnlock"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
"locking.errorUnlock"_s + on_entry<_> / (drawErrorUnlock),
"locking.errorUnlock"_s + buttonPress = "restart"_s,
"locking.errorUnlock"_s + backPress / clearCalibration = "calibrate"_s,
"locking.errorUnlock"_s + forceUpdate = "update"_s,
// The dashboard's word still reaches a session that runs on. Time added, a
// freeze or a hold arrives as FreezeChange once it is stored (the MQTT sync,
// the state read, a BLE write): when the session is no longer ready to
// unlock, the box leaves the error page for its locked pages through the
// check, which confirms the closed paddle and reports LOCKED. The motor is
// not asked to move. While the session is still at its end the page stays,
// with its Retry.
"locking.errorUnlock"_s + freezeState[isLocked && !isReadyToUnlock] = "locking"_s,
// The dashboard ended the session, or has none for the box: open, as from
// any other page. The check shows this page again if the paddle stays closed.
"locking.errorUnlock"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
