#include <ArduinoJson.h>

#include <string>

#include "unity.h"

#include "../../src/utils/voteLink.h"

void setUp() {}
void tearDown() {}

namespace {

constexpr const char* kChasterLink =
    "https://chaster.app/sessions/TIT7McrzudRgGKMB";

VoteUrlSync parse(const char* payload) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, payload));
    return voteUrlFromJson(doc, "voteUrl");
}

}  // namespace

void test_chaster_share_link_is_kept() {
    const VoteUrlSync sync =
        parse("{\"voteUrl\":\"https://chaster.app/sessions/TIT7McrzudRgGKMB\"}");
    TEST_ASSERT_TRUE(sync.present);
    TEST_ASSERT_EQUAL_STRING(kChasterLink, sync.url);
}

void test_missing_field_keeps_the_stored_link() {
    // Older servers and unencrypted commands leave the field out.
    const VoteUrlSync sync = parse("{\"type\":\"sync\",\"lockState\":\"locked\"}");
    TEST_ASSERT_FALSE(sync.present);
    TEST_ASSERT_EQUAL_STRING("", sync.url);
}

void test_empty_or_null_clears_the_stored_link() {
    for (const char* payload : {"{\"voteUrl\":\"\"}", "{\"voteUrl\":null}"}) {
        const VoteUrlSync sync = parse(payload);
        TEST_ASSERT_TRUE(sync.present);
        TEST_ASSERT_EQUAL_STRING("", sync.url);
    }
}

void test_unusable_links_are_never_shown() {
    const std::string tooLong =
        "{\"voteUrl\":\"https://chaster.app/sessions/" + std::string(100, 'a') +
        "\"}";
    for (const char* payload :
         {"{\"voteUrl\":\"http://chaster.app/sessions/TIT7McrzudRgGKMB\"}",
          "{\"voteUrl\":\"javascript:alert(1)\"}",
          "{\"voteUrl\":\"https://\"}",
          "{\"voteUrl\":\"https://chaster.app/sessions/a b\"}",
          "{\"voteUrl\":\"https://chaster.app/\\\"quoted\"}",
          "{\"voteUrl\":\"https://chaster.app/\\u00e9\"}",
          "{\"voteUrl\":\"https://chaster.app/\\n\"}",
          "{\"voteUrl\":42}",
          "{\"voteUrl\":true}",
          "{\"voteUrl\":{\"url\":\"https://chaster.app\"}}",
          tooLong.c_str()}) {
        const VoteUrlSync sync = parse(payload);
        // Present, so an unusable value clears an older link rather than
        // leaving a stale one on the box.
        TEST_ASSERT_TRUE_MESSAGE(sync.present, payload);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", sync.url, payload);
    }
}

void test_longest_link_fits_the_qr_code() {
    // 120 characters is the most the stored link may hold.
    std::string url = "https://chaster.app/sessions/";
    url += std::string(kVoteUrlBytes - 1 - url.size(), 'x');
    TEST_ASSERT_EQUAL(kVoteUrlBytes - 1, url.size());
    TEST_ASSERT_TRUE(voteUrlUsable(url.c_str()));
    TEST_ASSERT_FALSE(voteUrlUsable((url + "x").c_str()));
}

void test_usable_rejects_missing_text() {
    TEST_ASSERT_FALSE(voteUrlUsable(nullptr));
    TEST_ASSERT_FALSE(voteUrlUsable(""));
    TEST_ASSERT_TRUE(voteUrlUsable(kChasterLink));
}

void test_http_and_ble_shapes_read_the_nested_session() {
    // The HTTP state nests the session under "data"; BLE passes the session
    // object itself. Both read the same field.
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(
        doc,
        "{\"ok\":true,\"data\":{\"sessionId\":\"s\",\"voteUrl\":"
        "\"https://chaster.app/sessions/TIT7McrzudRgGKMB\"}}"));
    const auto http = voteUrlFromJson(doc["data"], "voteUrl");
    TEST_ASSERT_TRUE(http.present);
    TEST_ASSERT_EQUAL_STRING(kChasterLink, http.url);

    const JsonObjectConst session = doc["data"].as<JsonObjectConst>();
    const auto ble = voteUrlFromJson(session, "voteUrl");
    TEST_ASSERT_TRUE(ble.present);
    TEST_ASSERT_EQUAL_STRING(kChasterLink, ble.url);

    const auto absent = voteUrlFromJson(doc["data"], "missing");
    TEST_ASSERT_FALSE(absent.present);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_chaster_share_link_is_kept);
    RUN_TEST(test_missing_field_keeps_the_stored_link);
    RUN_TEST(test_empty_or_null_clears_the_stored_link);
    RUN_TEST(test_unusable_links_are_never_shown);
    RUN_TEST(test_longest_link_fits_the_qr_code);
    RUN_TEST(test_usable_rejects_missing_text);
    RUN_TEST(test_http_and_ble_shapes_read_the_nested_session);
    return UNITY_END();
}
