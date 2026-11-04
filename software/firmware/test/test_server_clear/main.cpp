#include <ArduinoJson.h>

#include "../../src/utils/serverClear.h"
#include "unity.h"

static const char* kHeld = "6f1c2a4e-7d3b-4f0a-9b1e-2c5d8a7e4f10";
static const char* kOther = "0b9e8d7c-6a5f-4e3d-8c2b-1a0f9e8d7c6b";

void setUp(void) {}
void tearDown(void) {}

// Reads the id exactly as the MQTT handler does.
static ServerClear classifyPayload(const char* payload, const char* held) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, payload));
    TEST_ASSERT_TRUE(doc["clear"].as<bool>());
    return classifyServerClear(doc["sessionId"] | "", held);
}

void test_a_clear_for_the_held_session_unlocks(void) {
    TEST_ASSERT_EQUAL(ServerClear::APPLY, classifyServerClear(kHeld, kHeld));
}

void test_a_clear_for_another_session_is_ignored(void) {
    // A throwaway session of the owner ended while the keyholder's lock runs.
    TEST_ASSERT_EQUAL(ServerClear::IGNORE, classifyServerClear(kOther, kHeld));
}

void test_a_named_clear_is_ignored_when_no_session_is_held(void) {
    TEST_ASSERT_EQUAL(ServerClear::IGNORE, classifyServerClear(kOther, ""));
    TEST_ASSERT_EQUAL(ServerClear::IGNORE, classifyServerClear(kOther, nullptr));
}

void test_ids_must_match_exactly(void) {
    TEST_ASSERT_EQUAL(ServerClear::IGNORE,
                      classifyServerClear("6f1c2a4e", kHeld));
    TEST_ASSERT_EQUAL(ServerClear::IGNORE,
                      classifyServerClear("6F1C2A4E-7D3B-4F0A-9B1E-2C5D8A7E4F10", kHeld));
    TEST_ASSERT_EQUAL(ServerClear::IGNORE,
                      classifyServerClear("6f1c2a4e-7d3b-4f0a-9b1e-2c5d8a7e4f10 ", kHeld));
}

void test_a_clear_without_an_id_asks_the_dashboard(void) {
    // Dashboards before this release never name the session.
    TEST_ASSERT_EQUAL(ServerClear::CONFIRM, classifyServerClear("", kHeld));
    TEST_ASSERT_EQUAL(ServerClear::CONFIRM, classifyServerClear(nullptr, kHeld));
    TEST_ASSERT_EQUAL(ServerClear::CONFIRM, classifyServerClear("", ""));
}

void test_the_dashboard_payloads(void) {
    TEST_ASSERT_EQUAL(
        ServerClear::APPLY,
        classifyPayload("{\"type\":\"sync\",\"clear\":true,"
                        "\"sessionId\":\"6f1c2a4e-7d3b-4f0a-9b1e-2c5d8a7e4f10\"}",
                        kHeld));
    TEST_ASSERT_EQUAL(
        ServerClear::IGNORE,
        classifyPayload("{\"type\":\"sync\",\"clear\":true,"
                        "\"sessionId\":\"0b9e8d7c-6a5f-4e3d-8c2b-1a0f9e8d7c6b\"}",
                        kHeld));
    TEST_ASSERT_EQUAL(ServerClear::CONFIRM,
                      classifyPayload("{\"type\":\"sync\",\"clear\":true}", kHeld));
}

void test_an_unusable_id_counts_as_no_id(void) {
    TEST_ASSERT_EQUAL(
        ServerClear::CONFIRM,
        classifyPayload("{\"type\":\"sync\",\"clear\":true,\"sessionId\":null}", kHeld));
    TEST_ASSERT_EQUAL(
        ServerClear::CONFIRM,
        classifyPayload("{\"type\":\"sync\",\"clear\":true,\"sessionId\":42}", kHeld));
    TEST_ASSERT_EQUAL(
        ServerClear::CONFIRM,
        classifyPayload("{\"type\":\"sync\",\"clear\":true,\"sessionId\":\"\"}", kHeld));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_a_clear_for_the_held_session_unlocks);
    RUN_TEST(test_a_clear_for_another_session_is_ignored);
    RUN_TEST(test_a_named_clear_is_ignored_when_no_session_is_held);
    RUN_TEST(test_ids_must_match_exactly);
    RUN_TEST(test_a_clear_without_an_id_asks_the_dashboard);
    RUN_TEST(test_the_dashboard_payloads);
    RUN_TEST(test_an_unusable_id_counts_as_no_id);
    return UNITY_END();
}
