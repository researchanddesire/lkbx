#include <initializer_list>
#include <string>

// Before Arduino.h: this header reads <mutex>, and the test build's Arduino.h
// defines round and abs as macros, which <chrono> declares as functions.
#include "../../src/utils/naturalUnlock.h"

#include <Arduino.h>
#include <unity.h>

#include "../../src/state/Events.h"
#include "../../src/utils/heldLock.h"
#include "../../src/utils/heldSession.h"
#include "../../src/utils/syncEffect.h"

using namespace sml;

// The unlock at the end of a session's timer (utils/naturalUnlock.h). A
// session is stored as finished only once the check after the motor movement
// has seen the paddle open. When the paddle stays closed the session runs on:
// the lock is still stored and held, the dashboard's messages apply, and a
// restart restores the lock (rad-app#2799).

void setUp() {}
void tearDown() {}

namespace {

const char* const kSession = "session-a";

// The parts of the box this unlock touches, kept as the firmware keeps them
// and changed at the same moments. The decisions are the firmware's own:
// naturalUnlock::Tracker, naturalUnlock::checkPaddle, decidePreflight,
// breakRecordStands and boxEndedSession.
struct Box {
    // Flash.
    bool locked = false;       // lockbox/isLocked
    bool lockHeld = false;     // lockbox/lockHeld
    bool local = false;        // lockbox/isLocalPlay
    bool activeBreak = false;  // session/isActiveBreak
    std::string state;         // session/lockState
    // RAM, lost in a restart.
    naturalUnlock::Tracker unlockTracker;
    int motorMovement = 0;  // Motor::lockId
    uint32_t nowMs = 60000;
    // What the tests look at.
    int paddleToldToOpen = 0;
    int paddlePressedHome = 0;
    int unlockedReports = 0;
    int lockedReports = 0;

    // Motor::lock.
    void motorLock(bool save) {
        ++motorMovement;
        ++paddlePressedHome;
        if (!save) return;
        if (!locked) lockHeld = false;
        locked = true;
    }
    // Motor::unlock: the held mark goes whenever the paddle is told to open.
    void motorUnlock(bool save) {
        ++motorMovement;
        ++paddleToldToOpen;
        lockHeld = false;
        if (save) locked = false;
    }

    // acceptExpiredTimer, for a session that is ready to unlock.
    void accept() { unlockTracker.accept(nowMs); }
    // clearLocalPlay.
    void clearLocalPlay() {
        if (unlockTracker.isAccepted(nowMs)) return;
        local = false;
    }
    // unlockAction.
    void unlockAction() {
        if (unlockTracker.begin(nowMs)) {
            const bool wasHeld = lockHeld;
            motorUnlock(false);
            unlockTracker.opening(motorMovement, wasHeld);
            return;
        }
        motorUnlock(true);
    }
    // A transition "[accept] / (clearLocalPlay, unlockAction)": the press on
    // Ready to unlock, a granted answer, a local timer that ran out.
    void unlockAtEndOfTimer() {
        accept();
        clearLocalPlay();
        unlockAction();
    }
    // A clear from the dashboard for the held session: clearState, then the
    // Unlock event.
    void dashboardClear() {
        state.clear();
        activeBreak = false;
        locked = false;
        lockHeld = false;
        clearLocalPlay();
        unlockAction();
    }

    // LKBX::startPreflight with these sensor readings, and what the
    // transition of its result does to the paddle and the reports.
    Preflight check(bool paddleLocked, bool backplateClosed) {
        const naturalUnlock::Opening opening =
            unlockTracker.take(motorMovement);
        switch (naturalUnlock::checkPaddle(opening.underWay, locked,
                                           paddleLocked)) {
            case naturalUnlock::Check::OPENED:
                // finishNaturalUnlock.
                locked = false;
                local = false;
                if (state != "pending") {
                    state = "completed";
                    activeBreak = false;
                }
                break;
            case naturalUnlock::Check::STILL_CLOSED:
                // keepLockAfterFailedUnlock, then the unlock-error page.
                if (opening.wasHeld && locked) lockHeld = true;
                motorLock(false);
                return Preflight::CANNOT_UNLOCK;
            case naturalUnlock::Check::NOT_UNDER_WAY:
                break;
        }

        const bool held =
            locked && !(paddleLocked && backplateClosed) && lockHeld;
        const Preflight result =
            decidePreflight(locked, paddleLocked, backplateClosed, held);
        switch (result) {
            case Preflight::LOCK_CONFIRMED:
                // markLockHeld also removes a break record left behind.
                lockHeld = true;
                activeBreak = false;
                ++lockedReports;
                break;
            case Preflight::KEEP_CLOSED:
                motorLock(false);
                ++lockedReports;
                break;
            case Preflight::CANNOT_LOCK:
            case Preflight::NO_BACKPLATE:
                motorUnlock(false);
                break;
            case Preflight::PADDLE_OPEN:
                ++unlockedReports;
                break;
            case Preflight::CANNOT_UNLOCK:
                motorLock(false);
                break;
        }
        return result;
    }

    // A restart: flash stays, RAM goes. The home state then restores a
    // stored lock that is not on a break (lockTimerAction).
    void restart() {
        unlockTracker.clear();
        motorMovement = 0;
        if (locked && !breakRecordStands(activeBreak, locked, lockHeld)) {
            motorLock(true);
        }
    }

    // Whether a sync for the stored session is applied (saveState).
    bool takesSync() const { return !boxEndedSession(true, state.c_str()); }
};

// A dashboard session the box has locked and confirmed.
void holdSession(Box& box) {
    box.state = "locked";
    box.motorLock(true);
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::LOCK_CONFIRMED);
    TEST_ASSERT_TRUE(box.lockHeld);
    box.paddleToldToOpen = 0;
    box.paddlePressedHome = 0;
    box.lockedReports = 0;
}

// The box after the unlock at the end of its timer met a paddle that stayed
// closed.
void failUnlock(Box& box) {
    holdSession(box);
    box.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::CANNOT_UNLOCK);
}

}  // namespace

// The rows of the unlock-error page, with the hardware replaced by counters.
// They are the exact fragment LKBX::InnerStateMachine includes, not a copy of
// its behaviour.
struct Page {
    bool locked = true;  // the stored lock
    bool ready = true;   // the session is at its end and nothing holds it
    int pressedHome = 0;
    int paddleOpens = 0;
    int unlocks = 0;
    int errorPages = 0;
    int calibrationsCleared = 0;
};

struct Fixture {
    auto operator()() const {
        auto isError = [](ErrorT expected) {
            return [expected](const auto& e) { return e.error == expected; };
        };
        auto isLocked = [](Page& p) { return p.locked; };
        auto isReadyToUnlock = [](Page& p) { return p.locked && p.ready; };
        auto movePaddleToLock = [](Page& p) { ++p.pressedHome; };
        auto movePaddleToUnlock = [](Page& p) { ++p.paddleOpens; };
        auto clearActiveBreak = [](Page&) {};
        auto clearLocalPlay = [](Page&) {};
        auto clearCalibration = [](Page& p) { ++p.calibrationsCleared; };
        auto unlockAction = [](Page& p) { ++p.unlocks; };
        auto drawErrorUnlock = [](Page& p) { ++p.errorPages; };
        return make_transition_table(
            *"fixture"_s + done = "fixture"_s,
#include "../../src/state/UnlockErrorTransitions.inc"
            "locking"_s + done = "locking.preflight"_s
        );
    }
};
using Machine = sml::sm<Fixture, sml::testing>;

namespace {
const Error kCannotUnlock{.error = ErrorT::CANNOT_UNLOCK};
}  // namespace

// --- The note the accept guard leaves for the unlock action -------------------

void test_an_accepted_unlock_is_carried_out_once() {
    naturalUnlock::Tracker tracker;
    // Nothing accepted: the unlock ends the stored lock, as it always has.
    TEST_ASSERT_FALSE(tracker.begin(1000));

    tracker.accept(1000);
    TEST_ASSERT_TRUE(tracker.isAccepted(1000));
    TEST_ASSERT_TRUE(tracker.begin(1001));
    // Used up: the next unlock is not this one.
    TEST_ASSERT_FALSE(tracker.isAccepted(1001));
    TEST_ASSERT_FALSE(tracker.begin(1002));
}

// An accepted unlock that no unlock action carried out must not turn a later
// unlock (a clear from the dashboard, say) into the end of a timer.
void test_an_accepted_unlock_that_is_not_carried_out_lapses() {
    naturalUnlock::Tracker tracker;
    tracker.accept(1000);
    TEST_ASSERT_TRUE(
        tracker.isAccepted(1000 + naturalUnlock::kAcceptedForMs - 1));
    TEST_ASSERT_FALSE(tracker.isAccepted(1000 + naturalUnlock::kAcceptedForMs));
    TEST_ASSERT_FALSE(tracker.begin(1000 + naturalUnlock::kAcceptedForMs));

    // The wait is measured across a millis() wraparound.
    tracker.accept(UINT32_MAX - 10);
    TEST_ASSERT_TRUE(tracker.begin(5));
}

void test_the_check_hears_of_the_unlock_once() {
    naturalUnlock::Tracker tracker;
    tracker.accept(0);
    TEST_ASSERT_TRUE(tracker.begin(0));
    tracker.opening(7, true);
    TEST_ASSERT_TRUE(tracker.isOpening(7));

    naturalUnlock::Opening opening = tracker.take(7);
    TEST_ASSERT_TRUE(opening.underWay);
    TEST_ASSERT_TRUE(opening.wasHeld);
    // The next check, of the same movement or another, is not told again.
    TEST_ASSERT_FALSE(tracker.isOpening(7));
    opening = tracker.take(7);
    TEST_ASSERT_FALSE(opening.underWay);
    TEST_ASSERT_FALSE(opening.wasHeld);
}

// A lock, a break or an offer that moved the motor after the unlock has
// replaced it: the check that follows is that movement's.
void test_another_motor_movement_replaces_the_unlock() {
    naturalUnlock::Tracker tracker;
    tracker.opening(7, true);
    TEST_ASSERT_FALSE(tracker.isOpening(8));
    const naturalUnlock::Opening opening = tracker.take(8);
    TEST_ASSERT_FALSE(opening.underWay);
    TEST_ASSERT_FALSE(opening.wasHeld);
    TEST_ASSERT_FALSE(tracker.take(7).underWay);
}

// A check that runs between an accept and its unlock action leaves the
// accept alone.
void test_a_check_does_not_use_up_an_accepted_unlock() {
    naturalUnlock::Tracker tracker;
    tracker.accept(100);
    TEST_ASSERT_FALSE(tracker.take(3).underWay);
    TEST_ASSERT_TRUE(tracker.begin(101));
}

// --- What the check decides ---------------------------------------------------

void test_the_check_decides_by_the_paddle() {
    TEST_ASSERT_TRUE(naturalUnlock::checkPaddle(true, true, false) ==
                     naturalUnlock::Check::OPENED);
    TEST_ASSERT_TRUE(naturalUnlock::checkPaddle(true, true, true) ==
                     naturalUnlock::Check::STILL_CLOSED);
}

void test_any_other_movement_is_checked_as_before() {
    for (const bool locked : {false, true}) {
        for (const bool paddleLocked : {false, true}) {
            TEST_ASSERT_TRUE(
                naturalUnlock::checkPaddle(false, locked, paddleLocked) ==
                naturalUnlock::Check::NOT_UNDER_WAY);
        }
    }
    // A clear from the dashboard ended the stored lock while the paddle was
    // opening: the box is checked as any unlocked one.
    TEST_ASSERT_TRUE(naturalUnlock::checkPaddle(true, false, false) ==
                     naturalUnlock::Check::NOT_UNDER_WAY);
    TEST_ASSERT_TRUE(naturalUnlock::checkPaddle(true, false, true) ==
                     naturalUnlock::Check::NOT_UNDER_WAY);
}

// --- The paddle opens ---------------------------------------------------------

void test_nothing_is_stored_before_the_paddle_is_seen_open() {
    Box box;
    holdSession(box);
    box.unlockAtEndOfTimer();

    // The paddle has been told to open and the check has not run yet.
    TEST_ASSERT_EQUAL_INT(1, box.paddleToldToOpen);
    TEST_ASSERT_TRUE(box.locked);
    TEST_ASSERT_EQUAL_STRING("locked", box.state.c_str());
    TEST_ASSERT_TRUE(box.takesSync());
    // The held mark is cleared like for every movement that opens the paddle.
    TEST_ASSERT_FALSE(box.lockHeld);
}

void test_a_session_is_finished_once_its_paddle_has_opened() {
    Box box;
    holdSession(box);
    box.unlockAtEndOfTimer();

    TEST_ASSERT_TRUE(box.check(false, true) == Preflight::PADDLE_OPEN);
    TEST_ASSERT_FALSE(box.locked);
    TEST_ASSERT_FALSE(box.lockHeld);
    TEST_ASSERT_EQUAL_STRING("completed", box.state.c_str());
    // The box reports that it opened, and from here on ignores the dashboard
    // for this session: a late "locked" must not close an open box.
    TEST_ASSERT_EQUAL_INT(1, box.unlockedReports);
    TEST_ASSERT_FALSE(box.takesSync());
    TEST_ASSERT_EQUAL_INT(0, box.paddlePressedHome);
}

// --- The paddle stays closed --------------------------------------------------

// 2026-11-23, a production Chaster lock: the unlock was accepted at the end
// time and the paddle did not open. The session was stored as finished all
// the same.
void test_a_failed_unlock_leaves_the_session_running() {
    Box box;
    failUnlock(box);

    TEST_ASSERT_TRUE(box.locked);
    TEST_ASSERT_EQUAL_STRING("locked", box.state.c_str());
    TEST_ASSERT_EQUAL_INT(0, box.unlockedReports);
    // The motor presses the paddle home again for the error page.
    TEST_ASSERT_EQUAL_INT(1, box.paddlePressedHome);
    // Only that session's own messages may change it, as for any locked one.
    TEST_ASSERT_TRUE(holdsLockedSession(kSession, box.state.c_str()));
    TEST_ASSERT_TRUE(classifyHeldSessionSync("session-b", kSession,
                                             box.state.c_str()) ==
                     HeldSessionSync::CONFIRM);
}

void test_a_failed_unlock_leaves_the_lock_held() {
    Box box;
    failUnlock(box);
    TEST_ASSERT_TRUE(box.lockHeld);

    // A backplate that read open during the failed unlock changes nothing:
    // the paddle never left its place.
    Box other;
    holdSession(other);
    other.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(other.check(true, false) == Preflight::CANNOT_UNLOCK);
    TEST_ASSERT_TRUE(other.locked);
    TEST_ASSERT_TRUE(other.lockHeld);
}

void test_a_failed_unlock_can_be_tried_again() {
    Box box;
    failUnlock(box);

    // Unlock on Ready to unlock, once more.
    box.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(box.check(false, true) == Preflight::PADDLE_OPEN);
    TEST_ASSERT_FALSE(box.locked);
    TEST_ASSERT_EQUAL_STRING("completed", box.state.c_str());
    TEST_ASSERT_EQUAL_INT(1, box.unlockedReports);
}

// --- Time added after a failed unlock -------------------------------------------

// The box ignored 11.7 hours Chaster added after its paddle had failed to
// open, because it had stored the session as finished.
void test_a_session_whose_unlock_failed_takes_the_dashboards_messages() {
    Box box;
    failUnlock(box);

    TEST_ASSERT_TRUE(box.takesSync());
    // A new end time is a change of the timer, not a new lock: no motor
    // sequence is replayed for it.
    TEST_ASSERT_TRUE(classifySyncEffect(true, box.state.c_str(), "locked",
                                        true) == SyncEffect::TIMER_META);
    // An identical repeat does nothing at all.
    TEST_ASSERT_TRUE(classifySyncEffect(true, box.state.c_str(), "locked",
                                        false) == SyncEffect::NONE);
}

void test_a_session_the_box_ended_ignores_the_dashboard() {
    TEST_ASSERT_TRUE(boxEndedSession(true, "completed"));
    TEST_ASSERT_TRUE(boxEndedSession(true, "abandoned"));
    // Every state of a session that is still running takes the sync.
    for (const char* state :
         {"locked", "break", "break_ending", "pending", "unlocked", ""}) {
        TEST_ASSERT_FALSE(boxEndedSession(true, state));
    }
    // Another session is never ignored on the strength of this one.
    TEST_ASSERT_FALSE(boxEndedSession(false, "completed"));
    TEST_ASSERT_FALSE(boxEndedSession(false, "abandoned"));
    TEST_ASSERT_FALSE(boxEndedSession(true, nullptr));
}

void test_time_added_after_a_failed_unlock_returns_to_the_locked_pages() {
    Box box;
    failUnlock(box);
    const int opensBefore = box.paddleToldToOpen;

    Page page;
    Machine machine{page};
    machine.set_current_states("locking.preflight"_s);
    TEST_ASSERT_TRUE(machine.process_event(kCannotUnlock));
    TEST_ASSERT_TRUE(machine.is("locking.errorUnlock"_s));
    TEST_ASSERT_EQUAL_INT(1, page.pressedHome);
    TEST_ASSERT_EQUAL_INT(1, page.errorPages);

    // The sync stored a later end time and sent FreezeChange.
    page.ready = false;
    TEST_ASSERT_TRUE(machine.process_event(FreezeChange{}));
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(0, page.unlocks);
    TEST_ASSERT_EQUAL_INT(0, page.paddleOpens);

    // The check confirms the closed box and reports LOCKED; the paddle was
    // never told to open.
    TEST_ASSERT_TRUE(machine.process_event(Done{}));
    TEST_ASSERT_TRUE(machine.is("locking.preflight"_s));
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::LOCK_CONFIRMED);
    TEST_ASSERT_TRUE(box.locked);
    TEST_ASSERT_TRUE(box.lockHeld);
    TEST_ASSERT_EQUAL_INT(1, box.lockedReports);
    TEST_ASSERT_EQUAL_INT(opensBefore, box.paddleToldToOpen);
    TEST_ASSERT_EQUAL_INT(0, box.unlockedReports);
}

// A freeze or an unlock hold from Chaster does the same.
void test_a_freeze_after_a_failed_unlock_returns_to_the_locked_pages() {
    Page page;
    // isReadyToUnlock is false for a held session, whatever its end time.
    page.ready = false;
    Machine machine{page};
    machine.set_current_states("locking.errorUnlock"_s);
    TEST_ASSERT_TRUE(machine.process_event(FreezeChange{}));
    TEST_ASSERT_TRUE(machine.is("locking"_s));
}

// A sync that leaves the session at its end keeps the page, with its Retry.
void test_the_error_page_stays_while_the_session_is_still_at_its_end() {
    Page page;
    Machine machine{page};
    machine.set_current_states("locking.errorUnlock"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locking.errorUnlock"_s));
    TEST_ASSERT_EQUAL_INT(0, page.pressedHome);
    TEST_ASSERT_EQUAL_INT(0, page.unlocks);
}

// After a clear from the dashboard whose unlock failed nothing is locked: a
// change of what is stored must not send that box to a locked page.
void test_the_error_page_of_an_ended_lock_does_not_return_to_a_locked_page() {
    Page page;
    page.locked = false;
    page.ready = false;
    Machine machine{page};
    machine.set_current_states("locking.errorUnlock"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locking.errorUnlock"_s));
}

// The dashboard ends the session while the error page is up: the box opens,
// as it does from every other page.
void test_a_clear_on_the_error_page_tries_the_unlock_again() {
    Page page;
    Machine machine{page};
    machine.set_current_states("locking.errorUnlock"_s);
    TEST_ASSERT_TRUE(machine.process_event(Unlock{}));
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, page.unlocks);

    Box box;
    failUnlock(box);
    box.dashboardClear();
    TEST_ASSERT_TRUE(box.check(false, true) == Preflight::PADDLE_OPEN);
    TEST_ASSERT_FALSE(box.locked);
    TEST_ASSERT_EQUAL_INT(1, box.unlockedReports);
}

void test_the_error_page_keeps_its_buttons() {
    Page page;
    Machine machine{page};
    machine.set_current_states("locking.errorUnlock"_s);
    TEST_ASSERT_TRUE(machine.process_event(ButtonPress{}));
    TEST_ASSERT_TRUE(machine.is("restart"_s));

    machine.set_current_states("locking.errorUnlock"_s);
    TEST_ASSERT_TRUE(machine.process_event(BackPress{}));
    TEST_ASSERT_TRUE(machine.is("calibrate"_s));
    TEST_ASSERT_EQUAL_INT(1, page.calibrationsCleared);

    machine.set_current_states("locking.errorUnlock"_s);
    TEST_ASSERT_TRUE(machine.process_event(ForceUpdate{}));
    TEST_ASSERT_TRUE(machine.is("update"_s));

    machine.set_current_states("locking.errorUnlock"_s);
    TEST_ASSERT_TRUE(machine.process_event(LockOffered{}));
    TEST_ASSERT_TRUE(machine.is("acceptLock"_s));
    TEST_ASSERT_EQUAL_INT(1, page.paddleOpens);
}

// --- A restart after a failed unlock ---------------------------------------------

void test_a_restart_after_a_failed_unlock_restores_the_running_session() {
    Box box;
    failUnlock(box);
    box.restart();

    TEST_ASSERT_TRUE(box.locked);
    TEST_ASSERT_EQUAL_STRING("locked", box.state.c_str());
    TEST_ASSERT_TRUE(box.takesSync());
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::LOCK_CONFIRMED);
    TEST_ASSERT_EQUAL_INT(1, box.lockedReports);
    TEST_ASSERT_EQUAL_INT(0, box.unlockedReports);

    // From Ready to unlock the wearer unlocks again, and this time it opens.
    box.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(box.check(false, true) == Preflight::PADDLE_OPEN);
    TEST_ASSERT_EQUAL_STRING("completed", box.state.c_str());
    TEST_ASSERT_EQUAL_INT(1, box.unlockedReports);
}

// The lock the box held before the failed unlock is still a held one: a bad
// sensor reading after the restart must not open it (utils/heldLock.h).
void test_a_restart_after_a_failed_unlock_never_opens_on_a_bad_reading() {
    for (const bool paddleReading : {false, true}) {
        for (const bool backplateReading : {false, true}) {
            if (paddleReading && backplateReading) continue;
            Box box;
            failUnlock(box);
            const int opensBefore = box.paddleToldToOpen;
            box.restart();
            TEST_ASSERT_TRUE(box.check(paddleReading, backplateReading) ==
                             Preflight::KEEP_CLOSED);
            TEST_ASSERT_EQUAL_INT(opensBefore, box.paddleToldToOpen);
            TEST_ASSERT_TRUE(box.locked);
            TEST_ASSERT_EQUAL_STRING("locked", box.state.c_str());
        }
    }
}

// A restart between the unlock and its check: what was noted in RAM is gone,
// and so is nothing else. The stored lock closes the box again.
void test_a_restart_before_the_check_does_not_finish_the_session() {
    Box box;
    holdSession(box);
    box.unlockAtEndOfTimer();
    box.restart();

    TEST_ASSERT_TRUE(box.locked);
    TEST_ASSERT_EQUAL_STRING("locked", box.state.c_str());
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::LOCK_CONFIRMED);
    TEST_ASSERT_EQUAL_INT(0, box.unlockedReports);

    // The paddle had been told to open, so this lock is checked as a new
    // one: with the backplate already off, the paddle opens to let it be
    // fitted.
    Box unfitted;
    holdSession(unfitted);
    unfitted.unlockAtEndOfTimer();
    unfitted.restart();
    TEST_ASSERT_TRUE(unfitted.check(true, false) == Preflight::NO_BACKPLATE);
    TEST_ASSERT_EQUAL_STRING("locked", unfitted.state.c_str());
}

// --- Other sessions and other unlocks -------------------------------------------

// A local timer whose paddle stays closed is still the wearer's own: the box
// must not ask the dashboard about it, and must release it without a clock.
void test_a_local_timer_stays_local_until_its_paddle_has_opened() {
    Box box;
    box.local = true;
    holdSession(box);
    box.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(box.local);
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::CANNOT_UNLOCK);
    TEST_ASSERT_TRUE(box.local);
    TEST_ASSERT_TRUE(box.locked);

    box.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(box.check(false, true) == Preflight::PADDLE_OPEN);
    TEST_ASSERT_FALSE(box.local);
    TEST_ASSERT_FALSE(box.locked);
}

// A local timer that runs out during a break finds the paddle open already.
void test_a_timer_that_runs_out_during_a_break_finishes_the_session() {
    Box box;
    box.local = true;
    holdSession(box);
    box.motorUnlock(false);  // locked.break
    box.activeBreak = true;

    box.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(box.activeBreak);
    TEST_ASSERT_TRUE(box.check(false, false) == Preflight::PADDLE_OPEN);
    TEST_ASSERT_FALSE(box.activeBreak);
    TEST_ASSERT_EQUAL_STRING("completed", box.state.c_str());
}

// When that unlock fails the box was on a break, with nothing held: the mark
// is not set for a lock that had none, and the next restart resumes the
// break.
void test_a_failed_unlock_during_a_break_does_not_invent_a_held_lock() {
    Box box;
    box.local = true;
    holdSession(box);
    box.motorUnlock(false);  // locked.break
    box.activeBreak = true;

    box.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::CANNOT_UNLOCK);
    TEST_ASSERT_FALSE(box.lockHeld);
    TEST_ASSERT_TRUE(box.locked);
    TEST_ASSERT_TRUE(breakRecordStands(box.activeBreak, box.locked,
                                       box.lockHeld));
}

// A break record left beside the lock is still ignored after a failed
// unlock, and removed when the lock is next confirmed.
void test_a_stale_break_record_beside_a_failed_unlock_is_still_ignored() {
    Box box;
    holdSession(box);
    box.activeBreak = true;  // left behind by a break that has ended
    box.unlockAtEndOfTimer();
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::CANNOT_UNLOCK);

    TEST_ASSERT_FALSE(breakRecordStands(box.activeBreak, box.locked,
                                        box.lockHeld));
    const int opensBefore = box.paddleToldToOpen;
    box.restart();
    TEST_ASSERT_EQUAL_INT(opensBefore, box.paddleToldToOpen);
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::LOCK_CONFIRMED);
    TEST_ASSERT_FALSE(box.activeBreak);
}

// A clear from the dashboard ends the stored lock before the motor moves, as
// it always has: the dashboard has ended the session, so nothing runs on.
void test_a_dashboard_clear_ends_the_lock_whatever_the_paddle_does() {
    Box box;
    holdSession(box);
    box.dashboardClear();
    TEST_ASSERT_FALSE(box.locked);
    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::CANNOT_UNLOCK);
    TEST_ASSERT_FALSE(box.locked);
    TEST_ASSERT_FALSE(box.lockHeld);
    TEST_ASSERT_EQUAL_STRING("", box.state.c_str());
}

// A clear that lands while the paddle is opening at the end of the timer
// wins: the session it cleared is not written back as finished.
void test_a_clear_during_the_unlock_is_not_overwritten() {
    Box box;
    holdSession(box);
    box.unlockAtEndOfTimer();
    // clearState ran; its Unlock event has not been dispatched yet.
    box.state.clear();
    box.locked = false;

    TEST_ASSERT_TRUE(box.check(false, true) == Preflight::PADDLE_OPEN);
    TEST_ASSERT_EQUAL_STRING("", box.state.c_str());
    TEST_ASSERT_FALSE(box.locked);
}

// The dashboard locks again while the paddle is opening: that movement is
// the one the check follows, and the session stays as the dashboard set it.
void test_a_lock_during_the_unlock_replaces_it() {
    Box box;
    holdSession(box);
    box.unlockAtEndOfTimer();
    box.motorLock(true);  // "locking" + locktimer / lockTimerAction

    TEST_ASSERT_TRUE(box.check(true, true) == Preflight::LOCK_CONFIRMED);
    TEST_ASSERT_TRUE(box.locked);
    TEST_ASSERT_EQUAL_STRING("locked", box.state.c_str());
    TEST_ASSERT_EQUAL_INT(0, box.unlockedReports);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_an_accepted_unlock_is_carried_out_once);
    RUN_TEST(test_an_accepted_unlock_that_is_not_carried_out_lapses);
    RUN_TEST(test_the_check_hears_of_the_unlock_once);
    RUN_TEST(test_another_motor_movement_replaces_the_unlock);
    RUN_TEST(test_a_check_does_not_use_up_an_accepted_unlock);
    RUN_TEST(test_the_check_decides_by_the_paddle);
    RUN_TEST(test_any_other_movement_is_checked_as_before);
    RUN_TEST(test_nothing_is_stored_before_the_paddle_is_seen_open);
    RUN_TEST(test_a_session_is_finished_once_its_paddle_has_opened);
    RUN_TEST(test_a_failed_unlock_leaves_the_session_running);
    RUN_TEST(test_a_failed_unlock_leaves_the_lock_held);
    RUN_TEST(test_a_failed_unlock_can_be_tried_again);
    RUN_TEST(test_a_session_whose_unlock_failed_takes_the_dashboards_messages);
    RUN_TEST(test_a_session_the_box_ended_ignores_the_dashboard);
    RUN_TEST(test_time_added_after_a_failed_unlock_returns_to_the_locked_pages);
    RUN_TEST(test_a_freeze_after_a_failed_unlock_returns_to_the_locked_pages);
    RUN_TEST(test_the_error_page_stays_while_the_session_is_still_at_its_end);
    RUN_TEST(test_the_error_page_of_an_ended_lock_does_not_return_to_a_locked_page);
    RUN_TEST(test_a_clear_on_the_error_page_tries_the_unlock_again);
    RUN_TEST(test_the_error_page_keeps_its_buttons);
    RUN_TEST(test_a_restart_after_a_failed_unlock_restores_the_running_session);
    RUN_TEST(test_a_restart_after_a_failed_unlock_never_opens_on_a_bad_reading);
    RUN_TEST(test_a_restart_before_the_check_does_not_finish_the_session);
    RUN_TEST(test_a_local_timer_stays_local_until_its_paddle_has_opened);
    RUN_TEST(test_a_timer_that_runs_out_during_a_break_finishes_the_session);
    RUN_TEST(test_a_failed_unlock_during_a_break_does_not_invent_a_held_lock);
    RUN_TEST(test_a_stale_break_record_beside_a_failed_unlock_is_still_ignored);
    RUN_TEST(test_a_dashboard_clear_ends_the_lock_whatever_the_paddle_does);
    RUN_TEST(test_a_clear_during_the_unlock_is_not_overwritten);
    RUN_TEST(test_a_lock_during_the_unlock_replaces_it);
    return UNITY_END();
}

int main() { return runUnityTests(); }
