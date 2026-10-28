#include "unity.h"
#include "../../src/utils/offeredLock.h"
void setUp() {}
void tearDown() {}

void test_acceptance_starts_full_duration_after_waiting() {
    const uint64_t received = 1789188000000ULL;
    const uint64_t accepted = received + 120000;
    TEST_ASSERT_EQUAL_UINT64(accepted + 600000, acceptedOfferDeadline(true, accepted, 600000));
    TEST_ASSERT_EQUAL_UINT64(0, acceptedOfferDeadline(false, accepted, 600000));
}
void test_invalid_offer_cannot_start_a_timer() {
    TEST_ASSERT_EQUAL_UINT64(0, acceptedOfferDeadline(true, 0, 600000));
    TEST_ASSERT_EQUAL_UINT64(0, acceptedOfferDeadline(true, 1789188000000ULL, 0));
    TEST_ASSERT_EQUAL_UINT64(0, acceptedOfferDeadline(true, UINT64_MAX - 100, 600000));
}
void test_retries_preserve_decisions_but_new_sessions_are_offered() {
    TEST_ASSERT_TRUE(preserveOfferedDecision(true, true, false));
    TEST_ASSERT_TRUE(preserveOfferedDecision(true, false, true));
    TEST_ASSERT_FALSE(preserveOfferedDecision(false, true, false));
    TEST_ASSERT_FALSE(preserveOfferedDecision(false, false, true));
    TEST_ASSERT_FALSE(preserveOfferedDecision(true, false, false));
}
void test_five_minute_timeout_across_millis_rollover() {
    const uint32_t start = UINT32_MAX - 100000;
    TEST_ASSERT_FALSE(offeredLockTimedOut(start, start + 299999));
    TEST_ASSERT_TRUE(offeredLockTimedOut(start, start + 300000));
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_acceptance_starts_full_duration_after_waiting);
    RUN_TEST(test_invalid_offer_cannot_start_a_timer);
    RUN_TEST(test_retries_preserve_decisions_but_new_sessions_are_offered);
    RUN_TEST(test_five_minute_timeout_across_millis_rollover);
    return UNITY_END();
}
