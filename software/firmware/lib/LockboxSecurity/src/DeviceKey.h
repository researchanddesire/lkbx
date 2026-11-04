#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "Base64Url.h"
#include "CommandCrypto.h"

namespace lockbox::security {

// v2 device identity. The dashboard issues one random 256-bit key, exactly
// once, and the device burns it into a read-protected eFuse block with purpose
// HMAC_UP. From then on the device proves itself with HMACs of fixed labels and
// never sends its MAC as proof. The labels are shared with the dashboard's SQL
// and must never change.
constexpr std::size_t DEVICE_KEY_BYTES = 32;
constexpr std::size_t DEVICE_KEY_TEXT_LENGTH = 43;
constexpr const char* DEVICE_TOKEN_LABEL = "lkbx-device-token-v2";
// Proves a fresh burn with the HMAC peripheral while the key is still in RAM.
constexpr const char* DEVICE_KEY_CHECK_LABEL = "lkbx-device-key-check-v2";
// Names the device on the broker after a v2 sign-in, in place of its MAC. It
// is an address derived from the burned key, not proof: anyone who sees a
// topic can read it.
constexpr const char* DEVICE_ID_LABEL = "lkbx-device-id-v2";
constexpr std::size_t DEVICE_ID_BYTES = 16;
constexpr std::size_t DEVICE_ID_TEXT_LENGTH = 2 * DEVICE_ID_BYTES;
// Broker topics after a v2 sign-in: "v2/<device id>", then "/log" and so on.
constexpr const char* V2_TOPIC_PREFIX = "v2/";

// A failure to store the key is shown as E-LOCK-999 and reported to the
// dashboard with the next key request, so a replacement can be issued.
enum class DeviceKeyFailure : int {
    none = 0,
    invalidKey = 993,
    noFreeBlock = 994,
    writeFailed = 995,
    verifyFailed = 996,
    burnRefused = 997,
    unknown = 999,
};

inline int failureCode(DeviceKeyFailure failure) {
    return static_cast<int>(failure);
}

/**
 * Accepts only `{"deviceKey": "<43 base64url characters>"}` holding 32 usable
 * bytes. An all-zero or all-one key would mean a broken server: never burn it.
 */
inline bool provisionedDeviceKey(JsonVariantConst body,
                                 std::uint8_t (&key)[DEVICE_KEY_BYTES]) {
    std::memset(key, 0, sizeof(key));
    JsonVariantConst value = body["deviceKey"];
    if (!value.is<const char*>()) return false;
    const char* text = value.as<const char*>();
    const std::size_t length = std::strlen(text);
    std::size_t written = 0;
    if (length != DEVICE_KEY_TEXT_LENGTH ||
        !base64UrlDecode(text, length, key, sizeof(key), written) ||
        written != DEVICE_KEY_BYTES) {
        std::memset(key, 0, sizeof(key));
        return false;
    }
    bool allZero = true;
    bool allOne = true;
    for (const std::uint8_t byte : key) {
        allZero = allZero && byte == 0x00;
        allOne = allOne && byte == 0xFF;
    }
    if (allZero || allOne) {
        std::memset(key, 0, sizeof(key));
        return false;
    }
    return true;
}

enum class ProvisionAnswer {
    // 201: the only copy of the key; burn it now.
    burn,
    // 409 or 403: the dashboard will never send this device a key. Ask again
    // only after the stored answer is erased (factory reset).
    neverAgain,
    // 429: wait out the window before asking.
    retryLater,
    // 428: the dashboard has no secret for this box yet. Sign in with v1,
    // which enrolls it, then ask once more in the same boot.
    afterSignIn,
    // Anything else (an older dashboard without v2, a network failure): try
    // again on a later boot, signing in with v1 meanwhile.
    nextBoot,
};

inline ProvisionAnswer provisionAnswer(int status) {
    if (status == 201) return ProvisionAnswer::burn;
    if (status == 409 || status == 403) return ProvisionAnswer::neverAgain;
    if (status == 429) return ProvisionAnswer::retryLater;
    if (status == 428) return ProvisionAnswer::afterSignIn;
    return ProvisionAnswer::nextBoot;
}

// The dashboard counts key requests per address per hour. A refused request
// waits out that window, so a box on a busy bench still asks again that day.
constexpr std::uint64_t PROVISION_RETRY_MS = 60ULL * 60 * 1000;

/** Whether this boot may ask the dashboard for a key. */
inline bool mayRequestDeviceKey(bool refused, std::uint64_t retryAtMs,
                                std::uint64_t nowMs, bool clockTrusted) {
    if (refused) return false;
    if (retryAtMs == 0) return true;
    return clockTrusted && nowMs >= retryAtMs;
}

/** The v2 token text for an HMAC of DEVICE_TOKEN_LABEL. */
inline std::string deviceTokenText(const std::uint8_t (&digest)[HMAC_SHA256_BYTES]) {
    return base64UrlEncode(digest, sizeof(digest));
}

/** The device id for an HMAC of DEVICE_ID_LABEL: its first 16 bytes, lowercase hex. */
inline std::string deviceIdText(const std::uint8_t (&digest)[HMAC_SHA256_BYTES]) {
    static const char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(DEVICE_ID_TEXT_LENGTH);
    for (std::size_t index = 0; index < DEVICE_ID_BYTES; ++index) {
        text += digits[digest[index] >> 4];
        text += digits[digest[index] & 0x0f];
    }
    return text;
}

inline bool isDeviceIdText(const char* text) {
    if (!text || std::strlen(text) != DEVICE_ID_TEXT_LENGTH) return false;
    for (const char* cursor = text; *cursor; ++cursor) {
        const bool digit = *cursor >= '0' && *cursor <= '9';
        const bool lower = *cursor >= 'a' && *cursor <= 'f';
        if (!digit && !lower) return false;
    }
    return true;
}

/**
 * Whether a failed v2 sign-in may fall back to v1 for this boot: only when
 * the dashboard has no v2 at all (404) or does not know this key (401
 * `unknown_device`). Every other failure (an outage, a firewall answer, a
 * rate limit) is retried on v2, so it can never switch signing off.
 */
inline bool v2SignInFallsBackToV1(int status, JsonVariantConst body) {
    if (status == 404) return true;
    return status == 401 && body["code"].is<const char*>() &&
           std::strcmp(body["code"].as<const char*>(), "unknown_device") == 0;
}

/**
 * A v2 sign-in counts only when the dashboard names this chip's device id and
 * hands it a command key: a v2 box never runs without signed commands.
 */
inline bool v2SignInNamesDevice(JsonVariantConst body, const char* deviceId) {
    return isDeviceIdText(deviceId) && body["deviceId"].is<const char*>() &&
           std::strcmp(body["deviceId"].as<const char*>(), deviceId) == 0 &&
           body["commandKey"].is<const char*>();
}

}  // namespace lockbox::security
