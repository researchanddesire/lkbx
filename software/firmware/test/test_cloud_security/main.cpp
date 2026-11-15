#include <unity.h>
#include <ArduinoJson.h>
#include <CloudInput.h>
#include <MqttFrame.h>
#include <ClockAuthority.h>
#include <string>
#include <limits>

using namespace lockbox::security;

void setUp() {}
void tearDown() {}

void bootstrap_time_never_changes_application_deadlines() {
    ClockAuthority clock;
    const int64_t start = 1788800000000;
    TEST_ASSERT_TRUE(clock.begin(start, 100));
    TEST_ASSERT_FALSE(clock.begin(0, 500));
    TEST_ASSERT_EQUAL_INT64(start + 50, clock.read(4102444799000, 150));
    TEST_ASSERT_EQUAL_INT64(start + 60, clock.read(0, 160));
    const auto restored = clock.read(0, 200);
    clock.finish();
    TEST_ASSERT_EQUAL_INT64(start + 100, clock.read(restored, 200));
    TEST_ASSERT_TRUE(clock.begin(restored, 200));
    clock.finish();
    TEST_ASSERT_EQUAL_INT64(start + 500, clock.read(start + 500, 250));
}

void fragmented_publication_is_atomic() {
    MqttFrame<128> frame;
    // A valid command prefix must never be dispatched from a larger frame.
    const char* prefix = "{\"type\":\"reboot\"}";
    const int length = std::strlen(prefix);
    TEST_ASSERT_EQUAL(FrameResult::incomplete,
                      frame.append("device", "device", 6, prefix, length, length + 1, 0));
    TEST_ASSERT_EQUAL(FrameResult::complete,
                      frame.append("device", nullptr, 0, " ", 1, length + 1, length));
    TEST_ASSERT_EQUAL(length + 1, frame.size());
}

void wrong_topic_and_invalid_fragments_are_rejected() {
    MqttFrame<32> frame;
    TEST_ASSERT_EQUAL(FrameResult::rejected, frame.append("a", "b", 1, "x", 1, 1, 0));
    TEST_ASSERT_EQUAL(FrameResult::rejected, frame.append("a", nullptr, 0, "x", 1, 1, 0));
    TEST_ASSERT_EQUAL(FrameResult::rejected, frame.append("a", "a", 1, "x", 1, 33, 0));
    TEST_ASSERT_EQUAL(FrameResult::rejected, frame.append("a", "a", 1, "x", -1, 1, 0));
    TEST_ASSERT_EQUAL(FrameResult::rejected, frame.append("a", "a", 1, "x", 1, 1, -1));
    TEST_ASSERT_EQUAL(FrameResult::rejected, frame.append("a", "a", 1, "\0x", 2, 2, 0));
    TEST_ASSERT_EQUAL(FrameResult::incomplete, frame.append("a", "a", 1, "x", 1, 3, 0));
    TEST_ASSERT_EQUAL(FrameResult::rejected, frame.append("a", nullptr, 0, "y", 1, 3, 2));
    TEST_ASSERT_EQUAL(FrameResult::rejected, frame.append("a", nullptr, 0, "z", 1, 3, 1));
    TEST_ASSERT_EQUAL(0, frame.size());
    TEST_ASSERT_EQUAL(FrameResult::complete, frame.append("a", "a", 1, "ok", 2, 2, 0));
    TEST_ASSERT_EQUAL_STRING("ok", frame.data());
}

void incomplete_credentials_never_become_valid_auth() {
    JsonDocument doc;
    deserializeJson(doc, R"({"clientId":"pool-channel","username":"device","password":"synthetic-only","sessionId":"pool-channel","pairingCode":"example","hueShift":120,"isPaired":false})");
    TEST_ASSERT_TRUE(authResponse(doc.as<JsonVariantConst>()));
    doc.remove("password");
    TEST_ASSERT_FALSE(authResponse(doc.as<JsonVariantConst>()));
    doc["password"] = 123;
    TEST_ASSERT_FALSE(authResponse(doc.as<JsonVariantConst>()));
    doc["password"] = std::string(513, 'a');
    TEST_ASSERT_FALSE(authResponse(doc.as<JsonVariantConst>()));
    doc["password"] = "synthetic-only";
    doc["isPaired"] = "false";
    TEST_ASSERT_FALSE(authResponse(doc.as<JsonVariantConst>()));
    deserializeJson(doc, R"({"password":"safe\u0000suffix"})");
    TEST_ASSERT_FALSE(boundedString(doc["password"], 1, 512));
}

void command_key_in_auth_is_optional_but_must_be_exact() {
    JsonDocument doc;
    deserializeJson(doc, R"({"clientId":"pool-channel","username":"device","password":"synthetic-only","sessionId":"pool-channel","pairingCode":"example","hueShift":120,"isPaired":false})");
    TEST_ASSERT_TRUE(authResponse(doc.as<JsonVariantConst>()));
    doc["commandKey"] = "BWt_KAZ_bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOA";
    TEST_ASSERT_TRUE(authResponse(doc.as<JsonVariantConst>()));
    for (const auto* bad : {"", "short", "BWt_KAZ_bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOA=",
                            "BWt/KAZ+bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOA",
                            "BWt_KAZ_bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOB"}) {
        doc["commandKey"] = bad;
        TEST_ASSERT_FALSE_MESSAGE(authResponse(doc.as<JsonVariantConst>()), bad);
    }
    doc["commandKey"] = 123;
    TEST_ASSERT_FALSE(authResponse(doc.as<JsonVariantConst>()));
}

void malformed_commands_have_no_accepted_action() {
    JsonDocument doc;
    const char* rejected[] = {"{}", "[]", R"({"type":"sync"})",
        R"({"type":"sync","clear":"true"})",
        R"({"type":"process","message":"lock"})",
        R"({"type":"process","message":"unlock"})",
        R"({"type":"process","message":"lockTimer","lockEnd":1788800000000})"};
    for (const auto* json : rejected) {
        TEST_ASSERT_FALSE(deserializeJson(doc, json));
        TEST_ASSERT_FALSE_MESSAGE(commandResponse(doc.as<JsonVariantConst>()), json);
    }
    for (const auto* json : {R"({"type":"reboot"})", R"({"type":"sync","clear":true})"}) {
        deserializeJson(doc, json);
        TEST_ASSERT_TRUE(commandResponse(doc.as<JsonVariantConst>()));
    }
}

void session_types_and_ranges_are_checked_before_persistence() {
    JsonDocument doc;
    deserializeJson(doc, R"({"sessionId":"synthetic","lockState":"locked","lockStart":1788800000000,"lockEnd":1788801000000,"duration":1000000})");
    TEST_ASSERT_TRUE(sessionResponse(doc.as<JsonVariantConst>()));
    doc["lockEnd"] = "1788801000000";
    TEST_ASSERT_FALSE(sessionResponse(doc.as<JsonVariantConst>()));
    doc["lockEnd"] = 1788801000000ULL;
    doc["lockState"] = "unknown";
    TEST_ASSERT_FALSE(sessionResponse(doc.as<JsonVariantConst>()));
    doc["lockState"] = "break_ending";
    TEST_ASSERT_TRUE(sessionResponse(doc.as<JsonVariantConst>()));
    doc["breakKeyholderOnly"] = true;
    TEST_ASSERT_TRUE(sessionResponse(doc.as<JsonVariantConst>()));
    doc["breakKeyholderOnly"] = "true";
    TEST_ASSERT_FALSE(sessionResponse(doc.as<JsonVariantConst>()));
    doc.remove("breakKeyholderOnly");
    // Whether Chaster controls the session decides whether the box asks before
    // it opens, so only a real boolean is accepted.
    doc["chasterControlled"] = true;
    TEST_ASSERT_TRUE(sessionResponse(doc.as<JsonVariantConst>()));
    doc["chasterControlled"] = "false";
    TEST_ASSERT_FALSE(sessionResponse(doc.as<JsonVariantConst>()));
    doc.remove("chasterControlled");
    doc["requiresPermission"] = true;
    TEST_ASSERT_TRUE(sessionResponse(doc.as<JsonVariantConst>()));
    doc["requiresPermission"] = 1;
    TEST_ASSERT_FALSE(sessionResponse(doc.as<JsonVariantConst>()));
    doc.remove("requiresPermission");
    doc["lockState"] = "pending";
    TEST_ASSERT_FALSE(sessionResponse(doc.as<JsonVariantConst>()));
    TEST_ASSERT_TRUE(sessionResponse(doc.as<JsonVariantConst>(), true));
    doc["duration"] = -1;
    TEST_ASSERT_FALSE(sessionResponse(doc.as<JsonVariantConst>(), true));
}

void postgres_integral_decimal_timestamps_remain_compatible() {
    JsonDocument doc;
    const char* accepted[] = {"0", "1800000000000.000000", "1800000000000",
        "1.8e12", "9007199254740991"};
    for (const auto* json : accepted) {
        TEST_ASSERT_FALSE(deserializeJson(doc, json));
        TEST_ASSERT_TRUE_MESSAGE(boundedNumber(doc.as<JsonVariantConst>(), true), json);
    }
    const char* rejected[] = {"-1", "1800000000000.5", "9007199254740992",
        "9007199254740991.0", "9007199254740992.0", "1e30", "true", "false", "\"1800000000000\"", "null"};
    for (const auto* json : rejected) {
        TEST_ASSERT_FALSE(deserializeJson(doc, json));
        TEST_ASSERT_FALSE_MESSAGE(boundedNumber(doc.as<JsonVariantConst>(), true), json);
    }
    doc.set(std::numeric_limits<double>::infinity());
    TEST_ASSERT_FALSE(boundedNumber(doc.as<JsonVariantConst>(), true));
    doc.set(std::numeric_limits<double>::quiet_NaN());
    TEST_ASSERT_FALSE(boundedNumber(doc.as<JsonVariantConst>(), true));

    deserializeJson(doc, R"({"type":"sync","sessionId":"synthetic","lockState":"locked","lockStart":1800000000000.000000,"lockEnd":1800003600000.000000,"duration":3600})");
    TEST_ASSERT_TRUE(commandResponse(doc.as<JsonVariantConst>()));
    TEST_ASSERT_EQUAL_UINT64(1800003600000ULL, doc["lockEnd"].as<uint64_t>());
}

void http_pending_offers_never_classify_as_active_locks() {
    JsonDocument doc;
    deserializeJson(doc, R"({"sessionId":"synthetic","lockState":"pending","lockStart":null,"lockEnd":null,"duration":3600000})");
    TEST_ASSERT_EQUAL(HttpSessionKind::pending, httpSessionKind(doc.as<JsonVariantConst>()));
    doc["lockState"] = "locked";
    TEST_ASSERT_EQUAL(HttpSessionKind::invalid, httpSessionKind(doc.as<JsonVariantConst>()));
    doc["lockState"] = "pending";
    doc["lockStart"] = 0;
    doc["lockEnd"] = 0;
    TEST_ASSERT_EQUAL(HttpSessionKind::pending, httpSessionKind(doc.as<JsonVariantConst>()));
    TEST_ASSERT_TRUE(sessionResponse(doc.as<JsonVariantConst>(), true));
    doc["duration"] = "3600000";
    TEST_ASSERT_EQUAL(HttpSessionKind::invalid, httpSessionKind(doc.as<JsonVariantConst>()));
    doc["duration"] = 3600000;
    doc["lockState"] = "locked";
    doc["lockStart"] = 1800000000000ULL;
    doc["lockEnd"] = 1800003600000ULL;
    TEST_ASSERT_EQUAL(HttpSessionKind::active, httpSessionKind(doc.as<JsonVariantConst>()));
    // A permanent lock has no end date; MQTT sends 0 where HTTP sends null.
    doc["lockEnd"] = nullptr;
    TEST_ASSERT_EQUAL(HttpSessionKind::active, httpSessionKind(doc.as<JsonVariantConst>()));
    TEST_ASSERT_FALSE(sessionResponse(doc.as<JsonVariantConst>(), true));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(bootstrap_time_never_changes_application_deadlines);
    RUN_TEST(fragmented_publication_is_atomic);
    RUN_TEST(wrong_topic_and_invalid_fragments_are_rejected);
    RUN_TEST(incomplete_credentials_never_become_valid_auth);
    RUN_TEST(command_key_in_auth_is_optional_but_must_be_exact);
    RUN_TEST(malformed_commands_have_no_accepted_action);
    RUN_TEST(session_types_and_ranges_are_checked_before_persistence);
    RUN_TEST(postgres_integral_decimal_timestamps_remain_compatible);
    RUN_TEST(http_pending_offers_never_classify_as_active_locks);
    return UNITY_END();
}
