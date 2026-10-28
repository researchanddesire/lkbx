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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_only_inactive_timer_sleeps_indefinitely);
    RUN_TEST(test_near_and_expired_deadlines_stay_awake);
    RUN_TEST(test_long_timer_wakes_before_deadline_without_32_bit_overflow);
    return UNITY_END();
}
