#include "../../src/utils/lockedReport.h"
#include "unity.h"

using lockedReport::kMaxAttempts;
using lockedReport::kResendIntervalMs;
using lockedReport::Tracker;

static const std::string kPayload = "{\"id\":\"abc\",\"state\":\"locked\"}";

void setUp(void) {}
void tearDown(void) {}

// --- confirmation --------------------------------------------------------

void test_report_stays_pending_until_acknowledged(void) {
    Tracker t;
    TEST_ASSERT_FALSE(t.isPending());
    t.onReport(42, 1000, kPayload);
    TEST_ASSERT_TRUE(t.isPending());
    TEST_ASSERT_EQUAL_INT(42, t.trackedId());
    TEST_ASSERT_EQUAL_UINT8(1, t.attemptCount());
    TEST_ASSERT_EQUAL_STRING(kPayload.c_str(), t.payload().c_str());
}

void test_matching_ack_confirms_the_report(void) {
    Tracker t;
    t.onReport(42, 1000, kPayload);
    t.onPublished(42);
    TEST_ASSERT_FALSE(t.isPending());
    TEST_ASSERT_EQUAL_UINT8(0, t.attemptCount());
    TEST_ASSERT_TRUE(t.payload().empty());
    TEST_ASSERT_FALSE(t.dueForResend(1000 + kResendIntervalMs));
}

void test_ack_for_another_message_does_not_confirm(void) {
    Tracker t;
    t.onReport(42, 1000, kPayload);
    t.onPublished(7);  // activity log or provenance publish
    t.onPublished(-1);
    TEST_ASSERT_TRUE(t.isPending());
    TEST_ASSERT_TRUE(t.dueForResend(1000 + kResendIntervalMs));
}

void test_ack_while_nothing_pending_is_ignored(void) {
    Tracker t;
    t.onPublished(42);
    TEST_ASSERT_FALSE(t.isPending());
    TEST_ASSERT_EQUAL_UINT8(0, t.attemptCount());
}

void test_ack_of_a_retry_id_confirms(void) {
    Tracker t;
    t.onReport(42, 1000, kPayload);
    t.onRetry(43, 1000 + kResendIntervalMs, kPayload);
    t.onPublished(42);  // the original entry expired from the outbox
    TEST_ASSERT_TRUE(t.isPending());
    t.onPublished(43);
    TEST_ASSERT_FALSE(t.isPending());
}

// --- resend timing -------------------------------------------------------

void test_resend_is_due_only_after_the_interval(void) {
    Tracker t;
    t.onReport(42, 1000, kPayload);
    TEST_ASSERT_FALSE(t.dueForResend(1000));
    TEST_ASSERT_FALSE(t.dueForResend(1000 + kResendIntervalMs - 1));
    TEST_ASSERT_TRUE(t.dueForResend(1000 + kResendIntervalMs));
}

void test_each_retry_restarts_the_interval(void) {
    Tracker t;
    t.onReport(42, 1000, kPayload);
    const uint32_t retryAt = 1000 + kResendIntervalMs;
    t.onRetry(43, retryAt, kPayload);
    TEST_ASSERT_EQUAL_INT(43, t.trackedId());
    TEST_ASSERT_EQUAL_UINT8(2, t.attemptCount());
    TEST_ASSERT_FALSE(t.dueForResend(retryAt + kResendIntervalMs - 1));
    TEST_ASSERT_TRUE(t.dueForResend(retryAt + kResendIntervalMs));
}

void test_retries_stop_after_max_attempts(void) {
    Tracker t;
    uint32_t now = 1000;
    t.onReport(100, now, kPayload);
    for (uint8_t i = 1; i < kMaxAttempts; ++i) {
        now += kResendIntervalMs;
        t.onRetry(100 + i, now, kPayload);
    }
    TEST_ASSERT_EQUAL_UINT8(kMaxAttempts, t.attemptCount());
    TEST_ASSERT_TRUE(t.isPending());
    TEST_ASSERT_FALSE(t.dueForResend(now + kResendIntervalMs));
    TEST_ASSERT_FALSE(t.dueForResend(now + 60 * kResendIntervalMs));
}

void test_interval_survives_millis_wraparound(void) {
    Tracker t;
    const uint32_t justBeforeWrap = UINT32_MAX - 100;
    t.onReport(42, justBeforeWrap, kPayload);
    // 100 ms later millis() has wrapped to a small number.
    TEST_ASSERT_FALSE(t.dueForResend(0));
    TEST_ASSERT_TRUE(t.dueForResend(kResendIntervalMs - 100));
}

// --- the same bytes are resent -------------------------------------------

void test_retry_keeps_the_original_payload(void) {
    Tracker t;
    t.onReport(42, 1000, kPayload);
    t.onRetry(43, 1000 + kResendIntervalMs, "{\"id\":\"other\"}");
    TEST_ASSERT_EQUAL_STRING(kPayload.c_str(), t.payload().c_str());
}

// --- superseded ----------------------------------------------------------

void test_other_state_message_clears_the_report(void) {
    Tracker t;
    t.onReport(42, 1000, kPayload);
    t.clear();  // unlocked / break / abandoned was sent, or a break began
    TEST_ASSERT_FALSE(t.isPending());
    TEST_ASSERT_EQUAL_UINT8(0, t.attemptCount());
    TEST_ASSERT_TRUE(t.payload().empty());
    TEST_ASSERT_FALSE(t.dueForResend(1000 + 10 * kResendIntervalMs));
}

void test_retry_after_clear_is_ignored(void) {
    Tracker t;
    t.onReport(42, 1000, kPayload);
    t.clear();
    t.onRetry(43, 2000, kPayload);
    TEST_ASSERT_FALSE(t.isPending());
    TEST_ASSERT_TRUE(t.payload().empty());
}

// --- skipped sends -------------------------------------------------------

void test_skipped_report_is_pending_but_not_counted(void) {
    Tracker t;
    t.onReport(-1, 1000, {});  // no client or no clock yet
    TEST_ASSERT_TRUE(t.isPending());
    TEST_ASSERT_EQUAL_UINT8(0, t.attemptCount());
    TEST_ASSERT_TRUE(t.payload().empty());
    TEST_ASSERT_TRUE(t.dueForResend(1000 + kResendIntervalMs));
    t.onPublished(-1);  // a negative id never confirms
    TEST_ASSERT_TRUE(t.isPending());
}

void test_skipped_retries_do_not_spend_the_budget(void) {
    Tracker t;
    t.onReport(-1, 1000, {});
    uint32_t now = 1000;
    for (int i = 0; i < 3 * kMaxAttempts; ++i) {
        now += kResendIntervalMs;
        TEST_ASSERT_TRUE(t.dueForResend(now));
        t.onRetry(-1, now, {});  // clock still unsynced
    }
    TEST_ASSERT_EQUAL_UINT8(0, t.attemptCount());
    TEST_ASSERT_TRUE(t.dueForResend(now + kResendIntervalMs));
}

void test_first_successful_retry_stores_the_payload(void) {
    Tracker t;
    t.onReport(-1, 1000, {});
    t.onRetry(44, 1000 + kResendIntervalMs, kPayload);
    TEST_ASSERT_EQUAL_INT(44, t.trackedId());
    TEST_ASSERT_EQUAL_UINT8(1, t.attemptCount());
    TEST_ASSERT_EQUAL_STRING(kPayload.c_str(), t.payload().c_str());
}

// --- fresh budget --------------------------------------------------------

void test_new_report_restarts_an_exhausted_budget(void) {
    Tracker t;
    uint32_t now = 1000;
    t.onReport(100, now, kPayload);
    for (uint8_t i = 1; i < kMaxAttempts; ++i) {
        now += kResendIntervalMs;
        t.onRetry(100 + i, now, kPayload);
    }
    TEST_ASSERT_FALSE(t.dueForResend(now + kResendIntervalMs));
    // The state machine reports LOCKED again (e.g. after a break end).
    t.onReport(200, now + 5000, "{\"id\":\"new\"}");
    TEST_ASSERT_EQUAL_UINT8(1, t.attemptCount());
    TEST_ASSERT_EQUAL_INT(200, t.trackedId());
    TEST_ASSERT_EQUAL_STRING("{\"id\":\"new\"}", t.payload().c_str());
    TEST_ASSERT_TRUE(t.dueForResend(now + 5000 + kResendIntervalMs));
}

void test_reconnect_restores_the_budget_and_keeps_the_report(void) {
    Tracker t;
    uint32_t now = 1000;
    t.onReport(100, now, kPayload);
    for (uint8_t i = 1; i < kMaxAttempts; ++i) {
        now += kResendIntervalMs;
        t.onRetry(100 + i, now, kPayload);
    }
    TEST_ASSERT_FALSE(t.dueForResend(now + kResendIntervalMs));
    t.onConnected();
    TEST_ASSERT_TRUE(t.isPending());
    TEST_ASSERT_EQUAL_UINT8(0, t.attemptCount());
    TEST_ASSERT_EQUAL_STRING(kPayload.c_str(), t.payload().c_str());
    TEST_ASSERT_TRUE(t.dueForResend(now + kResendIntervalMs));
}

void test_reconnect_with_nothing_pending_changes_nothing(void) {
    Tracker t;
    t.onConnected();
    TEST_ASSERT_FALSE(t.isPending());
    TEST_ASSERT_FALSE(t.dueForResend(1000 + kResendIntervalMs));
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_report_stays_pending_until_acknowledged);
    RUN_TEST(test_matching_ack_confirms_the_report);
    RUN_TEST(test_ack_for_another_message_does_not_confirm);
    RUN_TEST(test_ack_while_nothing_pending_is_ignored);
    RUN_TEST(test_ack_of_a_retry_id_confirms);
    RUN_TEST(test_resend_is_due_only_after_the_interval);
    RUN_TEST(test_each_retry_restarts_the_interval);
    RUN_TEST(test_retries_stop_after_max_attempts);
    RUN_TEST(test_interval_survives_millis_wraparound);
    RUN_TEST(test_retry_keeps_the_original_payload);
    RUN_TEST(test_other_state_message_clears_the_report);
    RUN_TEST(test_retry_after_clear_is_ignored);
    RUN_TEST(test_skipped_report_is_pending_but_not_counted);
    RUN_TEST(test_skipped_retries_do_not_spend_the_budget);
    RUN_TEST(test_first_successful_retry_stores_the_payload);
    RUN_TEST(test_new_report_restarts_an_exhausted_budget);
    RUN_TEST(test_reconnect_restores_the_budget_and_keeps_the_report);
    RUN_TEST(test_reconnect_with_nothing_pending_changes_nothing);
    return UNITY_END();
}

int main(void) { return runUnityTests(); }
