#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "LockboxDuration.h"

namespace lockbox::security {

constexpr size_t MAX_LEGACY_BLE_BYTES = 127;
constexpr uint64_t MIN_LEGACY_CURRENT_DATE_MS = 946684800000ULL;
constexpr uint64_t MAX_LEGACY_CURRENT_DATE_MS = 4102444799999ULL;

enum class LegacyOperation : uint8_t { Unlock = 0, Lock = 1, LockTimer = 2, WiFiSync = 9 };

struct SmartLockboxState {
    LegacyOperation operation = LegacyOperation::Lock;
    uint64_t lockEnd = 0;
    uint64_t currentDate = 0;
};

struct WiFiCredentials {
    std::string_view ssid;
    std::string_view password;
};

inline bool validLegacyBytes(std::string_view value) {
    if (value.empty() || value.size() > MAX_LEGACY_BLE_BYTES) return false;
    for (const unsigned char byte : value) {
        if (byte < 0x20 || byte == 0x7f) return false;
    }
    return true;
}

inline bool parseUnsignedDecimal(std::string_view value, uint64_t& result) {
    if (value.empty() || value.size() > 20) return false;
    uint64_t parsed = 0;
    for (const char byte : value) {
        if (byte < '0' || byte > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(byte - '0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    result = parsed;
    return true;
}

// Legacy wire format: operation;lockEndMs;currentDateMs. Failure never
// supplies a default unlock operation or partially updates the output.
inline bool parseSmartLockboxState(std::string_view value,
                                   SmartLockboxState& result) {
    if (!validLegacyBytes(value) || value.size() < 5 || value[1] != ';' ||
        value[0] < '0' || value[0] > '2') return false;
    const size_t delimiter = value.find(';', 2);
    if (delimiter == std::string_view::npos ||
        value.find(';', delimiter + 1) != std::string_view::npos) return false;

    SmartLockboxState parsed;
    parsed.operation = static_cast<LegacyOperation>(value[0] - '0');
    if (!parseUnsignedDecimal(value.substr(2, delimiter - 2), parsed.lockEnd) ||
        !parseUnsignedDecimal(value.substr(delimiter + 1), parsed.currentDate)) {
        return false;
    }
    // Reject implausible clock updates (2000 through 2099), and deadlines
    // outside the same supported duration as the local timer selector.
    if (parsed.currentDate < MIN_LEGACY_CURRENT_DATE_MS ||
        parsed.currentDate > MAX_LEGACY_CURRENT_DATE_MS) return false;
    if (parsed.lockEnd != 0 &&
        (parsed.lockEnd < MIN_LEGACY_CURRENT_DATE_MS ||
         (parsed.lockEnd > parsed.currentDate &&
          parsed.lockEnd - parsed.currentDate > MAX_TIMER_DURATION_MS))) return false;
    if (parsed.operation == LegacyOperation::LockTimer &&
        parsed.lockEnd <= parsed.currentDate) return false;

    result = parsed;
    return true;
}

// The first two separators delimit the operation and SSID. The remaining
// bytes are the password, including spaces and any additional semicolons.
inline bool parseWifiCredentials(std::string_view value, WiFiCredentials& result) {
    if (!validLegacyBytes(value) || value.size() < 4 || value.substr(0, 2) != "9;") {
        return false;
    }
    const size_t delimiter = value.find(';', 2);
    if (delimiter == std::string_view::npos) return false;
    const auto ssid = value.substr(2, delimiter - 2);
    const auto password = value.substr(delimiter + 1);
    if (ssid.empty() || ssid.size() > 32) return false;
    if (!password.empty() && (password.size() < 8 || password.size() > 64)) {
        return false;
    }
    if (password.size() == 64) {
        for (const char byte : password) {
            if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
                  (byte >= 'A' && byte <= 'F'))) return false;
        }
    }
    result = {ssid, password};
    return true;
}

inline bool validLegacyMessage(std::string_view value) {
    if (value.empty()) return false;
    if (value[0] == '9') {
        WiFiCredentials credentials;
        return parseWifiCredentials(value, credentials);
    }
    SmartLockboxState state;
    return parseSmartLockboxState(value, state);
}

inline bool legacyQueuePayload(const char* bytes, size_t capacity, size_t length,
                                std::string_view& result) {
    if (bytes == nullptr || length >= capacity || length > MAX_LEGACY_BLE_BYTES ||
        bytes[length] != '\0') return false;
    const std::string_view value(bytes, length);
    if (!validLegacyMessage(value)) return false;
    result = value;
    return true;
}

template <typename Seconds>
inline bool representableLegacyTime(uint64_t milliseconds) {
    return milliseconds / 1000 <=
           static_cast<uint64_t>(std::numeric_limits<Seconds>::max());
}

}  // namespace lockbox::security
