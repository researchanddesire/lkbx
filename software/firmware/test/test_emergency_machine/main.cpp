#include <Arduino.h>
#include <unity.h>

#include "../../src/state/Events.h"

using namespace sml;

// Hardware boundaries only are stubbed. The transition rows below are the
// exact fragment used by LKBX::InnerStateMachine, not a copy of its behavior.
struct Context {
    bool held = true;
    int declines = 0;
    int clears = 0;
    int motors = 0;
    int unlocks = 0;
    int locks = 0;
    int timers = 0;
};
struct Noop {};
struct Fixture {
    auto operator()() const {
        auto isEmergencyHeld = [](Context& c) { return c.held; };
        auto declineLockAction = [](Context& c) { ++c.declines; };
        auto clearActiveBreak = [](Context&) {};
        auto movePaddleToUnlock = [](Context& c) { ++c.motors; };
        auto clearEBlock = [](Context& c) { ++c.clears; };
        auto clearLocalPlay = [](Context&) {};
        auto unlockAction = [](Context& c) { ++c.unlocks; };
        auto lockAction = [](Context& c) { ++c.locks; };
        auto lockTimerAction = [](Context& c) { ++c.timers; };
        return make_transition_table(
            *"fixture"_s + event<Noop> = "fixture"_s,
#include "../../src/state/EmergencyTransitions.inc"
            "fixture"_s + event<Done> = "fixture"_s
        );
    }
};
using Machine = sml::sm<Fixture, sml::testing>;

void setUp() {}
void tearDown() {}

void test_held_block_ignores_remote_unlock_and_locks() {
    Context c;
    Machine machine{c};
    machine.set_current_states("emergency.blocked"_s);
    machine.process_event(Unlock{});
    machine.process_event(Lock{});
    machine.process_event(LockTimer{});
    TEST_ASSERT_TRUE(machine.is("emergency.blocked"_s));
    TEST_ASSERT_EQUAL_INT(0, c.clears);
    TEST_ASSERT_EQUAL_INT(0, c.unlocks + c.locks + c.timers);
}

void test_held_block_declines_an_offer_without_moving() {
    Context c;
    Machine machine{c};
    machine.set_current_states("emergency.blocked"_s);
    machine.process_event(LockOffered{});
    TEST_ASSERT_TRUE(machine.is("emergency.blocked"_s));
    TEST_ASSERT_EQUAL_INT(1, c.declines);
    TEST_ASSERT_EQUAL_INT(0, c.motors);
    TEST_ASSERT_EQUAL_INT(0, c.clears);
}

void test_held_wifi_screen_holds_the_same_line() {
    Context c;
    Machine machine{c};
    machine.set_current_states("emergency.wifi"_s);
    machine.process_event(Unlock{});
    machine.process_event(Lock{});
    machine.process_event(LockTimer{});
    machine.process_event(LockOffered{});
    TEST_ASSERT_TRUE(machine.is("emergency.wifi"_s));
    TEST_ASSERT_EQUAL_INT(1, c.declines);
    TEST_ASSERT_EQUAL_INT(0, c.clears);
    TEST_ASSERT_EQUAL_INT(0, c.motors + c.unlocks + c.locks + c.timers);
}

void test_expired_block_releases_on_unlock() {
    Context c;
    c.held = false;
    Machine machine{c};
    machine.set_current_states("emergency.blocked"_s);
    machine.process_event(Unlock{});
    TEST_ASSERT_TRUE(machine.is("home"_s));
    TEST_ASSERT_EQUAL_INT(1, c.clears);
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
}

void test_expired_block_takes_an_offer() {
    Context c;
    c.held = false;
    Machine machine{c};
    machine.set_current_states("emergency.blocked"_s);
    machine.process_event(LockOffered{});
    TEST_ASSERT_TRUE(machine.is("acceptLock"_s));
    TEST_ASSERT_EQUAL_INT(0, c.declines);
    TEST_ASSERT_EQUAL_INT(1, c.motors);
}

void test_expired_block_takes_locks() {
    {
        Context c;
        c.held = false;
        Machine machine{c};
        machine.set_current_states("emergency.blocked"_s);
        machine.process_event(Lock{});
        TEST_ASSERT_TRUE(machine.is("locking"_s));
        TEST_ASSERT_EQUAL_INT(1, c.clears);
        TEST_ASSERT_EQUAL_INT(1, c.locks);
    }
    {
        Context c;
        c.held = false;
        Machine machine{c};
        machine.set_current_states("emergency.blocked"_s);
        machine.process_event(LockTimer{});
        TEST_ASSERT_TRUE(machine.is("locking"_s));
        TEST_ASSERT_EQUAL_INT(1, c.clears);
        TEST_ASSERT_EQUAL_INT(1, c.timers);
    }
}

void test_expired_wifi_screen_releases_and_accepts() {
    {
        Context c;
        c.held = false;
        Machine machine{c};
        machine.set_current_states("emergency.wifi"_s);
        machine.process_event(Unlock{});
        TEST_ASSERT_TRUE(machine.is("home"_s));
        TEST_ASSERT_EQUAL_INT(1, c.clears);
    }
    {
        Context c;
        c.held = false;
        Machine machine{c};
        machine.set_current_states("emergency.wifi"_s);
        machine.process_event(LockOffered{});
        TEST_ASSERT_TRUE(machine.is("acceptLock"_s));
        TEST_ASSERT_EQUAL_INT(0, c.declines);
    }
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_held_block_ignores_remote_unlock_and_locks);
    RUN_TEST(test_held_block_declines_an_offer_without_moving);
    RUN_TEST(test_held_wifi_screen_holds_the_same_line);
    RUN_TEST(test_expired_block_releases_on_unlock);
    RUN_TEST(test_expired_block_takes_an_offer);
    RUN_TEST(test_expired_block_takes_locks);
    RUN_TEST(test_expired_wifi_screen_releases_and_accepts);
    return UNITY_END();
}

int main(void) { return runUnityTests(); }
