#include <unity.h>

#include <string>

#include "../../src/utils/emergencyRecord.h"
#include "../../src/utils/emergencyReport.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr char kSession[] = "8607f5e1-f052-47e7-9910-1d9180fffa0a";
constexpr char kOther[] = "0d6f3c52-7b1e-4a55-9d0e-6b1f0c2a9e11";
constexpr uint32_t kInterval = emergencyReport::kResendIntervalMs;
const std::string kReport = "{\"id\":\"abc\",\"state\":\"emergency\"}";
}  // namespace

// R2, 2026-11-16: the emergency report never reached the dashboard, the box
// restarted online, was told the session was still locked and drove the
// paddle back to locked under the "Emergency Unlocked" page.
void test_the_session_an_emergency_unlock_ended_is_never_taken_up_again() {
    TEST_ASSERT_TRUE(isEmergencyEndedSession(kSession, kSession));
}

void test_any_other_session_is_not_refused() {
    TEST_ASSERT_FALSE(isEmergencyEndedSession(kSession, kOther));
    TEST_ASSERT_FALSE(isEmergencyEndedSession(kSession, ""));
    TEST_ASSERT_FALSE(isEmergencyEndedSession(kSession, nullptr));
}

// A local session is not the dashboard's, and a box that has had no
// emergency unlock refuses nothing.
void test_without_a_recorded_session_nothing_is_refused() {
    TEST_ASSERT_FALSE(isEmergencyEndedSession("", kSession));
    TEST_ASSERT_FALSE(isEmergencyEndedSession("", ""));
    TEST_ASSERT_FALSE(isEmergencyEndedSession(nullptr, kSession));
}

// The dashboard ended the session while the box could not tell it about the
// emergency unlock. The lockout used to stay for its 24 hours when such a
// box came back online.
void test_a_dashboard_that_ended_the_session_by_itself_lifts_the_lockout() {
    TEST_ASSERT_TRUE(settleEmergencySession(kSession, true) ==
                     EmergencySettle::LIFT_LOCKOUT);
    TEST_ASSERT_TRUE(
        clearEndsLockout(true, settleEmergencySession(kSession, true)));
}

// The dashboard ends the session in answer to every emergency report. That
// must not end the lockout the report announces.
void test_a_session_ended_by_the_report_keeps_the_lockout() {
    TEST_ASSERT_TRUE(settleEmergencySession(kSession, false) ==
                     EmergencySettle::KEEP_LOCKOUT);
    TEST_ASSERT_FALSE(
        clearEndsLockout(true, settleEmergencySession(kSession, false)));
}

// The lockout of a local session, or one already settled, is the box's own.
void test_a_lockout_without_a_dashboard_session_outlasts_a_clear() {
    TEST_ASSERT_TRUE(settleEmergencySession("", true) ==
                     EmergencySettle::NONE);
    TEST_ASSERT_TRUE(settleEmergencySession("", false) ==
                     EmergencySettle::NONE);
    TEST_ASSERT_TRUE(settleEmergencySession(nullptr, true) ==
                     EmergencySettle::NONE);
    TEST_ASSERT_FALSE(clearEndsLockout(true, EmergencySettle::NONE));
}

// Without a lockout a clear unlocks as it always has.
void test_a_clear_without_a_lockout_unlocks() {
    TEST_ASSERT_TRUE(clearEndsLockout(false, EmergencySettle::NONE));
    TEST_ASSERT_TRUE(clearEndsLockout(false, EmergencySettle::KEEP_LOCKOUT));
    TEST_ASSERT_TRUE(clearEndsLockout(false, EmergencySettle::LIFT_LOCKOUT));
}

// R2, 2026-11-16, twice: a box with a local timer restarted with Wi-Fi up,
// asked the dashboard, was told "no session" and unlocked 33 s early.
void test_a_local_lock_is_kept_at_boot_without_asking_the_dashboard() {
    TEST_ASSERT_TRUE(bootKeepsLocalSession(true, true));
}

void test_any_other_boot_asks_the_dashboard() {
    // A dashboard session.
    TEST_ASSERT_FALSE(bootKeepsLocalSession(false, true));
    // Unlocked.
    TEST_ASSERT_FALSE(bootKeepsLocalSession(false, false));
    // A local timer that was set but never locked: nothing is held.
    TEST_ASSERT_FALSE(bootKeepsLocalSession(true, false));
}

void test_nothing_is_sent_while_nothing_is_owed() {
    emergencyReport::Owed owed;
    TEST_ASSERT_FALSE(owed.isOwed());
    TEST_ASSERT_FALSE(owed.due(0));
    TEST_ASSERT_FALSE(owed.due(10 * kInterval));
    TEST_ASSERT_FALSE(owed.takeAcknowledgement());
}

void test_an_owed_report_is_sent_at_once_and_then_every_ten_seconds() {
    emergencyReport::Owed owed;
    owed.owe(kReport);
    TEST_ASSERT_TRUE(owed.isOwed());
    TEST_ASSERT_TRUE(owed.due(1000));
    TEST_ASSERT_EQUAL_STRING(kReport.c_str(), owed.report().c_str());
    owed.onAttempt(7, 1000);
    TEST_ASSERT_EQUAL_UINT8(1, owed.attemptCount());
    TEST_ASSERT_FALSE(owed.due(1000 + kInterval - 1));
    TEST_ASSERT_TRUE(owed.due(1000 + kInterval));
}

void test_the_brokers_acknowledgement_ends_it() {
    emergencyReport::Owed owed;
    owed.owe(kReport);
    owed.onAttempt(7, 1000);
    // Another report's acknowledgement changes nothing.
    owed.onPublished(8);
    TEST_ASSERT_TRUE(owed.isOwed());
    TEST_ASSERT_FALSE(owed.isAcknowledged());
    owed.onPublished(7);
    TEST_ASSERT_FALSE(owed.isOwed());
    TEST_ASSERT_TRUE(owed.isAcknowledged());
    TEST_ASSERT_FALSE(owed.due(1000 + 10 * kInterval));
    // Recorded in flash once.
    TEST_ASSERT_TRUE(owed.takeAcknowledgement());
    TEST_ASSERT_FALSE(owed.takeAcknowledgement());
}

// No memory to seal it, or no room in the client's queue: tried again after
// the interval, without spending the budget.
void test_an_attempt_that_could_not_be_sent_is_not_counted() {
    emergencyReport::Owed owed;
    owed.owe(kReport);
    owed.onAttempt(-1, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, owed.attemptCount());
    TEST_ASSERT_FALSE(owed.due(1000 + kInterval - 1));
    TEST_ASSERT_TRUE(owed.due(1000 + kInterval));
    // An acknowledgement cannot be for a report that was never queued.
    owed.onPublished(-1);
    TEST_ASSERT_TRUE(owed.isOwed());
}

void test_the_attempts_are_bounded_per_connection() {
    emergencyReport::Owed owed;
    owed.owe(kReport);
    uint32_t now = 0;
    for (uint8_t attempt = 0; attempt < emergencyReport::kMaxAttempts;
         ++attempt) {
        TEST_ASSERT_TRUE(owed.due(now));
        owed.onAttempt(100 + attempt, now);
        now += kInterval;
    }
    TEST_ASSERT_FALSE(owed.due(now));
    TEST_ASSERT_FALSE(owed.due(now + 100 * kInterval));
    TEST_ASSERT_TRUE(owed.isOwed());
    // A new connection starts a new round.
    owed.onConnected();
    TEST_ASSERT_TRUE(owed.due(now));
}

// The dashboard still shows the session as active after the broker had the
// report (the report did not get from the broker to the dashboard): owed
// again, and sent at once.
void test_a_report_the_dashboard_has_not_applied_is_owed_again() {
    emergencyReport::Owed owed;
    owed.owe(kReport);
    owed.onAttempt(7, 1000);
    owed.onPublished(7);
    TEST_ASSERT_TRUE(owed.takeAcknowledgement());
    owed.owe(kReport);
    TEST_ASSERT_TRUE(owed.isOwed());
    TEST_ASSERT_FALSE(owed.isAcknowledged());
    TEST_ASSERT_TRUE(owed.due(1001));
    // The acknowledgement of the first copy does not count for this one.
    owed.onPublished(7);
    TEST_ASSERT_TRUE(owed.isOwed());
}

// Owed again while it is already owed: the same report keeps its place, so
// it is not sent twice within the interval, and gets a new round.
void test_owing_the_same_report_again_keeps_its_timing() {
    emergencyReport::Owed owed;
    owed.owe(kReport);
    uint32_t now = 0;
    for (uint8_t attempt = 0; attempt < emergencyReport::kMaxAttempts;
         ++attempt) {
        owed.onAttempt(100 + attempt, now);
        now += kInterval;
    }
    TEST_ASSERT_FALSE(owed.due(now));
    const uint32_t last = now - kInterval;
    owed.owe(kReport);
    TEST_ASSERT_EQUAL_UINT8(0, owed.attemptCount());
    TEST_ASSERT_FALSE(owed.due(last + kInterval - 1));
    TEST_ASSERT_TRUE(owed.due(last + kInterval));
    // The copy that is still on its way can be acknowledged.
    owed.onPublished(100 + emergencyReport::kMaxAttempts - 1);
    TEST_ASSERT_FALSE(owed.isOwed());
}

// The dashboard ended the session by itself: the report is not sent.
void test_a_dropped_report_is_not_sent() {
    emergencyReport::Owed owed;
    owed.owe(kReport);
    owed.onAttempt(7, 1000);
    owed.drop();
    TEST_ASSERT_FALSE(owed.isOwed());
    TEST_ASSERT_FALSE(owed.isAcknowledged());
    TEST_ASSERT_FALSE(owed.due(1000 + 10 * kInterval));
    owed.onPublished(7);
    TEST_ASSERT_FALSE(owed.takeAcknowledgement());
    TEST_ASSERT_TRUE(owed.report().empty());
}

void test_the_interval_survives_the_millisecond_counter_wrapping() {
    emergencyReport::Owed owed;
    owed.owe(kReport);
    const uint32_t sent = UINT32_MAX - 2000;
    owed.onAttempt(7, sent);
    TEST_ASSERT_FALSE(owed.due(sent + kInterval - 1));
    TEST_ASSERT_TRUE(owed.due(sent + kInterval));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_session_an_emergency_unlock_ended_is_never_taken_up_again);
    RUN_TEST(test_any_other_session_is_not_refused);
    RUN_TEST(test_without_a_recorded_session_nothing_is_refused);
    RUN_TEST(test_a_dashboard_that_ended_the_session_by_itself_lifts_the_lockout);
    RUN_TEST(test_a_session_ended_by_the_report_keeps_the_lockout);
    RUN_TEST(test_a_lockout_without_a_dashboard_session_outlasts_a_clear);
    RUN_TEST(test_a_clear_without_a_lockout_unlocks);
    RUN_TEST(test_a_local_lock_is_kept_at_boot_without_asking_the_dashboard);
    RUN_TEST(test_any_other_boot_asks_the_dashboard);
    RUN_TEST(test_nothing_is_sent_while_nothing_is_owed);
    RUN_TEST(test_an_owed_report_is_sent_at_once_and_then_every_ten_seconds);
    RUN_TEST(test_the_brokers_acknowledgement_ends_it);
    RUN_TEST(test_an_attempt_that_could_not_be_sent_is_not_counted);
    RUN_TEST(test_the_attempts_are_bounded_per_connection);
    RUN_TEST(test_a_report_the_dashboard_has_not_applied_is_owed_again);
    RUN_TEST(test_owing_the_same_report_again_keeps_its_timing);
    RUN_TEST(test_a_dropped_report_is_not_sent);
    RUN_TEST(test_the_interval_survives_the_millisecond_counter_wrapping);
    return UNITY_END();
}
