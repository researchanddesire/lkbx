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
    // Anything else (an older dashboard without v2, a network failure): try
    // again on a later boot, signing in with v1 meanwhile.
    nextBoot,
};

inline ProvisionAnswer provisionAnswer(int status) {
    if (status == 201) return ProvisionAnswer::burn;
    if (status == 409 || status == 403) return ProvisionAnswer::neverAgain;
    if (status == 429) return ProvisionAnswer::retryLater;
    return ProvisionAnswer::nextBoot;
}

// A key request costs one of the dashboard's per-address allowance, which is
// one a day by default, so a refused request waits the whole window.
constexpr std::uint64_t PROVISION_RETRY_MS = 24ULL * 60 * 60 * 1000;

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

}  // namespace lockbox::security
