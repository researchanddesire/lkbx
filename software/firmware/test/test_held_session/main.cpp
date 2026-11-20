#include <ArduinoJson.h>

#include "../../src/utils/heldSession.h"
#include "../../src/utils/offeredLock.h"
#include "../../src/utils/serverClear.h"
#include "unity.h"

static const char* kHeld = "6f1c2a4e-7d3b-4f0a-9b1e-2c5d8a7e4f10";
static const char* kOther = "0b9e8d7c-6a5f-4e3d-8c2b-1a0f9e8d7c6b";

void setUp(void) {}
void tearDown(void) {}

// Reads the id exactly as the MQTT handler does.
static HeldSessionSync classifyPayload(const char* payload, const char* held,
                                       const char* state) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, payload));
    return classifyHeldSessionSync(doc["sessionId"] | "", held, state);
}

void test_an_offer_for_another_session_never_reaches_a_locked_box(void) {
    // The wearer reactivated an old self-lock under the keyholder's lock.
    TEST_ASSERT_EQUAL(HeldSessionSync::CONFIRM,
                      classifyHeldSessionSync(kOther, kHeld, "locked"));
    TEST_ASSERT_EQUAL(
        HeldSessionSync::CONFIRM,
        classifyPayload("{\"type\":\"sync\",\"lockState\":\"pending\","
                        "\"sessionId\":\"0b9e8d7c-6a5f-4e3d-8c2b-1a0f9e8d7c6b\"}",
                        kHeld, "locked"));
}

void test_breaks_hold_the_session_too(void) {
    TEST_ASSERT_EQUAL(HeldSessionSync::CONFIRM,
                      classifyHeldSessionSync(kOther, kHeld, "break"));
    TEST_ASSERT_EQUAL(HeldSessionSync::CONFIRM,
                      classifyHeldSessionSync(kOther, kHeld, "break_ending"));
}

void test_any_state_for_another_session_is_confirmed(void) {
    // Not only offers: an "unlocked" or "locked" sync for another session
    // would also replace the held one.
    for (const char* state : {"unlocked", "locked", "break", "pending"}) {
        JsonDocument doc;
        doc["type"] = "sync";
        doc["lockState"] = state;
        doc["sessionId"] = kOther;
        TEST_ASSERT_EQUAL(HeldSessionSync::CONFIRM,
                          classifyHeldSessionSync(doc["sessionId"] | "", kHeld, "locked"));
    }
}

void test_a_sync_without_a_session_is_confirmed_while_locked(void) {
    TEST_ASSERT_EQUAL(HeldSessionSync::CONFIRM,
                      classifyHeldSessionSync("", kHeld, "locked"));
    TEST_ASSERT_EQUAL(HeldSessionSync::CONFIRM,
                      classifyHeldSessionSync(nullptr, kHeld, "locked"));
    TEST_ASSERT_EQUAL(HeldSessionSync::CONFIRM,
                      classifyPayload("{\"type\":\"sync\",\"lockState\":\"unlocked\"}",
                                      kHeld, "locked"));
    TEST_ASSERT_EQUAL(
        HeldSessionSync::CONFIRM,
        classifyPayload("{\"type\":\"sync\",\"lockState\":\"unlocked\",\"sessionId\":null}",
                        kHeld, "locked"));
    TEST_ASSERT_EQUAL(
        HeldSessionSync::CONFIRM,
        classifyPayload("{\"type\":\"sync\",\"lockState\":\"unlocked\",\"sessionId\":42}",
                        kHeld, "locked"));
}

void test_the_held_session_keeps_its_own_messages(void) {
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kHeld, kHeld, "locked"));
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kHeld, kHeld, "break"));
    TEST_ASSERT_EQUAL(
        HeldSessionSync::APPLY,
        classifyPayload("{\"type\":\"sync\",\"lockState\":\"unlocked\","
                        "\"sessionId\":\"6f1c2a4e-7d3b-4f0a-9b1e-2c5d8a7e4f10\"}",
                        kHeld, "locked"));
}

void test_a_re_offer_of_the_held_session_changes_nothing(void) {
    // It is applied, and saveState keeps the accepted decision.
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kHeld, kHeld, "locked"));
    TEST_ASSERT_TRUE(preserveOfferedDecision(true, true, false));
}

void test_ids_must_match_exactly(void) {
    TEST_ASSERT_EQUAL(HeldSessionSync::CONFIRM,
                      classifyHeldSessionSync("6f1c2a4e", kHeld, "locked"));
    TEST_ASSERT_EQUAL(
        HeldSessionSync::CONFIRM,
        classifyHeldSessionSync("6F1C2A4E-7D3B-4F0A-9B1E-2C5D8A7E4F10", kHeld, "locked"));
}

void test_a_box_without_a_locked_session_applies_syncs_as_before(void) {
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY, classifyHeldSessionSync(kOther, "", ""));
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kOther, nullptr, nullptr));
    // A pending offer is not accepted yet: a newer offer replaces it.
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kOther, kHeld, "pending"));
    // A declined, completed or unlocked session no longer holds the wearer.
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kOther, kHeld, "abandoned"));
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kOther, kHeld, "completed"));
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kOther, kHeld, "unlocked"));
    TEST_ASSERT_EQUAL(HeldSessionSync::APPLY,
                      classifyHeldSessionSync(kOther, kHeld, ""));
}

void test_clears_keep_their_own_rule(void) {
    // A clear for another session is still ignored, not confirmed.
    TEST_ASSERT_EQUAL(ServerClear::IGNORE, classifyServerClear(kOther, kHeld));
    TEST_ASSERT_EQUAL(ServerClear::APPLY, classifyServerClear(kHeld, kHeld));
    TEST_ASSERT_EQUAL(ServerClear::CONFIRM, classifyServerClear("", kHeld));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_an_offer_for_another_session_never_reaches_a_locked_box);
    RUN_TEST(test_breaks_hold_the_session_too);
    RUN_TEST(test_any_state_for_another_session_is_confirmed);
    RUN_TEST(test_a_sync_without_a_session_is_confirmed_while_locked);
    RUN_TEST(test_the_held_session_keeps_its_own_messages);
    RUN_TEST(test_a_re_offer_of_the_held_session_changes_nothing);
    RUN_TEST(test_ids_must_match_exactly);
    RUN_TEST(test_a_box_without_a_locked_session_applies_syncs_as_before);
    RUN_TEST(test_clears_keep_their_own_rule);
    return UNITY_END();
}
