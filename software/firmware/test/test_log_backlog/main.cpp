#include <unity.h>

#include "../../src/utils/logBacklog.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr int kCap = logBacklog::kOfflineBytes;
}  // namespace

// Connected, the client sends and expires what it holds: logs are always
// queued, whatever is waiting.
void test_a_connected_box_queues_every_log() {
    TEST_ASSERT_TRUE(activityLogIsQueued(true, 0));
    TEST_ASSERT_TRUE(activityLogIsQueued(true, kCap));
    TEST_ASSERT_TRUE(activityLogIsQueued(true, 100 * kCap));
}

// A short drop: the logs of the last moments are still delivered when the
// client connects again.
void test_a_box_off_the_broker_keeps_its_last_few_logs() {
    TEST_ASSERT_TRUE(activityLogIsQueued(false, 0));
    TEST_ASSERT_TRUE(activityLogIsQueued(false, 600));
    TEST_ASSERT_TRUE(activityLogIsQueued(false, kCap - 1));
}

// The fault: nothing leaves the outbox while the box is off the broker, so
// every log written offline or on the Wi-Fi setup page stayed in internal
// RAM.
void test_a_box_off_the_broker_stops_adding_logs_when_the_outbox_is_full() {
    TEST_ASSERT_FALSE(activityLogIsQueued(false, kCap));
    TEST_ASSERT_FALSE(activityLogIsQueued(false, kCap + 1));
    TEST_ASSERT_FALSE(activityLogIsQueued(false, 100 * kCap));
}

// Logs of 600 bytes, written one after another while offline: the outbox
// stops growing within one log of its limit.
void test_the_outbox_stops_growing_offline() {
    int outboxBytes = 0;
    int queued = 0;
    for (int log = 0; log < 1000; ++log) {
        if (!activityLogIsQueued(false, outboxBytes)) continue;
        outboxBytes += 600;
        ++queued;
    }
    TEST_ASSERT_TRUE(outboxBytes < kCap + 600);
    TEST_ASSERT_TRUE(queued >= kCap / 600);
    TEST_ASSERT_TRUE(queued < 1000);
    // Connected again, the client empties its outbox and logs flow.
    TEST_ASSERT_TRUE(activityLogIsQueued(true, outboxBytes));
    TEST_ASSERT_TRUE(activityLogIsQueued(false, 0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_connected_box_queues_every_log);
    RUN_TEST(test_a_box_off_the_broker_keeps_its_last_few_logs);
    RUN_TEST(test_a_box_off_the_broker_stops_adding_logs_when_the_outbox_is_full);
    RUN_TEST(test_the_outbox_stops_growing_offline);
    return UNITY_END();
}
