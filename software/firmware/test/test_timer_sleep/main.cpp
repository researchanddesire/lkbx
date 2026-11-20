#include "unity.h"
#include "../../src/utils/timerSleep.h"

void setUp() {}
void tearDown() {}

void test_only_inactive_timer_sleeps_indefinitely() {
    TEST_ASSERT_EQUAL_UINT64(0, timerSleepDelayMs(false, 0, 1000, 120000));
    TEST_ASSERT_EQUAL_UINT64(1, timerSleepDelayMs(true, 0, 1000, 120000));
}

void test_near_and_expired_deadlines_stay_awake() {
    TEST_ASSERT_EQUAL_UINT64(1, timerSleepDelayMs(true, 999, 1000, 120000));
    TEST_ASSERT_EQUAL_UINT64(1, timerSleepDelayMs(true, 1000, 1000, 120000));
    TEST_ASSERT_EQUAL_UINT64(1, timerSleepDelayMs(true, 121000, 1000, 120000));
}

void test_long_timer_wakes_before_deadline_without_32_bit_overflow() {
    const uint64_t now = 1789180000000ULL;
    TEST_ASSERT_EQUAL_UINT64(60000, timerSleepDelayMs(true, now + 180000, now, 120000));
    TEST_ASSERT_EQUAL_UINT64(2591880000ULL,
        timerSleepDelayMs(true, now + 2592000000ULL, now, 120000));
}

// A hidden timer must not light the box up before its deadline: it sleeps
// until the deadline itself, and the Sleep item never keeps it awake early.
void test_hidden_timer_wakes_at_the_deadline_not_before() {
    const uint64_t now = 1789180000000ULL;
    TEST_ASSERT_EQUAL_UINT64(120000, timerWakeMarginMs(true, 120000));
    TEST_ASSERT_EQUAL_UINT64(0, timerWakeMarginMs(false, 120000));

    const uint64_t hidden = timerWakeMarginMs(false, 120000);
    // Three minutes, ninety seconds and five seconds before the end all sleep
    // until the end: nothing distinguishes the last minutes.
    TEST_ASSERT_EQUAL_UINT64(180000, timerSleepDelayMs(true, now + 180000, now, hidden));
    TEST_ASSERT_EQUAL_UINT64(90000, timerSleepDelayMs(true, now + 90000, now, hidden));
    TEST_ASSERT_EQUAL_UINT64(5000, timerSleepDelayMs(true, now + 5000, now, hidden));
    // The same moments with a visible countdown stay awake inside the margin.
    TEST_ASSERT_EQUAL_UINT64(1, timerSleepDelayMs(true, now + 90000, now, 120000));
    // Once the deadline has passed the box stays awake either way.
    TEST_ASSERT_EQUAL_UINT64(1, timerSleepDelayMs(true, now, now, hidden));
    TEST_ASSERT_EQUAL_UINT64(1, timerSleepDelayMs(true, now - 1000, now, hidden));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_only_inactive_timer_sleeps_indefinitely);
    RUN_TEST(test_near_and_expired_deadlines_stay_awake);
    RUN_TEST(test_long_timer_wakes_before_deadline_without_32_bit_overflow);
    RUN_TEST(test_hidden_timer_wakes_at_the_deadline_not_before);
    return UNITY_END();
}
