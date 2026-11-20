#include <unity.h>

#include "../../src/utils/stateRead.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kRetry = stateRead::kRetryMs;
constexpr uint32_t kSlow = stateRead::kSlowRetryMs;

// One read from start to end, as the delay task runs it.
void runRead(StateRead& read, uint32_t startMs, uint32_t endMs, bool completed) {
    TEST_ASSERT_TRUE(read.due(startMs));
    read.begin(startMs);
    read.sent();
    TEST_ASSERT_TRUE(read.inFlight());
    TEST_ASSERT_FALSE(read.due(endMs));
    read.finished(completed, endMs);
    TEST_ASSERT_FALSE(read.inFlight());
}
}  // namespace

void test_nothing_is_read_until_a_read_is_wanted() {
    StateRead read;
    TEST_ASSERT_FALSE(read.pending());
    TEST_ASSERT_FALSE(read.due(0));
    TEST_ASSERT_FALSE(read.due(600000));
}

void test_a_wanted_read_starts_at_once_and_runs_once() {
    StateRead read;
    read.want();
    TEST_ASSERT_TRUE(read.pending());
    runRead(read, 1000, 1800, true);
    TEST_ASSERT_FALSE(read.pending());
    TEST_ASSERT_FALSE(read.due(1800));
    TEST_ASSERT_FALSE(read.due(1800 + kSlow));
}

// Several things can ask for the read before it starts (a reconnect and a
// clear, say): one read answers them all.
void test_one_read_answers_everything_wanted_before_it_started() {
    StateRead read;
    read.want();
    read.want();
    read.want();
    runRead(read, 0, 500, true);
    TEST_ASSERT_FALSE(read.pending());
}

// The answer in flight was built before the newer request was made, so it
// may not know what that request is about.
void test_a_read_wanted_while_one_runs_gets_its_own() {
    StateRead read;
    read.want();
    read.begin(0);
    read.sent();
    read.want();
    read.finished(true, 400);
    TEST_ASSERT_TRUE(read.pending());
    // The one before it completed, so this one need not wait.
    runRead(read, 400, 900, true);
    TEST_ASSERT_FALSE(read.pending());
}

// The fault this replaces: the read was tried 6 times over 30 s and then
// given up. It is now tried until one read completes.
void test_a_failed_read_is_tried_again_and_never_given_up() {
    StateRead read;
    read.want();
    uint32_t now = 0;
    runRead(read, now, now + 100, false);
    now += 100;
    // Quick tries first.
    for (uint8_t attempt = 2; attempt <= stateRead::kQuickAttempts + 1;
         ++attempt) {
        TEST_ASSERT_FALSE(read.due(now + kRetry - 1));
        now += kRetry;
        runRead(read, now, now + 100, false);
        now += 100;
    }
    // Then slower ones, for as long as it takes.
    for (int attempt = 0; attempt < 200; ++attempt) {
        TEST_ASSERT_TRUE(read.pending());
        TEST_ASSERT_FALSE(read.due(now + kSlow - 1));
        now += kSlow;
        runRead(read, now, now + 100, false);
        now += 100;
    }
    // The dashboard is back.
    now += kSlow;
    runRead(read, now, now + 100, true);
    TEST_ASSERT_FALSE(read.pending());
    TEST_ASSERT_EQUAL_UINT8(0, read.failures());
}

// A request that could not even be handed to the HTTP worker (its queue is
// full, no memory, no Wi-Fi) counts as a read that failed.
void test_a_read_that_could_not_be_sent_is_tried_again() {
    StateRead read;
    read.want();
    TEST_ASSERT_TRUE(read.due(0));
    read.begin(0);
    read.finished(false, 0);
    TEST_ASSERT_TRUE(read.pending());
    TEST_ASSERT_FALSE(read.due(kRetry - 1));
    TEST_ASSERT_TRUE(read.due(kRetry));
}

// After many failures the box asks every 30 s. A reconnect asks for the read
// again, and that must not wait out the slow interval.
void test_news_does_not_wait_out_the_slow_interval() {
    StateRead read;
    read.want();
    uint32_t now = 0;
    for (int attempt = 0; attempt < stateRead::kQuickAttempts + 3; ++attempt) {
        read.begin(now);
        read.sent();
        read.finished(false, now);
        now += kSlow;
    }
    const uint32_t lastTry = now - kSlow;
    TEST_ASSERT_FALSE(read.due(lastTry + kRetry));
    read.want();
    // Still not at once: the dashboard has just failed to answer.
    TEST_ASSERT_FALSE(read.due(lastTry + kRetry - 1));
    TEST_ASSERT_TRUE(read.due(lastTry + kRetry));
}

void test_waits_survive_the_millisecond_counter_wrapping() {
    StateRead read;
    read.want();
    const uint32_t failedAt = UINT32_MAX - 1000;
    read.begin(failedAt);
    read.sent();
    read.finished(false, failedAt);
    TEST_ASSERT_FALSE(read.due(failedAt + kRetry - 1));
    TEST_ASSERT_TRUE(read.due(failedAt + kRetry));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_read_until_a_read_is_wanted);
    RUN_TEST(test_a_wanted_read_starts_at_once_and_runs_once);
    RUN_TEST(test_one_read_answers_everything_wanted_before_it_started);
    RUN_TEST(test_a_read_wanted_while_one_runs_gets_its_own);
    RUN_TEST(test_a_failed_read_is_tried_again_and_never_given_up);
    RUN_TEST(test_a_read_that_could_not_be_sent_is_tried_again);
    RUN_TEST(test_news_does_not_wait_out_the_slow_interval);
    RUN_TEST(test_waits_survive_the_millisecond_counter_wrapping);
    return UNITY_END();
}
