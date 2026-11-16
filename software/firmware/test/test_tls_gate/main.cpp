#include <unity.h>

#include "../../src/utils/tlsGate.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kLease = tlsGate::kLeaseMs;
constexpr uint32_t kSettle = tlsGate::kMqttSettleMs;
}  // namespace

void test_the_first_to_ask_gets_the_turn() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.holder(0) == TlsUser::NONE);
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, 100));
    TEST_ASSERT_TRUE(gate.holder(100) == TlsUser::MQTT);
}

// The fault: the MQTT client connected again and the state read opened its
// own secure connection within the same two seconds.
void test_a_request_waits_while_the_client_is_connecting() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, 0));
    TEST_ASSERT_FALSE(gate.take(TlsUser::HTTP, 10));
    TEST_ASSERT_FALSE(gate.take(TlsUser::HTTP, 2000));
    // A request that was refused holds nothing.
    TEST_ASSERT_TRUE(gate.holder(2000) == TlsUser::MQTT);
    gate.give(TlsUser::MQTT);
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, 2050));
}

void test_the_client_waits_while_a_request_is_connecting() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, 0));
    TEST_ASSERT_FALSE(gate.take(TlsUser::MQTT, 500));
    gate.give(TlsUser::HTTP);
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, 550));
}

// Connected, the client sends its subscription, its provenance record and
// waiting reports at once: a request does not connect on top of them.
void test_the_client_keeps_its_turn_for_a_moment_after_connecting() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, 0));
    gate.giveAfter(TlsUser::MQTT, 3000, kSettle);
    TEST_ASSERT_FALSE(gate.take(TlsUser::HTTP, 3000));
    TEST_ASSERT_FALSE(gate.take(TlsUser::HTTP, 3000 + kSettle - 1));
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, 3000 + kSettle));
    TEST_ASSERT_TRUE(gate.holder(3000 + kSettle) == TlsUser::HTTP);
}

// A drop inside that moment gives the turn back at once.
void test_a_drop_right_after_connecting_ends_the_turn() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, 0));
    gate.giveAfter(TlsUser::MQTT, 1000, kSettle);
    gate.give(TlsUser::MQTT);
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, 1001));
}

// Nobody waits forever: a turn that was never given back runs out.
void test_a_turn_that_is_never_given_back_runs_out() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, 0));
    TEST_ASSERT_FALSE(gate.take(TlsUser::MQTT, kLease - 1));
    TEST_ASSERT_TRUE(gate.holder(kLease) == TlsUser::NONE);
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, kLease));
}

// The client's next attempt follows a failed one without a word in between.
void test_the_holder_taking_again_starts_a_new_turn() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, 0));
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, kLease - 1000));
    // Counted from the second take.
    TEST_ASSERT_FALSE(gate.take(TlsUser::HTTP, kLease + 500));
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, 2 * kLease - 1000));
}

// A caller that waited in vain connects anyway and reports the end of its
// connection as usual. That must not end the turn of whoever holds it.
void test_ending_a_turn_one_does_not_hold_changes_nothing() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, 0));
    gate.give(TlsUser::HTTP);
    gate.giveAfter(TlsUser::HTTP, 10, 1);
    TEST_ASSERT_TRUE(gate.holder(100) == TlsUser::MQTT);
    TEST_ASSERT_FALSE(gate.take(TlsUser::PROBE, 100));
}

void test_three_users_take_turns() {
    TlsGate gate;
    TEST_ASSERT_TRUE(gate.take(TlsUser::PROBE, 0));
    TEST_ASSERT_FALSE(gate.take(TlsUser::MQTT, 100));
    TEST_ASSERT_FALSE(gate.take(TlsUser::HTTP, 100));
    gate.give(TlsUser::PROBE);
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, 200));
    TEST_ASSERT_FALSE(gate.take(TlsUser::PROBE, 300));
    gate.giveAfter(TlsUser::MQTT, 1000, kSettle);
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, 1000 + kSettle));
    TEST_ASSERT_FALSE(gate.take(TlsUser::PROBE, 1000 + kSettle + 10));
}

void test_nobody_is_not_a_user() {
    TlsGate gate;
    TEST_ASSERT_FALSE(gate.take(TlsUser::NONE, 0));
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, 0));
}

void test_turns_survive_the_millisecond_counter_wrapping() {
    TlsGate gate;
    const uint32_t takenAt = UINT32_MAX - 1000;
    TEST_ASSERT_TRUE(gate.take(TlsUser::MQTT, takenAt));
    TEST_ASSERT_FALSE(gate.take(TlsUser::HTTP, takenAt + 2000));
    TEST_ASSERT_FALSE(gate.take(TlsUser::HTTP, takenAt + kLease - 1));
    TEST_ASSERT_TRUE(gate.take(TlsUser::HTTP, takenAt + kLease));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_first_to_ask_gets_the_turn);
    RUN_TEST(test_a_request_waits_while_the_client_is_connecting);
    RUN_TEST(test_the_client_waits_while_a_request_is_connecting);
    RUN_TEST(test_the_client_keeps_its_turn_for_a_moment_after_connecting);
    RUN_TEST(test_a_drop_right_after_connecting_ends_the_turn);
    RUN_TEST(test_a_turn_that_is_never_given_back_runs_out);
    RUN_TEST(test_the_holder_taking_again_starts_a_new_turn);
    RUN_TEST(test_ending_a_turn_one_does_not_hold_changes_nothing);
    RUN_TEST(test_three_users_take_turns);
    RUN_TEST(test_nobody_is_not_a_user);
    RUN_TEST(test_turns_survive_the_millisecond_counter_wrapping);
    return UNITY_END();
}
