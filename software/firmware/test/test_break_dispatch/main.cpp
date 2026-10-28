#include <Arduino.h>
#include <unity.h>

#include "../../src/state/Events.h"

using namespace sml;
namespace Str { namespace Protocol { constexpr const char* BREAK = "break"; } }

struct Context {
    bool active = false;
    uint64_t startedAt = 0;
    int unlocks = 0;
    int timers = 0;
    int clearedReports = 0;
    int reports = 0;
    bool reportHadTiming = false;
};
struct Fixture {
    auto operator()() const {
        auto movePaddleToUnlock = [](Context& c) { ++c.unlocks; };
        auto startBreakTimer = [](Context& c) {
            ++c.timers;
            if (!c.active) {
                c.active = true;
                c.startedAt = 1700000000000ULL;
            }
        };
        auto clearLockedReport = [](Context& c) { ++c.clearedReports; };
        auto sendMessage = [](const char*) {
            return [](Context& c) {
                ++c.reports;
                c.reportHadTiming = c.active && c.startedAt != 0;
            };
        };
        return make_transition_table(
            *"fixture"_s + done = "fixture"_s,
#include "../../src/state/BreakEntryTransitions.inc"
            "init.resume"_s + done = "locked.break"_s
        );
    }
};
using Machine = sml::sm<Fixture, sml::testing>;

void setUp() {}
void tearDown() {}

template<class State> void assertBreakFrom(State state) {
    Context c;
    Machine machine{c};
    machine.set_current_states(state);
    TEST_ASSERT_TRUE(machine.process_event(Break{}));
    TEST_ASSERT_TRUE(machine.is("locked.break"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
    TEST_ASSERT_EQUAL_INT(1, c.timers);
    TEST_ASSERT_EQUAL_INT(1, c.clearedReports);
    TEST_ASSERT_EQUAL_INT(1, c.reports);
    TEST_ASSERT_TRUE(c.reportHadTiming);
}

void test_dashboard_break_reports_from_permanent_lock() {
    assertBreakFrom("locked"_s);
}
void test_dashboard_break_reports_from_timer_lock() {
    assertBreakFrom("locked.timer"_s);
}
void test_dashboard_break_reports_from_lock_menu() {
    assertBreakFrom("lockMenu"_s);
}
void test_local_preflight_reports_once_after_persisting_start() {
    assertBreakFrom("locked.breakPreflight"_s);
}
void test_duplicate_break_does_not_restart_or_report() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.timer"_s);
    machine.process_event(Break{});
    TEST_ASSERT_FALSE(machine.process_event(Break{}));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
    TEST_ASSERT_EQUAL_INT(1, c.timers);
    TEST_ASSERT_EQUAL_INT(1, c.reports);
}
void test_resumed_break_keeps_original_start_in_report() {
    Context c;
    c.active = true;
    c.startedAt = 1699999999000ULL;
    Machine machine{c};
    machine.set_current_states("init.resume"_s);
    machine.process_event(Done{});
    TEST_ASSERT_EQUAL_UINT64(1699999999000ULL, c.startedAt);
    TEST_ASSERT_EQUAL_INT(1, c.reports);
    TEST_ASSERT_TRUE(c.reportHadTiming);
}
void test_unrelated_state_rejects_break_without_reporting() {
    Context c;
    Machine machine{c};
    TEST_ASSERT_FALSE(machine.process_event(Break{}));
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.timers);
    TEST_ASSERT_EQUAL_INT(0, c.reports);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dashboard_break_reports_from_permanent_lock);
    RUN_TEST(test_dashboard_break_reports_from_timer_lock);
    RUN_TEST(test_dashboard_break_reports_from_lock_menu);
    RUN_TEST(test_local_preflight_reports_once_after_persisting_start);
    RUN_TEST(test_duplicate_break_does_not_restart_or_report);
    RUN_TEST(test_resumed_break_keeps_original_start_in_report);
    RUN_TEST(test_unrelated_state_rejects_break_without_reporting);
    return UNITY_END();
}
