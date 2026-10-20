#include "unity.h"

#include "../../src/utils/breakTiming.h"

void setUp(void) {}
void tearDown(void) {}

// --- breakCooldownRemaining -------------------------------------------------

void test_first_break_waits_one_regularity_period(void) {
    const uint64_t start = 1700000000000;
    const uint64_t regularity = 2 * 60 * 60 * 1000;
    TEST_ASSERT_EQUAL_UINT64(regularity,
        breakCooldownRemaining(0, start, regularity, start));
    TEST_ASSERT_EQUAL_UINT64(1,
        breakCooldownRemaining(0, start, regularity, start + regularity - 1));
    TEST_ASSERT_EQUAL_UINT64(0,
        breakCooldownRemaining(0, start, regularity, start + regularity));
    TEST_ASSERT_EQUAL_UINT64(0,
        breakCooldownRemaining(0, start, regularity, start + regularity + 1));
}

void test_next_break_waits_from_actual_break_end(void) {
    const uint64_t start = 1700000000000;
    const uint64_t regularity = 2 * 60 * 60 * 1000;
    // An early end, the configured 15-minute end, and a late end all start
    // a fresh two-hour wait from the actual end timestamp.
    const uint64_t durations[] = {5 * 60 * 1000, 15 * 60 * 1000,
                                   20 * 60 * 1000};
    for (uint64_t duration : durations) {
        const uint64_t endedAt = start + regularity + duration;
        TEST_ASSERT_EQUAL_UINT64(regularity,
            breakCooldownRemaining(endedAt, start, regularity, endedAt));
        TEST_ASSERT_EQUAL_UINT64(1, breakCooldownRemaining(
            endedAt, start, regularity, endedAt + regularity - 1));
        TEST_ASSERT_EQUAL_UINT64(0, breakCooldownRemaining(
            endedAt, start, regularity, endedAt + regularity));
    }
}

void test_zero_regularity_allows_first_break_immediately(void) {
    TEST_ASSERT_EQUAL_UINT64(0,
        breakCooldownRemaining(0, 1700000000000, 0, 1700000000000));
}

// --- shouldResumeBreak ------------------------------------------------------

void test_resume_when_active_with_start(void) {
    TEST_ASSERT_TRUE(shouldResumeBreak(true, 1000));
}

void test_no_resume_when_not_active(void) {
    TEST_ASSERT_FALSE(shouldResumeBreak(false, 1000));
}

void test_no_resume_when_no_start(void) {
    TEST_ASSERT_FALSE(shouldResumeBreak(true, 0));
}

// Regression for Bug #1: a slept-through (already-expired) break must still
// resume from its original start instead of restarting at full time. The helper
// has no notion of expiry on purpose, so an active break with a start always
// resumes.
void test_resume_even_when_window_expired(void) {
    TEST_ASSERT_TRUE(shouldResumeBreak(true, 1000));
}

// --- breakTimeRemaining -----------------------------------------------------

void test_remaining_full_at_start(void) {
    TEST_ASSERT_EQUAL_INT64(900000,
                            breakTimeRemaining(1000, 900000, 1000));
}

void test_remaining_partway(void) {
    // 100s into a 900s break -> 800s left.
    TEST_ASSERT_EQUAL_INT64(800000,
                            breakTimeRemaining(1000, 900000, 101000));
}

// Bug #1: when the break window has been exceeded, remaining goes negative so
// the red "YOU ARE LATE" screen renders.
void test_remaining_negative_when_late(void) {
    // 1s past the end of the window (ends at 901000).
    TEST_ASSERT_EQUAL_INT64(-1000,
                            breakTimeRemaining(1000, 900000, 902000));
}

// --- isLockWindowOver -------------------------------------------------------

void test_lock_window_over_when_now_past_end(void) {
    TEST_ASSERT_TRUE(isLockWindowOver(1000, 2000));
}

void test_lock_window_not_over_when_now_before_end(void) {
    TEST_ASSERT_FALSE(isLockWindowOver(5000, 2000));
}

void test_lock_window_not_over_at_exact_end(void) {
    TEST_ASSERT_FALSE(isLockWindowOver(2000, 2000));
}

// lockEnd == 0 means "unknown" and must never be treated as over (avoids a
// spurious unlock when the field hasn't been synced yet).
void test_lock_window_zero_end_is_not_over(void) {
    TEST_ASSERT_FALSE(isLockWindowOver(0, 2000));
}

// --- isWithinBreakCooldown --------------------------------------------------
// A server-pushed break landing inside the cooldown of a just-ended break must
// be suppressed (the stale-"break" echo); outside it, allowed.

void test_cooldown_active_just_after_break(void) {
    // breakLast=1000, cooldown=60000 -> suppressed until 61000; now=2000 inside.
    TEST_ASSERT_TRUE(isWithinBreakCooldown(1000, 60000, 2000));
}

void test_cooldown_elapsed_after_window(void) {
    TEST_ASSERT_FALSE(isWithinBreakCooldown(1000, 60000, 61001));
}

void test_cooldown_at_exact_boundary_is_elapsed(void) {
    // now == breakLast + cooldown -> not within (cooldown has just elapsed).
    TEST_ASSERT_FALSE(isWithinBreakCooldown(1000, 60000, 61000));
}

// breakLast == 0 means no break recorded -> never suppress (don't block the
// first-ever break).
void test_cooldown_never_suppresses_when_no_break(void) {
    TEST_ASSERT_FALSE(isWithinBreakCooldown(0, 60000, 2000));
}

// --- isServerBreakEcho ------------------------------------------------------
// Only the fixed floor separates a keyholder-pushed break from the stale echo
// of the break the device just ended. The wearer's interval never widens it.

void test_server_break_inside_floor_is_echo(void) {
    TEST_ASSERT_TRUE(
        isServerBreakEcho(1000, 1000 + kServerBreakCooldownFloorMs - 1));
}

void test_server_break_at_floor_is_real(void) {
    TEST_ASSERT_FALSE(
        isServerBreakEcho(1000, 1000 + kServerBreakCooldownFloorMs));
}

void test_keyholder_break_inside_wearer_interval_is_real(void) {
    // 2026-09-27: the wearer's last break ended at 15:45 under an 18 h
    // interval and the keyholder opened the lock at 18:58. The device must
    // open; the interval only governs the wearer's own button.
    const uint64_t endedAt = 1790000000000ULL;
    const uint64_t threeHours = 3ULL * 60 * 60 * 1000;
    TEST_ASSERT_FALSE(isServerBreakEcho(endedAt, endedAt + threeHours));
}

void test_server_break_never_echo_without_previous_break(void) {
    TEST_ASSERT_FALSE(isServerBreakEcho(0, 2000));
}

// --- evaluateBreakPreflight -------------------------------------------------

void test_preflight_allows_after_interval(void) {
    const uint64_t endedAt = 1700000000000ULL;
    const uint64_t regularity = 2ULL * 60 * 60 * 1000;
    TEST_ASSERT_EQUAL(BreakPreflight::ALLOWED,
        evaluateBreakPreflight(true, false, endedAt, 0, regularity,
                               endedAt + regularity));
}

void test_preflight_too_soon_inside_interval(void) {
    const uint64_t endedAt = 1700000000000ULL;
    const uint64_t regularity = 2ULL * 60 * 60 * 1000;
    TEST_ASSERT_EQUAL(BreakPreflight::TOO_SOON,
        evaluateBreakPreflight(true, false, endedAt, 0, regularity,
                               endedAt + regularity - 1));
}

void test_preflight_refuses_wearer_when_keyholder_only(void) {
    // Even with the interval long elapsed (or set to one minute), the wearer
    // cannot start a break on a keyholder-only lock.
    const uint64_t endedAt = 1700000000000ULL;
    TEST_ASSERT_EQUAL(BreakPreflight::KEYHOLDER_ONLY,
        evaluateBreakPreflight(true, true, endedAt, 0, 60000,
                               endedAt + 24ULL * 60 * 60 * 1000));
}

void test_preflight_not_enabled_wins_over_keyholder_only(void) {
    TEST_ASSERT_EQUAL(BreakPreflight::NOT_ENABLED,
        evaluateBreakPreflight(false, true, 0, 0, 0, 1000));
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_break_waits_one_regularity_period);
    RUN_TEST(test_next_break_waits_from_actual_break_end);
    RUN_TEST(test_zero_regularity_allows_first_break_immediately);
    RUN_TEST(test_resume_when_active_with_start);
    RUN_TEST(test_no_resume_when_not_active);
    RUN_TEST(test_no_resume_when_no_start);
    RUN_TEST(test_resume_even_when_window_expired);
    RUN_TEST(test_remaining_full_at_start);
    RUN_TEST(test_remaining_partway);
    RUN_TEST(test_remaining_negative_when_late);
    RUN_TEST(test_lock_window_over_when_now_past_end);
    RUN_TEST(test_lock_window_not_over_when_now_before_end);
    RUN_TEST(test_lock_window_not_over_at_exact_end);
    RUN_TEST(test_lock_window_zero_end_is_not_over);
    RUN_TEST(test_cooldown_active_just_after_break);
    RUN_TEST(test_cooldown_elapsed_after_window);
    RUN_TEST(test_cooldown_at_exact_boundary_is_elapsed);
    RUN_TEST(test_cooldown_never_suppresses_when_no_break);
    RUN_TEST(test_server_break_inside_floor_is_echo);
    RUN_TEST(test_server_break_at_floor_is_real);
    RUN_TEST(test_keyholder_break_inside_wearer_interval_is_real);
    RUN_TEST(test_server_break_never_echo_without_previous_break);
    RUN_TEST(test_preflight_allows_after_interval);
    RUN_TEST(test_preflight_too_soon_inside_interval);
    RUN_TEST(test_preflight_refuses_wearer_when_keyholder_only);
    RUN_TEST(test_preflight_not_enabled_wins_over_keyholder_only);
    return UNITY_END();
}

int main(void) { return runUnityTests(); }
