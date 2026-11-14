#include <Arduino.h>
#include <unity.h>

#include "../../src/state/Events.h"

using namespace sml;

struct Context {
    bool local = true;
    bool ready = true;
    bool acceptance = true;
    bool breakEnding = false;
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_local_timer_releases_paddle_once);
    RUN_TEST(test_local_timer_expires_from_menus);
    RUN_TEST(test_timer_expires_from_the_vote_screen);
    RUN_TEST(test_managed_timer_waits_for_wearer);
    RUN_TEST(test_restored_local_timer_releases_from_ready_pages);
    RUN_TEST(test_local_break_expiry_does_not_relock);
    RUN_TEST(test_ineligible_timer_does_not_move_motor);
    RUN_TEST(test_session_change_before_acceptance_prevents_release);
    return UNITY_END();
}
