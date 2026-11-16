#include <string>

#include <unity.h>

#include "../../src/utils/unsentReport.h"

using unsentReport::Holder;
using unsentReport::kMaxAttempts;
using unsentReport::kRetryIntervalMs;

namespace {
const std::string kUnlocked = "{\"id\":\"a1\",\"state\":\"unlocked\"}";
const std::string kBreak = "{\"id\":\"b2\",\"state\":\"break\"}";

// A holder with its storage, as the firmware gives it one on first use.
struct Fixture {
    char storage[128] = {};
    Holder holder;
    Fixture() { holder.setStorage(storage, sizeof(storage)); }

    bool hold(const std::string& report, uint32_t nowMs) {
        return holder.hold(report.c_str(), report.length(), nowMs);
    }
};
}  // namespace

void setUp() {}
void tearDown() {}

void test_nothing_is_held_at_first() {
    Fixture f;
    TEST_ASSERT_FALSE(f.holder.isHeld());
    TEST_ASSERT_FALSE(f.holder.due(60000));
    std::string report = "stale";
    uint32_t ticket = 0;
    TEST_ASSERT_FALSE(f.holder.read(report, ticket));
}

// The fault this fixes: the report could not be sealed and was dropped.
void test_a_report_that_could_not_be_sent_is_kept_and_retried() {
    Fixture f;
    TEST_ASSERT_TRUE(f.hold(kUnlocked, 1000));
    TEST_ASSERT_TRUE(f.holder.isHeld());
    // Not at once: the memory that was missing needs a moment to come back.
    TEST_ASSERT_FALSE(f.holder.due(1000 + kRetryIntervalMs - 1));
    TEST_ASSERT_TRUE(f.holder.due(1000 + kRetryIntervalMs));

    std::string report;
    uint32_t ticket = 0;
    TEST_ASSERT_TRUE(f.holder.read(report, ticket));
    TEST_ASSERT_EQUAL_STRING(kUnlocked.c_str(), report.c_str());
    TEST_ASSERT_TRUE(f.holder.stillHeld(ticket));

    f.holder.retried(ticket, true, 1000 + kRetryIntervalMs);
    TEST_ASSERT_FALSE(f.holder.isHeld());
    TEST_ASSERT_FALSE(f.holder.due(1000 + 10 * kRetryIntervalMs));
}

void test_a_retry_that_fails_is_tried_again_after_the_interval() {
    Fixture f;
    f.hold(kUnlocked, 0);
    std::string report;
    uint32_t ticket = 0;
    TEST_ASSERT_TRUE(f.holder.read(report, ticket));
    f.holder.retried(ticket, false, kRetryIntervalMs);
    TEST_ASSERT_TRUE(f.holder.isHeld());
    TEST_ASSERT_EQUAL_UINT8(1, f.holder.attemptCount());
    TEST_ASSERT_FALSE(f.holder.due(2 * kRetryIntervalMs - 1));
    TEST_ASSERT_TRUE(f.holder.due(2 * kRetryIntervalMs));
}

void test_the_retries_are_bounded() {
    Fixture f;
    f.hold(kUnlocked, 0);
    uint32_t now = 0;
    for (uint8_t attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        now += kRetryIntervalMs;
        TEST_ASSERT_TRUE(f.holder.due(now));
        std::string report;
        uint32_t ticket = 0;
        TEST_ASSERT_TRUE(f.holder.read(report, ticket));
        f.holder.retried(ticket, false, now);
    }
    TEST_ASSERT_FALSE(f.holder.isHeld());
    TEST_ASSERT_FALSE(f.holder.due(now + 10 * kRetryIntervalMs));
}

// Only the newest state matters to the dashboard.
void test_the_newest_report_replaces_the_one_held() {
    Fixture f;
    f.hold(kUnlocked, 0);
    f.hold(kBreak, 3000);
    std::string report;
    uint32_t ticket = 0;
    TEST_ASSERT_TRUE(f.holder.read(report, ticket));
    TEST_ASSERT_EQUAL_STRING(kBreak.c_str(), report.c_str());
    // Its wait and its budget start again.
    TEST_ASSERT_FALSE(f.holder.due(3000 + kRetryIntervalMs - 1));
    TEST_ASSERT_TRUE(f.holder.due(3000 + kRetryIntervalMs));
    TEST_ASSERT_EQUAL_UINT8(0, f.holder.attemptCount());
}

// A state report that did go out makes the held one out of date.
void test_a_newer_report_that_was_sent_drops_the_one_held() {
    Fixture f;
    f.hold(kUnlocked, 0);
    f.holder.clear();
    TEST_ASSERT_FALSE(f.holder.isHeld());
    TEST_ASSERT_FALSE(f.holder.due(kRetryIntervalMs));
}

// A retry reads the report, seals it, and only then queues it. A newer
// report made in between must not be followed by the older one, and the
// retry's result must not touch the newer one.
void test_a_retry_overtaken_by_a_newer_report_sends_nothing() {
    Fixture f;
    f.hold(kUnlocked, 0);
    std::string report;
    uint32_t ticket = 0;
    TEST_ASSERT_TRUE(f.holder.read(report, ticket));

    // Overtaken by a report that was sent.
    f.holder.clear();
    TEST_ASSERT_FALSE(f.holder.stillHeld(ticket));

    // Overtaken by a report that is held in its place.
    f.hold(kBreak, 100);
    TEST_ASSERT_FALSE(f.holder.stillHeld(ticket));
    f.holder.retried(ticket, true, 200);
    TEST_ASSERT_TRUE(f.holder.isHeld());
    uint32_t newerTicket = 0;
    TEST_ASSERT_TRUE(f.holder.read(report, newerTicket));
    TEST_ASSERT_EQUAL_STRING(kBreak.c_str(), report.c_str());
    TEST_ASSERT_TRUE(f.holder.stillHeld(newerTicket));
}

// The same report held twice is two reports: the first retry's result is not
// taken for the second.
void test_the_same_text_held_again_is_a_new_report() {
    Fixture f;
    f.hold(kUnlocked, 0);
    std::string report;
    uint32_t ticket = 0;
    TEST_ASSERT_TRUE(f.holder.read(report, ticket));
    f.hold(kUnlocked, 50);
    TEST_ASSERT_FALSE(f.holder.stillHeld(ticket));
}

// Without storage, or for a report that does not fit, nothing can be kept.
// The report held before is out of date either way.
void test_a_report_that_cannot_be_kept_leaves_nothing_held() {
    Holder bare;
    TEST_ASSERT_FALSE(bare.hasStorage());
    TEST_ASSERT_FALSE(bare.hold(kUnlocked.c_str(), kUnlocked.length(), 0));
    TEST_ASSERT_FALSE(bare.isHeld());

    Fixture f;
    f.hold(kUnlocked, 0);
    const std::string tooLong(sizeof(f.storage), 'x');
    TEST_ASSERT_FALSE(f.hold(tooLong, 10));
    TEST_ASSERT_FALSE(f.holder.isHeld());
    // The longest report that fits leaves room for its terminator.
    const std::string longest(sizeof(f.storage) - 1, 'y');
    TEST_ASSERT_TRUE(f.hold(longest, 20));
    std::string report;
    uint32_t ticket = 0;
    TEST_ASSERT_TRUE(f.holder.read(report, ticket));
    TEST_ASSERT_EQUAL_UINT32(sizeof(f.storage) - 1, report.length());
}

void test_storage_is_given_once() {
    Fixture f;
    char other[32] = {};
    TEST_ASSERT_TRUE(f.holder.hasStorage());
    TEST_ASSERT_FALSE(f.holder.setStorage(other, sizeof(other)));
    Holder bare;
    TEST_ASSERT_FALSE(bare.setStorage(nullptr, 16));
    TEST_ASSERT_FALSE(bare.setStorage(other, 0));
}

void test_waits_survive_the_millisecond_counter_wrapping() {
    Fixture f;
    const uint32_t heldAt = UINT32_MAX - 2000;
    f.hold(kUnlocked, heldAt);
    TEST_ASSERT_FALSE(f.holder.due(heldAt + kRetryIntervalMs - 1));
    TEST_ASSERT_TRUE(f.holder.due(heldAt + kRetryIntervalMs));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_held_at_first);
    RUN_TEST(test_a_report_that_could_not_be_sent_is_kept_and_retried);
    RUN_TEST(test_a_retry_that_fails_is_tried_again_after_the_interval);
    RUN_TEST(test_the_retries_are_bounded);
    RUN_TEST(test_the_newest_report_replaces_the_one_held);
    RUN_TEST(test_a_newer_report_that_was_sent_drops_the_one_held);
    RUN_TEST(test_a_retry_overtaken_by_a_newer_report_sends_nothing);
    RUN_TEST(test_the_same_text_held_again_is_a_new_report);
    RUN_TEST(test_a_report_that_cannot_be_kept_leaves_nothing_held);
    RUN_TEST(test_storage_is_given_once);
    RUN_TEST(test_waits_survive_the_millisecond_counter_wrapping);
    return UNITY_END();
}
