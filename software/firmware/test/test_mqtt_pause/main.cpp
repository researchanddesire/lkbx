#include <unity.h>

#include "../../src/utils/mqttPause.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kStart = mqttPause::kStartRetryMs;

// A portal opens on a running client and the caller stops it.
MqttPause pausedForPortal() {
    MqttPause pause;
    pause.portalOpened();
    TEST_ASSERT_TRUE(pause.shouldStop());
    pause.stopped();
    return pause;
}
}  // namespace

void test_nothing_happens_without_a_portal() {
    MqttPause pause;
    TEST_ASSERT_FALSE(pause.shouldStop());
    TEST_ASSERT_FALSE(pause.paused());
    TEST_ASSERT_TRUE(pause.next(0, true) == MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(60000, false) == MqttPauseStep::NONE);
}

void test_the_client_is_stopped_once_and_stays_stopped_while_the_portal_is_open() {
    MqttPause pause = pausedForPortal();
    TEST_ASSERT_TRUE(pause.paused());
    TEST_ASSERT_FALSE(pause.shouldStop());
    // However long the portal stays open, the client is not started.
    TEST_ASSERT_TRUE(pause.next(1000, false) == MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(400000, false) == MqttPauseStep::NONE);
}

void test_the_client_starts_again_a_moment_after_the_portal_closes() {
    MqttPause pause = pausedForPortal();
    pause.portalClosed(10000);
    TEST_ASSERT_TRUE(pause.next(10000, false) == MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(10000 + kStart - 1, false) ==
                     MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(10000 + kStart, false) ==
                     MqttPauseStep::START_CLIENT);
}

// The client's task needs one free block of internal RAM; until it fits the
// box keeps trying instead of staying off the broker.
void test_a_start_that_fails_is_tried_again() {
    MqttPause pause = pausedForPortal();
    pause.portalClosed(0);
    uint32_t now = kStart;
    for (int attempt = 0; attempt < 5; ++attempt) {
        TEST_ASSERT_TRUE(pause.next(now, false) == MqttPauseStep::START_CLIENT);
        pause.startTried(false, now);
        TEST_ASSERT_TRUE(pause.paused());
        TEST_ASSERT_TRUE(pause.next(now + kStart - 1, false) ==
                         MqttPauseStep::NONE);
        now += kStart;
    }
    pause.startTried(true, now);
    TEST_ASSERT_FALSE(pause.paused());
    TEST_ASSERT_TRUE(pause.next(now + kStart, false) == MqttPauseStep::NONE);
}

void test_the_state_is_read_once_after_the_client_connects_again() {
    MqttPause pause = pausedForPortal();
    pause.portalClosed(0);
    pause.startTried(true, kStart);
    // Not before the broker has the box again.
    TEST_ASSERT_TRUE(pause.next(kStart + 100, false) == MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(kStart + 200, true) ==
                     MqttPauseStep::RECONCILE);
    // Asking is enough: the read itself is tried until it completes
    // (utils/stateRead.h), so it is asked for once per start.
    pause.reconcileAsked();
    TEST_ASSERT_TRUE(pause.next(kStart + 300, true) == MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(kStart + 60000, true) == MqttPauseStep::NONE);
}

// Until it has been asked for, the read stays due, also across a connection
// that drops and comes back.
void test_the_state_read_stays_due_until_it_is_asked_for() {
    MqttPause pause = pausedForPortal();
    pause.portalClosed(0);
    pause.startTried(true, kStart);
    TEST_ASSERT_TRUE(pause.next(kStart + 200, true) ==
                     MqttPauseStep::RECONCILE);
    TEST_ASSERT_TRUE(pause.next(kStart + 300, false) == MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(kStart + 90000, true) ==
                     MqttPauseStep::RECONCILE);
}

// At boot the client's task can fail to fit in memory. The box then keeps
// trying, exactly as after a portal, instead of staying off the broker until
// it restarts.
void test_a_client_that_could_not_start_at_boot_is_started_later() {
    MqttPause pause;
    pause.startFailed(5000);
    TEST_ASSERT_TRUE(pause.paused());
    // No portal is involved, so nothing has to be stopped.
    TEST_ASSERT_FALSE(pause.shouldStop());
    TEST_ASSERT_TRUE(pause.next(5000 + kStart - 1, false) ==
                     MqttPauseStep::NONE);
    uint32_t now = 5000 + kStart;
    for (int attempt = 0; attempt < 20; ++attempt) {
        TEST_ASSERT_TRUE(pause.next(now, false) == MqttPauseStep::START_CLIENT);
        pause.startTried(false, now);
        TEST_ASSERT_TRUE(pause.next(now + kStart - 1, false) ==
                         MqttPauseStep::NONE);
        now += kStart;
    }
    pause.startTried(true, now);
    TEST_ASSERT_FALSE(pause.paused());
    // Connected for the first time this boot: read the state once.
    TEST_ASSERT_TRUE(pause.next(now + 500, true) == MqttPauseStep::RECONCILE);
    pause.reconcileAsked();
    TEST_ASSERT_TRUE(pause.next(now + 600, true) == MqttPauseStep::NONE);
}

// A portal opened while the client is still waiting for its first start: the
// client stays stopped under it and starts after it has closed.
void test_a_portal_holds_back_a_client_that_never_started() {
    MqttPause pause;
    pause.startFailed(0);
    pause.portalOpened();
    TEST_ASSERT_FALSE(pause.shouldStop());
    TEST_ASSERT_TRUE(pause.next(kStart, false) == MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(300000, false) == MqttPauseStep::NONE);
    pause.portalClosed(300000);
    TEST_ASSERT_TRUE(pause.next(300000 + kStart, false) ==
                     MqttPauseStep::START_CLIENT);
}

// The portal's task could not be created after the client was stopped for
// it: the caller closes the portal again at once, and the client comes back.
void test_a_portal_that_could_not_open_gives_the_client_back() {
    MqttPause pause = pausedForPortal();
    pause.portalClosed(1000);
    TEST_ASSERT_FALSE(pause.portalOpen());
    TEST_ASSERT_TRUE(pause.next(1000 + kStart, false) ==
                     MqttPauseStep::START_CLIENT);
}

// Should two portals ever be counted at once, the client stays stopped until
// the last one has closed.
void test_overlapping_portals_keep_the_client_stopped() {
    MqttPause pause = pausedForPortal();
    pause.portalOpened();
    TEST_ASSERT_FALSE(pause.shouldStop());
    pause.portalClosed(1000);
    TEST_ASSERT_TRUE(pause.portalOpen());
    TEST_ASSERT_TRUE(pause.next(1000 + kStart, false) == MqttPauseStep::NONE);
    pause.portalClosed(5000);
    TEST_ASSERT_FALSE(pause.portalOpen());
    TEST_ASSERT_TRUE(pause.next(5000 + kStart - 1, false) ==
                     MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(5000 + kStart, false) ==
                     MqttPauseStep::START_CLIENT);
}

void test_a_portal_reopened_before_the_client_started_keeps_it_stopped() {
    MqttPause pause = pausedForPortal();
    pause.portalClosed(0);
    pause.portalOpened();
    TEST_ASSERT_FALSE(pause.shouldStop());
    TEST_ASSERT_TRUE(pause.next(kStart, false) == MqttPauseStep::NONE);
}

void test_a_portal_opened_after_the_client_started_stops_it_again() {
    MqttPause pause = pausedForPortal();
    pause.portalClosed(0);
    pause.startTried(true, kStart);
    pause.portalOpened();
    TEST_ASSERT_TRUE(pause.shouldStop());
    pause.stopped();
    // The state read waits for the next start.
    TEST_ASSERT_TRUE(pause.next(kStart + 100, true) == MqttPauseStep::NONE);
    pause.portalClosed(20000);
    TEST_ASSERT_TRUE(pause.next(20000 + kStart, false) ==
                     MqttPauseStep::START_CLIENT);
    pause.startTried(true, 20000 + kStart);
    TEST_ASSERT_TRUE(pause.next(20000 + kStart + 1, true) ==
                     MqttPauseStep::RECONCILE);
}

// With no client to stop (first setup, or offline) the portal changes nothing.
void test_a_portal_without_a_stopped_client_starts_nothing() {
    MqttPause pause;
    pause.portalOpened();
    TEST_ASSERT_TRUE(pause.shouldStop());
    // The caller had no running client: nothing was stopped.
    pause.portalClosed(1000);
    TEST_ASSERT_FALSE(pause.shouldStop());
    TEST_ASSERT_TRUE(pause.next(1000 + kStart, true) == MqttPauseStep::NONE);
}

void test_a_client_removed_for_an_update_is_not_started_again() {
    MqttPause pause = pausedForPortal();
    pause.portalClosed(0);
    pause.clientGone();
    TEST_ASSERT_FALSE(pause.paused());
    TEST_ASSERT_TRUE(pause.next(kStart, true) == MqttPauseStep::NONE);
}

void test_waits_survive_the_millisecond_counter_wrapping() {
    MqttPause pause = pausedForPortal();
    const uint32_t closedAt = UINT32_MAX - 500;
    pause.portalClosed(closedAt);
    TEST_ASSERT_TRUE(pause.next(closedAt + kStart - 1, false) ==
                     MqttPauseStep::NONE);
    TEST_ASSERT_TRUE(pause.next(closedAt + kStart, false) ==
                     MqttPauseStep::START_CLIENT);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_happens_without_a_portal);
    RUN_TEST(test_the_client_is_stopped_once_and_stays_stopped_while_the_portal_is_open);
    RUN_TEST(test_the_client_starts_again_a_moment_after_the_portal_closes);
    RUN_TEST(test_a_start_that_fails_is_tried_again);
    RUN_TEST(test_the_state_is_read_once_after_the_client_connects_again);
    RUN_TEST(test_the_state_read_stays_due_until_it_is_asked_for);
    RUN_TEST(test_a_client_that_could_not_start_at_boot_is_started_later);
    RUN_TEST(test_a_portal_holds_back_a_client_that_never_started);
    RUN_TEST(test_a_portal_that_could_not_open_gives_the_client_back);
    RUN_TEST(test_overlapping_portals_keep_the_client_stopped);
    RUN_TEST(test_a_portal_reopened_before_the_client_started_keeps_it_stopped);
    RUN_TEST(test_a_portal_opened_after_the_client_started_stops_it_again);
    RUN_TEST(test_a_portal_without_a_stopped_client_starts_nothing);
    RUN_TEST(test_a_client_removed_for_an_update_is_not_started_again);
    RUN_TEST(test_waits_survive_the_millisecond_counter_wrapping);
    return UNITY_END();
}
