#include "../../src/utils/emergencyBlock.h"
#include "unity.h"

static const uint64_t kBlockEnd = 1790000000000ULL;

void setUp(void) {}
void tearDown(void) {}

void test_block_holds_until_the_stored_end(void) {
    TEST_ASSERT_FALSE(emergencyBlockExpired(kBlockEnd - 1, kBlockEnd));
    TEST_ASSERT_FALSE(emergencyBlockExpired(kBlockEnd - 86400000ULL, kBlockEnd));
}

void test_block_ends_at_or_after_the_stored_end(void) {
    TEST_ASSERT_TRUE(emergencyBlockExpired(kBlockEnd, kBlockEnd));
    TEST_ASSERT_TRUE(emergencyBlockExpired(kBlockEnd + 1, kBlockEnd));
}

void test_unsynced_clock_never_ends_a_real_block(void) {
    TEST_ASSERT_FALSE(emergencyBlockExpired(0, kBlockEnd));
    TEST_ASSERT_FALSE(emergencyBlockExpired(1000ULL * 3600, kBlockEnd));
}

void test_block_without_a_stored_end_is_not_held(void) {
    TEST_ASSERT_TRUE(emergencyBlockExpired(0, 0));
    TEST_ASSERT_TRUE(emergencyBlockExpired(kBlockEnd, 0));
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_block_holds_until_the_stored_end);
    RUN_TEST(test_block_ends_at_or_after_the_stored_end);
    RUN_TEST(test_unsynced_clock_never_ends_a_real_block);
    RUN_TEST(test_block_without_a_stored_end_is_not_held);
    return UNITY_END();
}

int main(void) { return runUnityTests(); }
