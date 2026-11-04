#include <ArduinoJson.h>
#include <CommandEnvelope.h>
#include <SigningPolicy.h>
#include <unity.h>

#include <cstring>
#include <string>

using namespace lockbox::security;

namespace {
const char* MAC = "AA:BB:CC:DD:EE:FF";
const char* COMMAND_KEY = "BWt_KAZ_bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOA";  // gitleaks:allow
const char* PLAINTEXT = R"({"type":"sync","clear":true})";

CommandGateResult openPlain(CommandGate& gate) {
    std::string command;
    return gate.open(MAC, PLAINTEXT, std::strlen(PLAINTEXT), command);
}
}  // namespace

void setUp() {}
void tearDown() {}

void a_box_that_never_signed_keeps_plain_json() {
    TEST_ASSERT_FALSE(signaturesRequired(false, false));
    CommandGate gate;
    gate.requireSignatures(signaturesRequired(false, false));
    TEST_ASSERT_EQUAL(CommandGateResult::plain, openPlain(gate));
    TEST_ASSERT_TRUE(mayReportPlain(true, false, false, false));
}

void a_v1_box_that_signed_once_never_takes_plain_json_again() {
    // A 200 without commandKey keeps the stored key instead of erasing it.
    TEST_ASSERT_EQUAL(AuthKeyAction::keep, authKeyAction(false));
    TEST_ASSERT_TRUE(signaturesRequired(false, true));
    // Even with a corrupt stored key (no key in the gate), plain is refused.
    CommandGate gate;
    gate.requireSignatures(signaturesRequired(false, true));
    TEST_ASSERT_EQUAL(CommandGateResult::unsignedRefused, openPlain(gate));
    TEST_ASSERT_FALSE(mayReportPlain(true, false, true, false));
}

void a_burned_key_requires_signatures_from_the_start() {
    TEST_ASSERT_TRUE(signaturesRequired(true, false));
    CommandGate gate;
    gate.requireSignatures(signaturesRequired(true, false));
    TEST_ASSERT_EQUAL(CommandGateResult::unsignedRefused, openPlain(gate));
    TEST_ASSERT_FALSE(mayReportPlain(true, true, false, false));
}

void a_new_key_still_replaces_the_stored_one() {
    TEST_ASSERT_EQUAL(AuthKeyAction::store, authKeyAction(true));
    CommandGate gate;
    gate.requireSignatures(signaturesRequired(false, true));
    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    // A keyed gate takes only envelopes.
    TEST_ASSERT_EQUAL(CommandGateResult::malformed, openPlain(gate));
}

void nothing_goes_out_plain_before_the_gate_has_loaded() {
    // Whether a key is held is unknown until NVS was read.
    TEST_ASSERT_FALSE(mayReportPlain(false, false, false, false));
    TEST_ASSERT_FALSE(mayReportPlain(false, true, true, true));
}

void a_box_holding_a_key_never_reports_plain() {
    TEST_ASSERT_FALSE(mayReportPlain(true, false, false, true));
    TEST_ASSERT_FALSE(mayReportPlain(true, false, true, true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(a_box_that_never_signed_keeps_plain_json);
    RUN_TEST(a_v1_box_that_signed_once_never_takes_plain_json_again);
    RUN_TEST(a_burned_key_requires_signatures_from_the_start);
    RUN_TEST(a_new_key_still_replaces_the_stored_one);
    RUN_TEST(nothing_goes_out_plain_before_the_gate_has_loaded);
    RUN_TEST(a_box_holding_a_key_never_reports_plain);
    return UNITY_END();
}
