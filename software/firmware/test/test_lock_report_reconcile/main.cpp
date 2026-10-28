#include "../../src/utils/lockReportReconcile.h"
#include "unity.h"

static const uint64_t kLocalEnd = 1790000000000ULL;

void setUp(void) {}
void tearDown(void) {}

// --- serverMissedLock ----------------------------------------------------

void test_server_still_holding_an_offer_for_a_held_lock_is_missed(void) {
    TEST_ASSERT_TRUE(serverMissedLock(true, "locked", false, kLocalEnd,
                                      "pending", 0));
}

void test_server_locked_without_an_end_is_missed(void) {
    TEST_ASSERT_TRUE(serverMissedLock(true, "locked", false, kLocalEnd,
                                      "locked", 0));
    TEST_ASSERT_TRUE(serverMissedLock(true, "locked", false, kLocalEnd,
                                      "break", 0));
    TEST_ASSERT_TRUE(serverMissedLock(true, "locked", false, kLocalEnd,
                                      "break_ending", 0));
}

void test_server_locked_with_an_end_is_fine(void) {
    TEST_ASSERT_FALSE(serverMissedLock(true, "locked", false, kLocalEnd,
                                       "locked", kLocalEnd));
    TEST_ASSERT_FALSE(serverMissedLock(true, "locked", false, kLocalEnd,
                                       "locked", kLocalEnd + 60000));
}

void test_another_session_is_never_repaired(void) {
    TEST_ASSERT_FALSE(serverMissedLock(false, "locked", false, kLocalEnd,
                                       "pending", 0));
    TEST_ASSERT_FALSE(serverMissedLock(false, "locked", false, kLocalEnd,
                                       "locked", 0));
}

void test_a_box_that_is_not_plainly_locked_stays_quiet(void) {
    // Still an offer locally: nothing to repeat.
    TEST_ASSERT_FALSE(serverMissedLock(true, "pending", false, kLocalEnd,
                                       "pending", 0));
    // On a break: a LOCKED report would end the break on the server.
    TEST_ASSERT_FALSE(serverMissedLock(true, "locked", true, kLocalEnd,
                                       "pending", 0));
    // No deadline of its own to defend.
    TEST_ASSERT_FALSE(serverMissedLock(true, "locked", false, 0, "pending", 0));
}

void test_server_unlock_or_clear_is_not_a_missed_lock(void) {
    TEST_ASSERT_FALSE(serverMissedLock(true, "locked", false, kLocalEnd,
                                       "unlocked", 0));
    TEST_ASSERT_FALSE(serverMissedLock(true, "locked", false, kLocalEnd,
                                       "completed", 0));
    TEST_ASSERT_FALSE(serverMissedLock(true, "locked", false, kLocalEnd,
                                       "abandoned", 0));
}

// --- keepLocalDeadline ---------------------------------------------------

void test_dateless_locked_sync_keeps_the_local_deadline(void) {
    TEST_ASSERT_TRUE(keepLocalDeadline(true, "locked", 0, kLocalEnd));
    TEST_ASSERT_TRUE(keepLocalDeadline(true, "break", 0, kLocalEnd));
    TEST_ASSERT_TRUE(keepLocalDeadline(true, "break_ending", 0, kLocalEnd));
}

void test_a_server_deadline_always_wins(void) {
    TEST_ASSERT_FALSE(keepLocalDeadline(true, "locked", kLocalEnd, kLocalEnd));
    TEST_ASSERT_FALSE(
        keepLocalDeadline(true, "locked", kLocalEnd + 1, kLocalEnd));
    TEST_ASSERT_FALSE(
        keepLocalDeadline(true, "locked", kLocalEnd - 1, kLocalEnd));
}

void test_new_session_without_an_end_is_a_permanent_lock(void) {
    TEST_ASSERT_FALSE(keepLocalDeadline(false, "locked", 0, kLocalEnd));
}

void test_nothing_to_keep_without_a_local_deadline(void) {
    TEST_ASSERT_FALSE(keepLocalDeadline(true, "locked", 0, 0));
}

void test_offers_and_unlocks_never_keep_a_deadline(void) {
    TEST_ASSERT_FALSE(keepLocalDeadline(true, "pending", 0, kLocalEnd));
    TEST_ASSERT_FALSE(keepLocalDeadline(true, "unlocked", 0, kLocalEnd));
    TEST_ASSERT_FALSE(keepLocalDeadline(true, "completed", 0, kLocalEnd));
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_server_still_holding_an_offer_for_a_held_lock_is_missed);
    RUN_TEST(test_server_locked_without_an_end_is_missed);
    RUN_TEST(test_server_locked_with_an_end_is_fine);
    RUN_TEST(test_another_session_is_never_repaired);
    RUN_TEST(test_a_box_that_is_not_plainly_locked_stays_quiet);
    RUN_TEST(test_server_unlock_or_clear_is_not_a_missed_lock);
    RUN_TEST(test_dateless_locked_sync_keeps_the_local_deadline);
    RUN_TEST(test_a_server_deadline_always_wins);
    RUN_TEST(test_new_session_without_an_end_is_a_permanent_lock);
    RUN_TEST(test_nothing_to_keep_without_a_local_deadline);
    RUN_TEST(test_offers_and_unlocks_never_keep_a_deadline);
    return UNITY_END();
}

int main(void) { return runUnityTests(); }
