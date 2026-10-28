#include <Arduino.h>
#include <unity.h>

#include "../../src/state/Events.h"
#include "../../src/utils/syncEffect.h"

using namespace sml;

// Hardware boundaries only are stubbed. The transition rows below are the
// exact fragment used by LKBX::InnerStateMachine, not a copy of its behavior.
enum class MenuItemE { BACK };
namespace Str { namespace Protocol { constexpr const char* LOCKED = "locked"; } }

struct Context {
    bool locked = true;
    bool frozen = false;
    bool timed = true;
    bool expired = false;
    bool emergency = false;
    bool backOption = true;
    int reports = 0;
    int draws = 0;
    int motors = 0;
    int returns = 0;
};
struct Noop {};
struct Fixture {
    auto operator()() const {
        auto isLocked = [](Context& c) { return c.locked; };
        auto isFrozen = [](Context& c) { return c.frozen; };
        auto isLockedTimer = [](Context& c) { return c.locked && c.timed; };
        auto isReadyToUnlock = [](Context& c) {
            return c.locked && c.timed && c.expired && !c.frozen && !c.emergency;
        };
        auto isEmergencyBlocked = [](Context& c) { return c.emergency; };
        auto sendMessage = [](const char*) {
            return [](Context& c) { ++c.reports; };
        };
        auto drawLockedActions = [](Context& c) { ++c.draws; };
        auto startUnlockTimer = [](Context&) {};
        auto switchToTimer = [](Context& c) { c.timed = true; };
        auto switchToLock = [](Context& c) { c.timed = false; };
        auto clearActiveBreak = [](Context&) {};
        auto movePaddleToUnlock = [](Context& c) { ++c.motors; };
        auto clearLocalPlay = [](Context&) {};
        auto unlockAction = [](Context& c) { c.locked = false; ++c.motors; };
        // Production dispatches Done reentrantly; tests deliver that event
        // explicitly after asserting the intermediate return route.
        auto returnToLockScreen = [](Context& c) { ++c.returns; };
        auto isOption = [](MenuItemE) {
            return [](Context& c) { return c.backOption; };
        };
        return make_transition_table(
            *"fixture"_s + event<Noop> = "fixture"_s,
            "lockMenu.sleep"_s + event<Noop> = "lockMenu.sleep"_s,
#include "../../src/state/FrozenTransitions.inc"
            "fixture"_s + event<Done> = "fixture"_s
        );
    }
};
using Machine = sml::sm<Fixture, sml::testing>;

void setUp() {}
void tearDown() {}

void test_frozen_preflight_reports_physical_acceptance_once() {
    Context c;
    c.frozen = true;
    c.expired = true;
    Machine machine{c};
    machine.set_current_states("locking.preflight"_s);
    machine.process_event(Done{});
    TEST_ASSERT_TRUE(machine.is("locked.frozen"_s));
    TEST_ASSERT_EQUAL_INT(1, c.reports);
    TEST_ASSERT_EQUAL_INT(0, c.motors);
    machine.process_event(Done{});
    TEST_ASSERT_EQUAL_INT(1, c.reports);
}

void test_emergency_and_unlocked_preflight_do_not_enter_frozen() {
    Context c;
    c.frozen = true;
    c.emergency = true;
    Machine machine{c};
    machine.set_current_states("locking.preflight"_s);
    machine.process_event(Done{});
    TEST_ASSERT_TRUE(machine.is("emergency.blocked"_s));
    TEST_ASSERT_EQUAL_INT(0, c.reports);
    c.emergency = false;
    c.locked = false;
    machine.set_current_states("locking.preflight"_s);
    machine.process_event(Done{});
    TEST_ASSERT_FALSE(machine.is("locked.frozen"_s));
    TEST_ASSERT_EQUAL_INT(0, c.reports);
}

void test_freeze_enters_from_timer_ready_and_permanent_without_motor() {
    Context c;
    c.frozen = true;
    c.expired = true;
    Machine machine{c};
    machine.set_current_states("locked.timer"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked.frozen"_s));
    machine.set_current_states("locked.ready"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked.frozen"_s));
    c.timed = false;
    machine.set_current_states("locked"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked.frozen"_s));
    TEST_ASSERT_EQUAL_INT(3, c.draws);
    TEST_ASSERT_EQUAL_INT(0, c.reports);
    TEST_ASSERT_EQUAL_INT(0, c.motors);
}

void test_duplicate_freeze_and_timer_expiry_do_not_reenter_or_unlock() {
    Context c;
    c.frozen = true;
    c.expired = true;
    Machine machine{c};
    machine.set_current_states("locked.timer"_s);
    machine.process_event(FreezeChange{});
    for (int i = 0; i < 10; ++i) {
        machine.process_event(FreezeChange{});
        machine.process_event(TimerExpired{});
    }
    TEST_ASSERT_TRUE(machine.is("locked.frozen"_s));
    TEST_ASSERT_EQUAL_INT(1, c.draws);
    TEST_ASSERT_EQUAL_INT(0, c.reports);
    TEST_ASSERT_EQUAL_INT(0, c.motors);
}

void test_menu_preserves_focus_and_back_returns_without_locking() {
    Context c;
    c.frozen = true;
    Machine machine{c};
    machine.set_current_states("locked.frozen"_s);
    machine.process_event(ButtonPress{});
    TEST_ASSERT_TRUE(machine.is("lockMenu"_s));
    for (bool frozen : {false, true}) {
        c.frozen = frozen;
        machine.process_event(FreezeChange{});
        TEST_ASSERT_TRUE(machine.is("lockMenu"_s));
    }
    machine.process_event(BackPress{});
    TEST_ASSERT_TRUE(machine.is("locked.return"_s));
    machine.process_event(Done{});
    TEST_ASSERT_TRUE(machine.is("locked.frozen"_s));
    TEST_ASSERT_EQUAL_INT(1, c.returns);
    TEST_ASSERT_EQUAL_INT(0, c.reports);
    TEST_ASSERT_EQUAL_INT(0, c.motors);
}

void test_back_option_selects_current_timer_ready_or_permanent_state() {
    Context c;
    Machine machine{c};
    machine.set_current_states("lockMenu"_s);
    machine.process_event(ButtonPress{});
    machine.process_event(Done{});
    TEST_ASSERT_TRUE(machine.is("locked.timer"_s));
    c.expired = true;
    machine.set_current_states("lockMenu"_s);
    machine.process_event(ButtonPress{});
    machine.process_event(Done{});
    TEST_ASSERT_TRUE(machine.is("locked.ready"_s));
    c.timed = false;
    machine.set_current_states("lockMenu"_s);
    machine.process_event(ButtonPress{});
    machine.process_event(Done{});
    TEST_ASSERT_TRUE(machine.is("locked"_s));
    TEST_ASSERT_EQUAL_INT(0, c.reports);
    TEST_ASSERT_EQUAL_INT(0, c.motors);
}

void test_sleep_break_and_emergency_ignore_freeze_updates() {
    Context c;
    c.frozen = true;
    Machine machine{c};
    machine.set_current_states("lockMenu.sleep"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("lockMenu.sleep"_s));
    machine.set_current_states("locked.break"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked.break"_s));
    machine.set_current_states("emergency.blocked"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("emergency.blocked"_s));
    TEST_ASSERT_EQUAL_INT(0, c.motors);
}

void test_unfreeze_selects_timer_ready_or_permanent_without_motor() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.frozen"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked.timer"_s));
    c.expired = true;
    machine.set_current_states("locked.frozen"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked.ready"_s));
    c.timed = false;
    machine.set_current_states("locked.frozen"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked"_s));
    TEST_ASSERT_EQUAL_INT(0, c.reports);
    TEST_ASSERT_EQUAL_INT(0, c.motors);
}

void test_explicit_unlock_break_and_new_offer_keep_their_routes() {
    Context c;
    c.frozen = true;
    Machine machine{c};
    machine.set_current_states("locked.frozen"_s);
    machine.process_event(Break{});
    TEST_ASSERT_TRUE(machine.is("locked.break"_s));
    machine.set_current_states("locked.frozen"_s);
    machine.process_event(LockOffered{});
    TEST_ASSERT_TRUE(machine.is("acceptLock"_s));
    TEST_ASSERT_EQUAL_INT(1, c.motors);
    machine.set_current_states("locked.frozen"_s);
    machine.process_event(Unlock{});
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(2, c.motors);
    TEST_ASSERT_FALSE(c.locked);
}

void test_metadata_reconciles_timer_modes_without_motor() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked"_s);
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked.timer"_s));
    c.timed = false;
    machine.process_event(FreezeChange{});
    TEST_ASSERT_TRUE(machine.is("locked"_s));
    TEST_ASSERT_EQUAL_INT(0, c.reports);
    TEST_ASSERT_EQUAL_INT(0, c.motors);
}

void test_runtime_dispatch_suppresses_duplicates_but_preserves_relock() {
    TEST_ASSERT_TRUE(classifySyncEffect(true, "locked", "locked", false) == SyncEffect::NONE);
    TEST_ASSERT_TRUE(classifySyncEffect(true, "locked", "locked", true) == SyncEffect::TIMER_META);
    TEST_ASSERT_TRUE(classifySyncEffect(true, "break", "locked", false) == SyncEffect::MOTOR_LOCK);
    TEST_ASSERT_TRUE(classifySyncEffect(true, "break_ending", "locked", true) == SyncEffect::MOTOR_LOCK);
    TEST_ASSERT_TRUE(classifySyncEffect(false, "locked", "locked", false) == SyncEffect::MOTOR_LOCK);
    TEST_ASSERT_TRUE(classifySyncEffect(false, "", "pending", true) == SyncEffect::OFFER);
    TEST_ASSERT_TRUE(classifySyncEffect(true, "locked", "break", true) == SyncEffect::BREAK);
    TEST_ASSERT_TRUE(classifySyncEffect(true, "locked", "unlocked", true) == SyncEffect::MOTOR_UNLOCK);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_frozen_preflight_reports_physical_acceptance_once);
    RUN_TEST(test_emergency_and_unlocked_preflight_do_not_enter_frozen);
    RUN_TEST(test_freeze_enters_from_timer_ready_and_permanent_without_motor);
    RUN_TEST(test_duplicate_freeze_and_timer_expiry_do_not_reenter_or_unlock);
    RUN_TEST(test_menu_preserves_focus_and_back_returns_without_locking);
    RUN_TEST(test_back_option_selects_current_timer_ready_or_permanent_state);
    RUN_TEST(test_sleep_break_and_emergency_ignore_freeze_updates);
    RUN_TEST(test_unfreeze_selects_timer_ready_or_permanent_without_motor);
    RUN_TEST(test_explicit_unlock_break_and_new_offer_keep_their_routes);
    RUN_TEST(test_metadata_reconciles_timer_modes_without_motor);
    RUN_TEST(test_runtime_dispatch_suppresses_duplicates_but_preserves_relock);
    return UNITY_END();
}
