#include <ArduinoJson.h>
#include <DeviceKey.h>
#include <unity.h>

#include <cstring>
#include <string>

using namespace lockbox::security;

namespace {

// Shared with rad-app lib/lockbox-device/__tests__/v2Vector.spec.ts. The
// dashboard derives the same values from the key it issued.
constexpr const char* VECTOR_KEY = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
constexpr const char* VECTOR_TOKEN = "jlTDfyPZWokCUjnMoagQMl9KzFB7yzit4CiLmJ9QunY";
constexpr const char* VECTOR_V1_SECRET = "jNHn_HG_26kKo7YjnUsXjV4a7V94lm28B_7n0Y8BirI";

void vectorKey(std::uint8_t (&key)[DEVICE_KEY_BYTES]) {
    for (std::size_t index = 0; index < DEVICE_KEY_BYTES; ++index) {
        key[index] = static_cast<std::uint8_t>(index);
    }
}

std::string hmacText(const std::uint8_t (&key)[DEVICE_KEY_BYTES], const char* message) {
    std::uint8_t digest[HMAC_SHA256_BYTES];
    TEST_ASSERT_TRUE(hmacSha256(key, sizeof(key),
                                reinterpret_cast<const std::uint8_t*>(message),
                                std::strlen(message), digest));
    return deviceTokenText(digest);
}

bool parseKey(const char* json, std::uint8_t (&key)[DEVICE_KEY_BYTES]) {
    JsonDocument document;
    TEST_ASSERT_FALSE(deserializeJson(document, json));
    return provisionedDeviceKey(document.as<JsonVariantConst>(), key);
}

void test_token_matches_the_dashboard() {
    std::uint8_t key[DEVICE_KEY_BYTES];
    vectorKey(key);
    TEST_ASSERT_EQUAL_STRING(VECTOR_TOKEN, hmacText(key, DEVICE_TOKEN_LABEL).c_str());
    // The v1 secret older images derive from the same burned key.
    TEST_ASSERT_EQUAL_STRING(
        VECTOR_V1_SECRET,
        hmacText(key, "lkbx-device-secret-v1:AA:BB:CC:DD:EE:FF").c_str());
}

void test_accepts_the_issued_key() {
    std::uint8_t key[DEVICE_KEY_BYTES];
    const std::string json = std::string("{\"deviceKey\":\"") + VECTOR_KEY + "\",\"keyVersion\":1}";
    TEST_ASSERT_TRUE(parseKey(json.c_str(), key));
    std::uint8_t expected[DEVICE_KEY_BYTES];
    vectorKey(expected);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, key, DEVICE_KEY_BYTES);
}

void test_rejects_anything_else() {
    std::uint8_t key[DEVICE_KEY_BYTES];
    const std::uint8_t zero[DEVICE_KEY_BYTES] = {};
    TEST_ASSERT_FALSE(parseKey("{}", key));
    TEST_ASSERT_FALSE(parseKey("{\"deviceKey\":42}", key));
    // 42 characters, and 44 with padding.
    TEST_ASSERT_FALSE(parseKey("{\"deviceKey\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh\"}", key));
    TEST_ASSERT_FALSE(parseKey("{\"deviceKey\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}", key));
    // Standard base64 alphabet, and non-zero trailing bits.
    TEST_ASSERT_FALSE(parseKey("{\"deviceKey\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdH+8\"}", key));
    TEST_ASSERT_FALSE(parseKey("{\"deviceKey\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh9\"}", key));
    // Keys no working server would issue are never burned.
    TEST_ASSERT_FALSE(parseKey("{\"deviceKey\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"}", key));
    TEST_ASSERT_FALSE(parseKey("{\"deviceKey\":\"__________________________________________8\"}", key));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, key, DEVICE_KEY_BYTES);
}

void test_answers() {
    TEST_ASSERT_TRUE(provisionAnswer(201) == ProvisionAnswer::burn);
    TEST_ASSERT_TRUE(provisionAnswer(409) == ProvisionAnswer::neverAgain);
    TEST_ASSERT_TRUE(provisionAnswer(403) == ProvisionAnswer::neverAgain);
    TEST_ASSERT_TRUE(provisionAnswer(429) == ProvisionAnswer::retryLater);
    // An older dashboard without v2, a server error, or no response at all.
    TEST_ASSERT_TRUE(provisionAnswer(404) == ProvisionAnswer::nextBoot);
    TEST_ASSERT_TRUE(provisionAnswer(503) == ProvisionAnswer::nextBoot);
    TEST_ASSERT_TRUE(provisionAnswer(-1) == ProvisionAnswer::nextBoot);
    TEST_ASSERT_TRUE(provisionAnswer(200) == ProvisionAnswer::nextBoot);
}

void test_asks_once_until_answered() {
    TEST_ASSERT_TRUE(mayRequestDeviceKey(false, 0, 0, false));
    TEST_ASSERT_FALSE(mayRequestDeviceKey(true, 0, 0, true));
    // Rate limited: wait the window, and never on an unverified clock.
    TEST_ASSERT_FALSE(mayRequestDeviceKey(false, 2000, 1999, true));
    TEST_ASSERT_TRUE(mayRequestDeviceKey(false, 2000, 2000, true));
    TEST_ASSERT_FALSE(mayRequestDeviceKey(false, 2000, 5000, false));
}

void test_failure_codes_are_e_lock_999_family() {
    TEST_ASSERT_EQUAL_INT(999, failureCode(DeviceKeyFailure::unknown));
    TEST_ASSERT_EQUAL_INT(995, failureCode(DeviceKeyFailure::writeFailed));
    TEST_ASSERT_EQUAL_INT(0, failureCode(DeviceKeyFailure::none));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_token_matches_the_dashboard);
    RUN_TEST(test_accepts_the_issued_key);
    RUN_TEST(test_rejects_anything_else);
    RUN_TEST(test_answers);
    RUN_TEST(test_asks_once_until_answered);
    RUN_TEST(test_failure_codes_are_e_lock_999_family);
    return UNITY_END();
}
