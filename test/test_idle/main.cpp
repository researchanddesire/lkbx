#include <unity.h>

#include "../../src/services/lastInteraction.h"
#include "../../src/utils/idleTarget.h"
#include "caller.h"

extern unsigned long _mock_millis;
void runSleepTests();

void setUp() {
    idleState = IdleState::NOT_IDLE;
    lastInteraction = 0;
    _mock_millis = 0;
    sleepFake = {};
}

void tearDown() {}

// ---------------------------------------------------------------------------
// Cross-TU sharing: proves the static→extern fix works.
// If lastInteraction / idleState were still declared `static` in the header,
// writes in caller.cpp would update caller.cpp's private copy and these
// assertions would fail because main.cpp reads its own separate copy.
// ---------------------------------------------------------------------------

void test_setNotIdle_from_other_TU_wakes_idleState() {
    idleState = IdleState::PSEUDO_SLEEP;
    callerSetNotIdle(5000);
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleState);
}

void test_setNotIdle_from_other_TU_updates_lastInteraction() {
    lastInteraction = 1000;
    callerSetNotIdle(5000);
    TEST_ASSERT_EQUAL_UINT32(5000, lastInteraction);
}

void test_setNotIdleISR_from_other_TU_wakes_idleState() {
    idleState = IdleState::IDLE;
    lastInteraction = 1000;
    callerSetNotIdleISR(8000);
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleState);
    TEST_ASSERT_EQUAL_UINT32(8000, lastInteraction);
}

// ---------------------------------------------------------------------------
// setNotIdle always bumps lastInteraction, even when already NOT_IDLE.
// This keeps the idle timer from starting to count while the user is active.
// ---------------------------------------------------------------------------

void test_setNotIdle_bumps_timestamp_when_already_not_idle() {
    idleState = IdleState::NOT_IDLE;
    lastInteraction = 1000;
    callerSetNotIdle(9000);
    TEST_ASSERT_EQUAL_UINT32(9000, lastInteraction);
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleState);
}

// ---------------------------------------------------------------------------
// idleTargetFromDelta: verify timeout thresholds.
// Default (non-SHORT_TIMEOUTS): IDLE 30s, PSEUDO_SLEEP 2min, SLEEP 10min.
// ---------------------------------------------------------------------------

void test_idleTarget_not_idle_when_fresh() {
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleTargetFromDelta(0, false));
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleTargetFromDelta(100, false));
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleTargetFromDelta(29999, false));
}

void test_idleTarget_idle_at_threshold() {
    TEST_ASSERT_EQUAL(IdleState::IDLE, idleTargetFromDelta(30000, false));
    TEST_ASSERT_EQUAL(IdleState::IDLE, idleTargetFromDelta(60000, false));
    TEST_ASSERT_EQUAL(IdleState::IDLE, idleTargetFromDelta(119999, false));
}

void test_idleTarget_pseudo_sleep_at_threshold() {
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP,
                      idleTargetFromDelta(120000, false));
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP,
                      idleTargetFromDelta(300000, false));
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP,
                      idleTargetFromDelta(599999, false));
}

void test_idleTarget_sleep_at_threshold() {
    TEST_ASSERT_EQUAL(IdleState::SLEEP, idleTargetFromDelta(600000, false));
    TEST_ASSERT_EQUAL(IdleState::SLEEP, idleTargetFromDelta(999999, false));
}

void test_idleTarget_charging_caps_at_pseudo_sleep() {
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP,
                      idleTargetFromDelta(600000, true));
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP,
                      idleTargetFromDelta(999999, true));
}

void test_idleTarget_charging_still_allows_idle() {
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleTargetFromDelta(100, true));
    TEST_ASSERT_EQUAL(IdleState::IDLE, idleTargetFromDelta(30000, true));
}

// ---------------------------------------------------------------------------
// Wake scenario: simulates device in PSEUDO_SLEEP, encoder ISR fires,
// then idleTargetFromDelta with fresh timestamp returns NOT_IDLE.
// ---------------------------------------------------------------------------

void test_wake_from_pseudo_sleep_via_interaction() {
    _mock_millis = 200000;
    lastInteraction = 0;
    idleState = IdleState::PSEUDO_SLEEP;

    callerSetNotIdleISR(200000);

    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleState);

    unsigned long delta = _mock_millis - lastInteraction;
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleTargetFromDelta(delta, false));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_setNotIdle_from_other_TU_wakes_idleState);
    RUN_TEST(test_setNotIdle_from_other_TU_updates_lastInteraction);
    RUN_TEST(test_setNotIdleISR_from_other_TU_wakes_idleState);
    RUN_TEST(test_setNotIdle_bumps_timestamp_when_already_not_idle);
    RUN_TEST(test_idleTarget_not_idle_when_fresh);
    RUN_TEST(test_idleTarget_idle_at_threshold);
    RUN_TEST(test_idleTarget_pseudo_sleep_at_threshold);
    RUN_TEST(test_idleTarget_sleep_at_threshold);
    RUN_TEST(test_idleTarget_charging_caps_at_pseudo_sleep);
    RUN_TEST(test_idleTarget_charging_still_allows_idle);
    RUN_TEST(test_wake_from_pseudo_sleep_via_interaction);
    runSleepTests();
    return UNITY_END();
}
