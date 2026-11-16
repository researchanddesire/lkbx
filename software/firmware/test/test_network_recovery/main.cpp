#include <unity.h>

#include "../../src/utils/networkRecovery.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kCheck = networkRecovery::kOffBrokerBeforeCheckMs;
constexpr uint32_t kAfter = networkRecovery::kAfterReconnectMs;
constexpr uint32_t kRecheck = networkRecovery::kBrokerOnlyRecheckMs;
constexpr uint32_t kRest = networkRecovery::kAtRestMs;
constexpr uint32_t kFresh = networkRecovery::kNoAnswerFreshMs;

// On the broker.
RecoveryInputs online() {
    RecoveryInputs in;
    in.stationUp = true;
    in.brokerExpected = true;
    in.brokerUp = true;
    return in;
}

// The fault: the station is up and the broker cannot be reached.
RecoveryInputs cutOff(bool atRest = false) {
    RecoveryInputs in;
    in.stationUp = true;
    in.brokerExpected = true;
    in.brokerUp = false;
    in.atRest = atRest;
    return in;
}

// The station itself is down.
RecoveryInputs stationDown() {
    RecoveryInputs in = cutOff();
    in.stationUp = false;
    return in;
}

// A box that has been on the broker and lost it at `now`.
NetworkRecovery lostAt(uint32_t now) {
    NetworkRecovery recovery;
    TEST_ASSERT_TRUE(recovery.next(online(), now) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::NONE);
    return recovery;
}

// The same, up to the first question to the dashboard. Returns the time.
uint32_t untilFirstCheck(NetworkRecovery &recovery, uint32_t lostAtMs) {
    const uint32_t now = lostAtMs + kCheck;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::CHECK);
    return now;
}

// Up to the point where the box wants to restart. Returns the time.
uint32_t untilRestartWanted(NetworkRecovery &recovery, uint32_t lostAtMs) {
    uint32_t now = untilFirstCheck(recovery, lostAtMs);
    recovery.checked(false, now);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::RECONNECT);
    now += kAfter;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::CHECK);
    recovery.checked(false, now);
    TEST_ASSERT_TRUE(recovery.restartWanted());
    return now;
}
}  // namespace

void test_a_box_on_the_broker_is_left_alone() {
    NetworkRecovery recovery;
    for (uint32_t now = 0; now < 10 * kCheck; now += 1000) {
        TEST_ASSERT_TRUE(recovery.next(online(), now) == RecoveryStep::NONE);
    }
    TEST_ASSERT_FALSE(recovery.stepped());
}

// A drop is usually mended by the MQTT client within seconds.
void test_a_short_drop_is_left_to_the_client() {
    NetworkRecovery recovery = lostAt(1000);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), 1000 + kCheck - 1) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(online(), 1000 + kCheck) ==
                     RecoveryStep::NONE);
    // A later drop gets the full wait again.
    TEST_ASSERT_TRUE(recovery.next(cutOff(), 5000 + kCheck) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), 5000 + 2 * kCheck - 1) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), 5000 + 2 * kCheck) ==
                     RecoveryStep::CHECK);
}

// The fault: R8 stayed cut off for 40 minutes with its station up, until it
// was restarted.
void test_a_cut_off_box_reconnects_and_then_restarts() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilFirstCheck(recovery, 0);
    // While the question is out nothing else happens.
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + 500) == RecoveryStep::NONE);
    recovery.checked(false, now + 1000);
    now += 1000;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::RECONNECT);
    // Once.
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + 1000) == RecoveryStep::NONE);
    TEST_ASSERT_FALSE(recovery.restartWanted());

    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + kAfter - 1) ==
                     RecoveryStep::NONE);
    now += kAfter;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::CHECK);
    recovery.checked(false, now);
    TEST_ASSERT_TRUE(recovery.restartWanted());

    // At rest for a few seconds without a break, then it restarts.
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest - 1) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest) ==
                     RecoveryStep::RESTART);
    // Nothing more is asked of the caller.
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest + 1000) ==
                     RecoveryStep::NONE);
}

void test_connecting_again_that_helps_ends_it() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilFirstCheck(recovery, 0);
    recovery.checked(false, now);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::RECONNECT);
    TEST_ASSERT_TRUE(recovery.stepped());
    TEST_ASSERT_TRUE(recovery.next(online(), now + 20000) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_FALSE(recovery.stepped());
    // A new drop starts from the beginning.
    now += 30000;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + kCheck - 1) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + kCheck) ==
                     RecoveryStep::CHECK);
}

// The broker is down or refuses the box, and the network is fine: neither
// the Wi-Fi station nor the box is touched.
void test_a_dashboard_that_answers_means_only_the_broker_is_missing() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilFirstCheck(recovery, 0);
    recovery.checked(true, now);
    TEST_ASSERT_FALSE(recovery.restartWanted());
    for (uint32_t later = 1000; later < kRecheck; later += 1000) {
        TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + later) ==
                         RecoveryStep::NONE);
    }
    // Asked again after a while, for as long as it lasts.
    for (int round = 0; round < 5; ++round) {
        now += kRecheck;
        TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) ==
                         RecoveryStep::CHECK);
        recovery.checked(true, now);
        TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + 1000) ==
                         RecoveryStep::NONE);
    }
    // If the dashboard stops answering as well, the box is cut off.
    now += kRecheck;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::CHECK);
    recovery.checked(false, now);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::RECONNECT);
}

void test_an_answer_after_connecting_again_calls_the_restart_off() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilFirstCheck(recovery, 0);
    recovery.checked(false, now);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::RECONNECT);
    now += kAfter;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::CHECK);
    recovery.checked(true, now);
    TEST_ASSERT_FALSE(recovery.restartWanted());
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest + 1000) ==
                     RecoveryStep::NONE);
}

// A box that is in use, moving its paddle or holding a local session is
// never restarted: the caller reports it as not at rest.
void test_the_restart_waits_until_the_box_is_at_rest() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilRestartWanted(recovery, 0);
    for (uint32_t later = 0; later < 30000; later += 1000) {
        TEST_ASSERT_TRUE(recovery.next(cutOff(false), now + later) ==
                         RecoveryStep::NONE);
    }
    now += 30000;
    // At rest, then touched again: the few seconds start over.
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(false), now + kRest - 1000) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + 2 * kRest - 1) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + 2 * kRest) ==
                     RecoveryStep::RESTART);
}

// A box that was busy for a long time restarts on a fresh "no answer", not
// on one from an hour ago.
void test_a_restart_that_waited_long_asks_the_dashboard_once_more() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilRestartWanted(recovery, 0);
    now += kFresh + 600000;
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::NONE);
    now += kRest;
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::CHECK);
    recovery.checked(false, now + 2000);
    now += 2000;
    TEST_ASSERT_TRUE(recovery.restartWanted());
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest) ==
                     RecoveryStep::RESTART);
}

void test_a_late_answer_calls_the_waiting_restart_off() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilRestartWanted(recovery, 0);
    now += kFresh + 1000;
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::NONE);
    now += kRest;
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::CHECK);
    recovery.checked(true, now);
    TEST_ASSERT_FALSE(recovery.restartWanted());
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest) ==
                     RecoveryStep::NONE);
}

void test_being_back_on_the_broker_calls_the_restart_off() {
    NetworkRecovery recovery = lostAt(0);
    const uint32_t now = untilRestartWanted(recovery, 0);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(online(), now + 1000) == RecoveryStep::NONE);
    TEST_ASSERT_FALSE(recovery.restartWanted());
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest + 2000) ==
                     RecoveryStep::NONE);
}

// A restart that did not help must not be followed by another: after it the
// box has not been on the broker, and nothing here starts before it has.
void test_a_box_that_was_never_on_the_broker_is_never_restarted() {
    NetworkRecovery recovery;
    for (uint32_t now = 0; now < 20 * (kCheck + kAfter); now += 1000) {
        TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) ==
                         RecoveryStep::NONE);
    }
    TEST_ASSERT_FALSE(recovery.stepped());
}

// A station that is down is the station keeper's business. An access point
// that is switched off must never restart the box.
void test_a_station_that_is_down_never_leads_to_a_restart() {
    NetworkRecovery recovery;
    TEST_ASSERT_TRUE(recovery.next(online(), 0) == RecoveryStep::NONE);
    for (uint32_t now = 1000; now < 20 * (kCheck + kAfter); now += 1000) {
        TEST_ASSERT_TRUE(recovery.next(stationDown(), now) ==
                         RecoveryStep::NONE);
    }
    TEST_ASSERT_FALSE(recovery.stepped());
}

void test_the_first_wait_starts_when_the_station_is_back() {
    NetworkRecovery recovery = lostAt(0);
    TEST_ASSERT_TRUE(recovery.next(stationDown(), kCheck - 1000) ==
                     RecoveryStep::NONE);
    const uint32_t back = kCheck + 5000;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), back) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), back + kCheck - 1) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), back + kCheck) ==
                     RecoveryStep::CHECK);
}

// Connecting the station again takes it down for a moment, and it can stay
// down. The two minutes are counted from when it is back.
void test_the_wait_after_connecting_again_counts_from_when_the_station_is_back() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilFirstCheck(recovery, 0);
    recovery.checked(false, now);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::RECONNECT);
    for (uint32_t later = 1000; later <= 3 * kAfter; later += 1000) {
        TEST_ASSERT_TRUE(recovery.next(stationDown(), now + later) ==
                         RecoveryStep::NONE);
    }
    now += 3 * kAfter;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + kAfter - 1) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + kAfter) ==
                     RecoveryStep::CHECK);
}

void test_a_station_that_drops_while_a_restart_waits_calls_it_off() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilRestartWanted(recovery, 0);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(stationDown(), now + 1000) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_FALSE(recovery.restartWanted());
    // Back up and still cut off: looked at afresh, with a new question.
    now += 2000;
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kRest) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kAfter) ==
                     RecoveryStep::CHECK);
}

// While a Wi-Fi setup page is up, or the client was taken away for an
// update, the box is not expected on the broker.
void test_a_setup_page_or_a_missing_client_stops_everything() {
    NetworkRecovery recovery = lostAt(0);
    uint32_t now = untilRestartWanted(recovery, 0);
    RecoveryInputs held = cutOff(true);
    held.held = true;
    TEST_ASSERT_TRUE(recovery.next(held, now) == RecoveryStep::NONE);
    TEST_ASSERT_FALSE(recovery.restartWanted());
    TEST_ASSERT_FALSE(recovery.stepped());

    recovery = lostAt(0);
    now = untilRestartWanted(recovery, 0);
    RecoveryInputs noClient = cutOff(true);
    noClient.brokerExpected = false;
    TEST_ASSERT_TRUE(recovery.next(noClient, now) == RecoveryStep::NONE);
    TEST_ASSERT_FALSE(recovery.restartWanted());
    // Afterwards the first wait runs in full again.
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + 1000) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + kCheck) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(true), now + 1000 + kCheck) ==
                     RecoveryStep::CHECK);
}

// An answer that arrives after the box is back on the broker is for nobody.
void test_an_answer_nobody_waits_for_is_dropped() {
    NetworkRecovery recovery = lostAt(0);
    const uint32_t now = untilFirstCheck(recovery, 0);
    TEST_ASSERT_TRUE(recovery.next(online(), now + 1000) == RecoveryStep::NONE);
    recovery.checked(false, now + 2000);
    TEST_ASSERT_FALSE(recovery.stepped());
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + 3000) == RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + 4000) == RecoveryStep::NONE);
}

void test_waits_survive_the_millisecond_counter_wrapping() {
    const uint32_t lost = UINT32_MAX - 20000;
    NetworkRecovery recovery = lostAt(lost);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), lost + kCheck - 1) ==
                     RecoveryStep::NONE);
    uint32_t now = lost + kCheck;
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::CHECK);
    recovery.checked(false, now);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now) == RecoveryStep::RECONNECT);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + kAfter - 1) ==
                     RecoveryStep::NONE);
    TEST_ASSERT_TRUE(recovery.next(cutOff(), now + kAfter) ==
                     RecoveryStep::CHECK);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_box_on_the_broker_is_left_alone);
    RUN_TEST(test_a_short_drop_is_left_to_the_client);
    RUN_TEST(test_a_cut_off_box_reconnects_and_then_restarts);
    RUN_TEST(test_connecting_again_that_helps_ends_it);
    RUN_TEST(test_a_dashboard_that_answers_means_only_the_broker_is_missing);
    RUN_TEST(test_an_answer_after_connecting_again_calls_the_restart_off);
    RUN_TEST(test_the_restart_waits_until_the_box_is_at_rest);
    RUN_TEST(test_a_restart_that_waited_long_asks_the_dashboard_once_more);
    RUN_TEST(test_a_late_answer_calls_the_waiting_restart_off);
    RUN_TEST(test_being_back_on_the_broker_calls_the_restart_off);
    RUN_TEST(test_a_box_that_was_never_on_the_broker_is_never_restarted);
    RUN_TEST(test_a_station_that_is_down_never_leads_to_a_restart);
    RUN_TEST(test_the_first_wait_starts_when_the_station_is_back);
    RUN_TEST(test_the_wait_after_connecting_again_counts_from_when_the_station_is_back);
    RUN_TEST(test_a_station_that_drops_while_a_restart_waits_calls_it_off);
    RUN_TEST(test_a_setup_page_or_a_missing_client_stops_everything);
    RUN_TEST(test_an_answer_nobody_waits_for_is_dropped);
    RUN_TEST(test_waits_survive_the_millisecond_counter_wrapping);
    return UNITY_END();
}
