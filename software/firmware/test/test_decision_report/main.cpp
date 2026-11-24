#include "../../src/utils/decisionReport.h"
#include "unity.h"

using decisionReport::kFirstWaitMs;
using decisionReport::kLongestWaitMs;
using decisionReport::Tracker;
using decisionReport::waitAfter;

// The report a box repeats after it opened at the end of its timer. The
// dashboard knows it by its id.
static const std::string kUnlocked =
    "{\"id\":\"a1b2c3\",\"state\":\"unlocked\",\"sessionId\":\"session-1\"}";
// What building the report again would give: a new id and a new time.
static const std::string kRebuilt =
    "{\"id\":\"ffffff\",\"state\":\"unlocked\",\"sessionId\":\"session-1\"}";
static const char kSession[] = "session-1";
static const char kOtherSession[] = "session-2";

void setUp(void) {}
void tearDown(void) {}

// One attempt as the firmware makes it: the stored report, or a freshly built
// one the first time. Returns what was sent.
static std::string attempt(Tracker &t, uint32_t nowMs,
                           const std::string &fresh, int msgId = 7) {
    std::string payload = t.payload();
    if (payload.empty()) payload = fresh;
    t.onAttempt(msgId, nowMs, payload);
    return payload;
}

// The box takes the decision at `nowMs` and its page reports it once. The
// first repeat goes out one interval later; returns that moment.
static uint32_t decideAndRepeatOnce(Tracker &t, uint32_t nowMs) {
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", nowMs));
    const uint32_t first = nowMs + kFirstWaitMs;
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", first));
    TEST_ASSERT_EQUAL_STRING(kUnlocked.c_str(),
                             attempt(t, first, kUnlocked).c_str());
    return first;
}

// --- the same report, every time -------------------------------------------

void test_a_new_decision_waits_before_its_first_repeat(void) {
    // The page that made the decision has just reported it. A second report
    // in the same moment would be handled alongside the first.
    Tracker t;
    TEST_ASSERT_FALSE(t.isTracking());
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 1000));
    TEST_ASSERT_TRUE(t.isTracking());
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 1000 + kFirstWaitMs - 1));
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", 1000 + kFirstWaitMs));
    // Nothing was built yet: the caller builds the report.
    TEST_ASSERT_TRUE(t.payload().empty());
}

void test_every_repeat_sends_the_bytes_of_the_first(void) {
    Tracker t;
    uint32_t now = decideAndRepeatOnce(t, 1000);

    // A rebuilt report is never used: 16,308 new ids became 16,308 segments
    // on the dashboard.
    for (int repeat = 0; repeat < 20; ++repeat) {
        now += kLongestWaitMs;
        TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now));
        TEST_ASSERT_EQUAL_STRING(kUnlocked.c_str(),
                                 attempt(t, now, kRebuilt).c_str());
    }
}

// --- backing off -------------------------------------------------------------

void test_the_wait_doubles_up_to_five_minutes(void) {
    TEST_ASSERT_EQUAL_UINT32(0, waitAfter(0));
    TEST_ASSERT_EQUAL_UINT32(10000, waitAfter(1));
    TEST_ASSERT_EQUAL_UINT32(20000, waitAfter(2));
    TEST_ASSERT_EQUAL_UINT32(40000, waitAfter(3));
    TEST_ASSERT_EQUAL_UINT32(80000, waitAfter(4));
    TEST_ASSERT_EQUAL_UINT32(160000, waitAfter(5));
    TEST_ASSERT_EQUAL_UINT32(kLongestWaitMs, waitAfter(6));
    TEST_ASSERT_EQUAL_UINT32(kLongestWaitMs, waitAfter(7));
    TEST_ASSERT_EQUAL_UINT32(kLongestWaitMs, waitAfter(255));
}

void test_a_repeat_is_due_only_after_its_wait(void) {
    Tracker t;
    uint32_t now = decideAndRepeatOnce(t, 1000);

    // The wait after the first repeat is twice the first.
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", now + kFirstWaitMs));
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", now + 2 * kFirstWaitMs - 1));
    now += 2 * kFirstWaitMs;
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now));
    attempt(t, now, kRebuilt);

    // And the next one twice that.
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", now + 4 * kFirstWaitMs - 1));
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now + 4 * kFirstWaitMs));
}

void test_an_unanswered_box_sends_far_fewer_reports(void) {
    // Every ten seconds was 360 reports an hour.
    Tracker t;
    int sent = 0;
    for (uint32_t now = 0; now < 60 * 60 * 1000; now += 50) {
        if (!t.due(kSession, "unlocked", now)) continue;
        attempt(t, now, kUnlocked);
        ++sent;
    }
    // At 10, 30, 70, 150 and 310 s, then every five minutes.
    TEST_ASSERT_EQUAL_INT(15, sent);
    TEST_ASSERT_TRUE(t.isTracking());
}

void test_it_never_gives_up(void) {
    Tracker t;
    uint32_t now = decideAndRepeatOnce(t, 0);
    for (int n = 0; n < 1000; ++n) {
        now += kLongestWaitMs;
        TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now));
        attempt(t, now, kRebuilt);
    }
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, t.attemptCount());
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", now + kLongestWaitMs - 1));
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now + kLongestWaitMs));
}

void test_the_wait_survives_millis_wraparound(void) {
    Tracker t;
    const uint32_t beforeWrap = UINT32_MAX - 4000;
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", beforeWrap));
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 1000));  // 5 s later
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", 6000));   // 10 s later
}

// --- attempts that never left -----------------------------------------------

void test_a_send_that_failed_keeps_the_report_and_its_wait(void) {
    Tracker t;
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 1000));
    uint32_t now = 1000 + kFirstWaitMs;
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now));
    // No memory to seal it: nothing left the box.
    attempt(t, now, kUnlocked, -1);
    TEST_ASSERT_EQUAL_STRING(kUnlocked.c_str(), t.payload().c_str());
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", now + kFirstWaitMs - 1));

    // Still the first wait after any number of failed sends, and still the
    // report that was built first.
    for (int n = 0; n < 10; ++n) {
        now += kFirstWaitMs;
        TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now));
        TEST_ASSERT_EQUAL_STRING(kUnlocked.c_str(),
                                 attempt(t, now, kRebuilt, -1).c_str());
    }
    TEST_ASSERT_EQUAL_UINT8(1, t.attemptCount());
}

// --- a new connection ---------------------------------------------------------

void test_a_new_connection_sends_at_once_with_the_same_report(void) {
    Tracker t;
    uint32_t now = decideAndRepeatOnce(t, 0);
    for (int n = 0; n < 8; ++n) {
        now += kLongestWaitMs;
        TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now));
        attempt(t, now, kRebuilt);
    }
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", now + 1000));

    t.onConnected();
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now + 1000));
    TEST_ASSERT_EQUAL_STRING(kUnlocked.c_str(),
                             attempt(t, now + 1000, kRebuilt).c_str());
    // And the wait starts short again.
    TEST_ASSERT_FALSE(
        t.due(kSession, "unlocked", now + 1000 + kFirstWaitMs - 1));
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", now + 1000 + kFirstWaitMs));
}

void test_a_decision_held_from_before_goes_out_when_the_box_connects(void) {
    // After a restart the box still holds its decision. It is noticed while
    // the box is offline and reported the moment it connects.
    Tracker t;
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 1000));
    t.onConnected();
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", 1500));
    TEST_ASSERT_EQUAL_STRING(kUnlocked.c_str(),
                             attempt(t, 1500, kUnlocked).c_str());
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 1500 + kFirstWaitMs - 1));
    TEST_ASSERT_TRUE(t.due(kSession, "unlocked", 1500 + kFirstWaitMs));
}

void test_a_new_connection_with_nothing_to_report_changes_nothing(void) {
    Tracker t;
    t.onConnected();
    TEST_ASSERT_FALSE(t.isTracking());
    TEST_ASSERT_TRUE(t.payload().empty());
    // A decision taken afterwards still waits before its first repeat.
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 1000));
    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 1000 + kFirstWaitMs - 1));
}

// --- the acknowledgement ------------------------------------------------------

void test_it_stops_once_the_box_no_longer_holds_the_decision(void) {
    Tracker t;
    const uint32_t now = decideAndRepeatOnce(t, 1000);

    // The dashboard cleared the session, or sent another one.
    t.clear();
    TEST_ASSERT_FALSE(t.isTracking());
    TEST_ASSERT_TRUE(t.payload().empty());
    TEST_ASSERT_EQUAL_UINT8(0, t.attemptCount());

    // An attempt that was already under way changes nothing.
    t.onAttempt(7, now + 1000, kUnlocked);
    TEST_ASSERT_FALSE(t.isTracking());
    TEST_ASSERT_TRUE(t.payload().empty());
}

void test_no_session_is_nothing_to_report(void) {
    Tracker t;
    TEST_ASSERT_FALSE(t.due("", "unlocked", 1000));
    TEST_ASSERT_FALSE(t.due(nullptr, "unlocked", 1000));
    TEST_ASSERT_FALSE(t.due(kSession, "", 1000));
    TEST_ASSERT_FALSE(t.due(kSession, nullptr, 1000));
    TEST_ASSERT_FALSE(t.isTracking());

    // And it drops a report that was being repeated.
    const uint32_t now = decideAndRepeatOnce(t, 1000);
    TEST_ASSERT_FALSE(t.due("", "unlocked", now + kLongestWaitMs));
    TEST_ASSERT_FALSE(t.isTracking());
}

void test_another_session_is_a_new_report(void) {
    Tracker t;
    const uint32_t now = decideAndRepeatOnce(t, 1000);

    // Built afresh: the old report names the old session.
    TEST_ASSERT_FALSE(t.due(kOtherSession, "unlocked", now + kLongestWaitMs));
    TEST_ASSERT_TRUE(t.payload().empty());
    TEST_ASSERT_TRUE(t.due(kOtherSession, "unlocked",
                           now + kLongestWaitMs + kFirstWaitMs));
}

void test_another_decision_for_the_session_is_a_new_report(void) {
    Tracker t;
    TEST_ASSERT_FALSE(t.due(kSession, "abandoned", 1000));
    TEST_ASSERT_TRUE(t.due(kSession, "abandoned", 1000 + kFirstWaitMs));
    attempt(t, 1000 + kFirstWaitMs, "{\"id\":\"d1\",\"state\":\"abandoned\"}");

    TEST_ASSERT_FALSE(t.due(kSession, "unlocked", 60000));
    TEST_ASSERT_TRUE(t.payload().empty());
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_new_decision_waits_before_its_first_repeat);
    RUN_TEST(test_every_repeat_sends_the_bytes_of_the_first);
    RUN_TEST(test_the_wait_doubles_up_to_five_minutes);
    RUN_TEST(test_a_repeat_is_due_only_after_its_wait);
    RUN_TEST(test_an_unanswered_box_sends_far_fewer_reports);
    RUN_TEST(test_it_never_gives_up);
    RUN_TEST(test_the_wait_survives_millis_wraparound);
    RUN_TEST(test_a_send_that_failed_keeps_the_report_and_its_wait);
    RUN_TEST(test_a_new_connection_sends_at_once_with_the_same_report);
    RUN_TEST(test_a_decision_held_from_before_goes_out_when_the_box_connects);
    RUN_TEST(test_a_new_connection_with_nothing_to_report_changes_nothing);
    RUN_TEST(test_it_stops_once_the_box_no_longer_holds_the_decision);
    RUN_TEST(test_no_session_is_nothing_to_report);
    RUN_TEST(test_another_session_is_a_new_report);
    RUN_TEST(test_another_decision_for_the_session_is_a_new_report);
    return UNITY_END();
}

int main(void) { return runUnityTests(); }
