#include <Arduino.h>
#include <unity.h>

#include "../../src/state/Events.h"
#include "../../src/utils/countdownWatch.h"
#include "../../src/utils/lockMenuCountdown.h"

using namespace sml;

struct Context {
    bool local = true;
    bool ready = true;
    bool acceptance = true;
    bool breakEnding = false;
    bool chaster = false;
    int unlocks = 0;
    int locks = 0;
    int clearedBreaks = 0;
};

struct Fixture {
    auto operator()() const {
        auto isReadyToUnlock = [](Context& c) { return c.ready; };
        auto acceptLocalTimerUnlock = [](Context& c) {
            return c.local && c.ready && c.acceptance;
        };
        auto isBreakEndRequested = [](Context& c) { return c.breakEnding; };
        auto isChasterSession = [](Context& c) { return c.chaster; };
        auto clearLocalPlay = [](Context& c) { c.local = false; };
        auto clearActiveBreak = [](Context& c) { ++c.clearedBreaks; };
        auto unlockAction = [](Context& c) { ++c.unlocks; };
        auto movePaddleToLock = [](Context& c) { ++c.locks; };
        return make_transition_table(
            *"fixture"_s + event<Done> = "fixture"_s,
#include "../../src/state/TimerExpiryTransitions.inc"
            "locking"_s + event<Done> = "home"_s
        );
    }
};
using Machine = sml::sm<Fixture, sml::testing>;

void setUp() {}
void tearDown() {}

void test_local_timer_releases_paddle_once() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.timer"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
    TEST_ASSERT_FALSE(c.local);
    machine.process_event(TimerExpired{});
    machine.process_event(Done{});
    TEST_ASSERT_TRUE(machine.is("home"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.locks);
}

void test_local_timer_expires_from_menus() {
    Context c;
    Machine machine{c};
    machine.set_current_states("lockMenu"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    c.local = true;
    machine.set_current_states("lockMenu.sleep"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(2, c.unlocks);
}

void test_timer_expires_from_the_vote_screen() {
    Context c;
    Machine machine{c};
    machine.set_current_states("lockMenu.vote"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);

    // A managed session shows Ready to unlock instead.
    c.local = false;
    machine.set_current_states("lockMenu.vote"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locked.ready"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
}

void test_managed_timer_waits_for_wearer() {
    Context c;
    c.local = false;
    Machine machine{c};
    machine.set_current_states("locked.timer"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locked.ready"_s));
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locked.ready"_s));
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
}

void test_restored_local_timer_releases_from_ready_pages() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.ready"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    c.local = true;
    machine.set_current_states("locked.ready.addTime"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(2, c.unlocks);
}

void test_local_break_expiry_does_not_relock() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.break"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.locks);
}

void test_managed_break_relocks_after_lock_window() {
    Context c;
    c.local = false;
    Machine machine{c};
    machine.set_current_states("locked.break"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.locks);
    TEST_ASSERT_EQUAL_INT(1, c.clearedBreaks);
}

// A dashboard break on a Chaster session opened from Ready to unlock, where
// the unlock timer keeps sending TimerExpired, stays open.
void test_chaster_break_outlives_lock_window() {
    Context c;
    c.local = false;
    c.chaster = true;
    Machine machine{c};
    machine.set_current_states("locked.break"_s);
    machine.process_event(TimerExpired{});
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locked.break"_s));
    TEST_ASSERT_EQUAL_INT(0, c.locks);
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.clearedBreaks);
}

void test_ineligible_timer_does_not_move_motor() {
    Context c;
    c.ready = false;
    Machine machine{c};
    machine.set_current_states("locked.timer"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_TRUE(machine.is("locked.timer"_s));
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
}

void test_session_change_before_acceptance_prevents_release() {
    Context c;
    c.acceptance = false;
    Machine machine{c};
    machine.set_current_states("locked.timer"_s);
    machine.process_event(TimerExpired{});
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
}

// The lock menu runs the countdown again when it is entered without one
// (from the break pages, say), so it still moves on at the deadline.
void test_lock_menu_counts_down_a_timer_that_has_not_run_out() {
    TEST_ASSERT_TRUE(lockMenuCountsDown(true, false, false));
    TEST_ASSERT_TRUE(lockMenuCountsDown(true, true, false));
}

// A session that is ready to unlock must not be sent back to Ready to unlock
// the moment the menu opens: the menu, with its emergency unlock, is where
// the page that says Chaster refused leads.
void test_lock_menu_stays_open_for_a_session_ready_to_unlock() {
    TEST_ASSERT_FALSE(lockMenuCountsDown(true, false, true));
}

// A local timer that has run out is released wherever the wearer is.
void test_lock_menu_releases_a_local_timer_that_has_run_out() {
    TEST_ASSERT_TRUE(lockMenuCountsDown(true, true, true));
}

// A lock without a timer has nothing to count.
void test_lock_menu_has_no_countdown_without_a_timer() {
    TEST_ASSERT_FALSE(lockMenuCountsDown(false, false, false));
    TEST_ASSERT_FALSE(lockMenuCountsDown(false, true, true));
    TEST_ASSERT_FALSE(lockMenuCountsDown(false, false, true));
}

// While the countdown task cannot be created, the delay task makes the same
// decision once a second (utils/countdownWatch.h).
namespace {
CountdownView countdownAt(uint64_t endMs, uint64_t nowMs) {
    CountdownView view;
    view.endMs = endMs;
    view.nowMs = nowMs;
    return view;
}
}  // namespace

void test_watch_leaves_a_running_countdown_alone() {
    TEST_ASSERT_TRUE(countdownDue(countdownAt(60000, 1000)) ==
                     CountdownDue::NOTHING);
    TEST_ASSERT_TRUE(countdownDue(countdownAt(60000, 59999)) ==
                     CountdownDue::NOTHING);
}

// R8, 2026-11-16: the countdown task could not be created 22 times in a row.
// A local timer must still release at its deadline.
void test_watch_reports_a_deadline_that_has_passed() {
    TEST_ASSERT_TRUE(countdownDue(countdownAt(60000, 60000)) ==
                     CountdownDue::TIMER_EXPIRED);
    TEST_ASSERT_TRUE(countdownDue(countdownAt(60000, 90000)) ==
                     CountdownDue::TIMER_EXPIRED);
    // On Ready to unlock too: a local timer restored there is released.
    CountdownView ready = countdownAt(60000, 90000);
    ready.readyPage = true;
    TEST_ASSERT_TRUE(countdownDue(ready) == CountdownDue::TIMER_EXPIRED);
}

void test_watch_ends_a_lockout_that_has_run_out() {
    CountdownView lockout = countdownAt(60000, 59999);
    lockout.lockout = true;
    TEST_ASSERT_TRUE(countdownDue(lockout) == CountdownDue::NOTHING);
    lockout.nowMs = 60000;
    TEST_ASSERT_TRUE(countdownDue(lockout) == CountdownDue::LOCKOUT_OVER);
    // A freeze left on the session the lockout ended does not hold it.
    lockout.frozen = true;
    TEST_ASSERT_TRUE(countdownDue(lockout) == CountdownDue::LOCKOUT_OVER);
}

void test_watch_never_expires_a_held_session() {
    CountdownView held = countdownAt(60000, 90000);
    held.frozen = true;
    TEST_ASSERT_TRUE(countdownDue(held) == CountdownDue::NOTHING);
    // From Ready to unlock a held session goes back to its own page.
    held.readyPage = true;
    TEST_ASSERT_TRUE(countdownDue(held) == CountdownDue::BACK_TO_COUNTDOWN);
}

void test_watch_returns_to_the_countdown_when_time_was_added() {
    CountdownView ready = countdownAt(120000, 90000);
    ready.readyPage = true;
    TEST_ASSERT_TRUE(countdownDue(ready) == CountdownDue::BACK_TO_COUNTDOWN);
}

// The decision reaches the transitions the countdown task drives.
void test_watch_decision_releases_a_local_timer() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.timer"_s);
    if (countdownDue(countdownAt(60000, 60000)) ==
        CountdownDue::TIMER_EXPIRED) {
        machine.process_event(TimerExpired{});
    }
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_local_timer_releases_paddle_once);
    RUN_TEST(test_local_timer_expires_from_menus);
    RUN_TEST(test_timer_expires_from_the_vote_screen);
    RUN_TEST(test_managed_timer_waits_for_wearer);
    RUN_TEST(test_restored_local_timer_releases_from_ready_pages);
    RUN_TEST(test_local_break_expiry_does_not_relock);
    RUN_TEST(test_managed_break_relocks_after_lock_window);
    RUN_TEST(test_chaster_break_outlives_lock_window);
    RUN_TEST(test_ineligible_timer_does_not_move_motor);
    RUN_TEST(test_session_change_before_acceptance_prevents_release);
    RUN_TEST(test_lock_menu_counts_down_a_timer_that_has_not_run_out);
    RUN_TEST(test_lock_menu_stays_open_for_a_session_ready_to_unlock);
    RUN_TEST(test_lock_menu_releases_a_local_timer_that_has_run_out);
    RUN_TEST(test_lock_menu_has_no_countdown_without_a_timer);
    RUN_TEST(test_watch_leaves_a_running_countdown_alone);
    RUN_TEST(test_watch_reports_a_deadline_that_has_passed);
    RUN_TEST(test_watch_ends_a_lockout_that_has_run_out);
    RUN_TEST(test_watch_never_expires_a_held_session);
    RUN_TEST(test_watch_returns_to_the_countdown_when_time_was_added);
    RUN_TEST(test_watch_decision_releases_a_local_timer);
    return UNITY_END();
}
