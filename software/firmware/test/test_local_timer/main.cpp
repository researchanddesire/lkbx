#include <Arduino.h>
#include <unity.h>

#include "../../src/state/Events.h"
#include "LockboxDuration.h"

using namespace sml;

// Hardware boundaries only are stubbed. The transition rows below are the
// exact fragment used by LKBX::InnerStateMachine, and the guard applies the
// same deadline rule as saveLocalLockTimerDuration (LockboxDuration.h) before
// its write.

namespace {
constexpr int64_t kNow = 1789188000000LL;
constexpr uint64_t kTenMinutes = 600000ULL;
}  // namespace

struct Context {
    // What the timer page holds: zero until it has drawn a duration.
    uint64_t duration = 0;
    // Whether storage takes the deadline.
    bool storageWorks = true;
    uint64_t savedDeadline = 0;
    int saves = 0;
    int locks = 0;
    int notices = 0;
};

struct Fixture {
    auto operator()() const {
        auto saveLocalTimer = [](Context& c) {
            uint64_t deadline = 0;
            if (!lockbox::security::localTimerDeadline(kNow, c.duration,
                                                       deadline) ||
                !c.storageWorks) {
                return false;
            }
            c.savedDeadline = deadline;
            ++c.saves;
            return true;
        };
        auto lockTimerAction = [](Context& c) { ++c.locks; };
        auto showTimerUnavailable = [](Context& c) { ++c.notices; };
        return make_transition_table(
            *"fixture"_s + event<Done> = "fixture"_s,
#include "../../src/state/LocalTimerTransitions.inc"
            "timer"_s + backPress = "home.idle"_s,
            "locking"_s + event<Done> = "home"_s
        );
    }
};
using Machine = sml::sm<Fixture, sml::testing>;

void setUp() {}
void tearDown() {}

void test_a_shown_duration_locks_with_its_deadline_saved() {
    Context c;
    c.duration = kTenMinutes;
    Machine machine{c};
    machine.set_current_states("timer"_s);
    TEST_ASSERT_TRUE(machine.process_event(ButtonPress{}));
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.locks);
    TEST_ASSERT_EQUAL_INT(1, c.saves);
    TEST_ASSERT_EQUAL_INT(0, c.notices);
    TEST_ASSERT_TRUE(c.savedDeadline ==
                     static_cast<uint64_t>(kNow) + kTenMinutes);
}

// 2026-11-15, R8: the page's task could not be created, so the page held no
// duration, and Lock locked the box with no deadline.
void test_a_page_that_never_drew_does_not_lock() {
    Context c;
    Machine machine{c};
    machine.set_current_states("timer"_s);
    machine.process_event(ButtonPress{});
    TEST_ASSERT_TRUE(machine.is("timer"_s));
    TEST_ASSERT_EQUAL_INT(0, c.locks);
    TEST_ASSERT_EQUAL_INT(0, c.saves);
    TEST_ASSERT_EQUAL_INT(1, c.notices);
    TEST_ASSERT_TRUE(c.savedDeadline == 0);
}

void test_an_unsaved_deadline_does_not_lock() {
    Context c;
    c.duration = kTenMinutes;
    c.storageWorks = false;
    Machine machine{c};
    machine.set_current_states("timer"_s);
    machine.process_event(ButtonPress{});
    TEST_ASSERT_TRUE(machine.is("timer"_s));
    TEST_ASSERT_EQUAL_INT(0, c.locks);
    TEST_ASSERT_EQUAL_INT(1, c.notices);
}

void test_an_impossible_duration_does_not_lock() {
    const uint64_t impossible[] = {
        0, lockbox::security::MAX_TIMER_DURATION_MS + 1, UINT64_MAX};
    for (uint64_t duration : impossible) {
        Context c;
        c.duration = duration;
        Machine machine{c};
        machine.set_current_states("timer"_s);
        machine.process_event(ButtonPress{});
        TEST_ASSERT_TRUE(machine.is("timer"_s));
        TEST_ASSERT_EQUAL_INT(0, c.locks);
        TEST_ASSERT_EQUAL_INT(1, c.notices);
    }
}

// Every refused press says so again, and none of them moves the motor.
void test_a_refused_press_can_be_repeated_without_locking() {
    Context c;
    Machine machine{c};
    machine.set_current_states("timer"_s);
    for (int press = 1; press <= 3; ++press) {
        machine.process_event(ButtonPress{});
        TEST_ASSERT_TRUE(machine.is("timer"_s));
        TEST_ASSERT_EQUAL_INT(press, c.notices);
    }
    TEST_ASSERT_EQUAL_INT(0, c.locks);
}

// Once the page has drawn a duration the same press locks.
void test_the_press_locks_once_the_page_has_a_duration() {
    Context c;
    Machine machine{c};
    machine.set_current_states("timer"_s);
    machine.process_event(ButtonPress{});
    TEST_ASSERT_EQUAL_INT(0, c.locks);
    c.duration = kTenMinutes;
    machine.process_event(ButtonPress{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.locks);
    TEST_ASSERT_EQUAL_INT(1, c.saves);
}

void test_back_still_leaves_the_refused_page() {
    Context c;
    Machine machine{c};
    machine.set_current_states("timer"_s);
    machine.process_event(ButtonPress{});
    TEST_ASSERT_TRUE(machine.process_event(BackPress{}));
    TEST_ASSERT_TRUE(machine.is("home.idle"_s));
    TEST_ASSERT_EQUAL_INT(0, c.locks);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_shown_duration_locks_with_its_deadline_saved);
    RUN_TEST(test_a_page_that_never_drew_does_not_lock);
    RUN_TEST(test_an_unsaved_deadline_does_not_lock);
    RUN_TEST(test_an_impossible_duration_does_not_lock);
    RUN_TEST(test_a_refused_press_can_be_repeated_without_locking);
    RUN_TEST(test_the_press_locks_once_the_page_has_a_duration);
    RUN_TEST(test_back_still_leaves_the_refused_page);
    return UNITY_END();
}
