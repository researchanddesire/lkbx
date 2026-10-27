#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "PairingCodeLease.h"

namespace lockbox::security {

inline bool boundedString(JsonVariantConst value, std::size_t min,
                          std::size_t max) {
    if (!value.is<const char*>()) return false;
    const JsonString text = value.as<JsonString>();
    return text.size() >= min && text.size() <= max &&
           std::strlen(text.c_str()) == text.size();
}

inline bool authResponse(JsonVariantConst value) {
    uint64_t expiry = 0;
    if (!value["pairingCodeExpiresAt"].isNull() &&
        (!boundedString(value["pairingCodeExpiresAt"], 20, 27) ||
         !pairingExpiryMs(value["pairingCodeExpiresAt"].as<const char*>(), expiry))) return false;
    return value.is<JsonObjectConst>() &&
           boundedString(value["clientId"], 1, 128) &&
           boundedString(value["username"], 1, 128) &&
           boundedString(value["password"], 1, 512) &&
           boundedString(value["sessionId"], 1, 128) &&
           boundedString(value["pairingCode"], 1, 128) &&
           value["hueShift"].is<int>() &&
           value["hueShift"].as<int>() >= 0 &&
           value["hueShift"].as<int>() <= 360 &&
           value["isPaired"].is<bool>();
}

// PostgreSQL timestamps can arrive as integral decimal JSON numbers. Accept
// those without permitting fractions, strings, or values outside the exact
// integer range shared by the server's JSON number representation.
inline bool boundedNumber(JsonVariantConst value, bool required = false) {
    if (value.isNull()) return !required;
    constexpr uint64_t maximum = 9007199254740991ULL;
    if (value.is<uint64_t>()) return value.as<uint64_t>() <= maximum;
    if (!value.is<double>()) return false;
    const double number = value.as<double>();
    // ArduinoJson's decimal parser can round boundary tokens down near 2^53.
    // Keep decimal values well below that boundary; this still covers server
    // timestamps and all supported durations with ample room.
    constexpr double decimalMaximum = 4503599627370495.0;
    return std::isfinite(number) && number >= 0 && number <= decimalMaximum &&
           std::floor(number) == number;
}

inline bool sessionResponse(JsonVariantConst value, bool allowPending = false,
                            bool httpNullDates = false) {
    if (!value.is<JsonObjectConst>() ||
        !boundedString(value["sessionId"], 1, 128) ||
        !boundedString(value["lockState"], 1, 16)) return false;
    const char* state = value["lockState"];
    if (std::strcmp(state, "locked") && std::strcmp(state, "break") &&
        std::strcmp(state, "break_ending") &&
        (!allowPending || std::strcmp(state, "pending"))) return false;
    const bool pending = std::strcmp(state, "pending") == 0;
    // HTTP serializes a missing date as null: an offer has no dates yet and a
    // permanent lock has no end.
    if (!boundedNumber(value["lockStart"], !(pending && httpNullDates)) ||
        !boundedNumber(value["lockEnd"], !httpNullDates)) return false;
    if (!boundedNumber(value["duration"], true)) return false;
    for (const auto* key : {"breakMax", "breakReg", "breakLast"}) {
        if (!boundedNumber(value[key])) return false;
    }
    for (const auto* key : {"isBreak", "isEUnlock", "isTestLock", "isTimeDisp",
                            "displayElapsedTime", "breakKeyholderOnly"}) {
        if (!value[key].isNull() && !value[key].is<bool>()) return false;
    }
    return true;
}

enum class HttpSessionKind { invalid, active, pending };

inline HttpSessionKind httpSessionKind(JsonVariantConst value) {
    if (!sessionResponse(value, true, true)) return HttpSessionKind::invalid;
    return std::strcmp(value["lockState"].as<const char*>(), "pending") == 0
               ? HttpSessionKind::pending : HttpSessionKind::active;
}

inline bool commandResponse(JsonVariantConst value) {
    if (!value.is<JsonObjectConst>() ||
        !boundedString(value["type"], 1, 16)) return false;
    const char* type = value["type"];
    if (!std::strcmp(type, "reboot")) return true;
    if (!std::strcmp(type, "sync")) {
        if (!value["clear"].isNull() && !value["clear"].is<bool>()) return false;
        return value["clear"] == true || sessionResponse(value, true);
    }
    if (std::strcmp(type, "process") ||
        !boundedString(value["message"], 1, 32)) return false;
    const char* message = value["message"];
    if (!std::strcmp(message, "unlock") || !std::strcmp(message, "lock")) return true;
    return !std::strcmp(message, "lockTimer") &&
           boundedNumber(value["lockEnd"], true) &&
           value["lockEnd"].as<uint64_t>() != 0;
}

}  // namespace lockbox::security
