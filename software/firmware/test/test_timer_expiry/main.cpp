#include <Arduino.h>
#include <unity.h>

#include "../../src/state/Events.h"
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_local_timer_releases_paddle_once);
    RUN_TEST(test_local_timer_expires_from_menus);
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
    return UNITY_END();
}
