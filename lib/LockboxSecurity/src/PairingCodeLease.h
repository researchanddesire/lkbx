#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace lockbox::security {

// Auth responses use ISO UTC, independent of the device's timezone. Accept
// second precision and up to six fractional digits; reject offsets/naive dates.
inline bool pairingExpiryMs(std::string_view text, uint64_t& output) {
    if (text.size() < 20 || text.size() > 27 || text[4] != '-' ||
        text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text.back() != 'Z') return false;
    auto number = [&](size_t start, size_t count, int& value) {
        value = 0;
        for (size_t index = start; index < start + count; ++index) {
            if (text[index] < '0' || text[index] > '9') return false;
            value = value * 10 + text[index] - '0';
        }
        return true;
    };
    int year, month, day, hour, minute, second;
    if (!number(0, 4, year) || !number(5, 2, month) || !number(8, 2, day) ||
        !number(11, 2, hour) || !number(14, 2, minute) || !number(17, 2, second) ||
        year < 2024 || year > 2099 || month < 1 || month > 12 || hour > 23 ||
        minute > 59 || second > 59) return false;
    const auto leap = [](int value) {
        return value % 4 == 0 && (value % 100 != 0 || value % 400 == 0);
    };
    constexpr int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day < 1 || day > lengths[month - 1] + (month == 2 && leap(year))) return false;
    int fractionalMs = 0;
    if (text.size() != 20) {
        if (text[19] != '.' || text.size() < 22) return false;
        for (size_t index = 20; index + 1 < text.size(); ++index) {
            if (text[index] < '0' || text[index] > '9') return false;
            if (index < 23) fractionalMs = fractionalMs * 10 + text[index] - '0';
        }
        for (size_t index = text.size() - 21; index < 3; ++index) fractionalMs *= 10;
    }
    uint64_t days = 0;
    for (int value = 1970; value < year; ++value) days += leap(value) ? 366 : 365;
    for (int value = 1; value < month; ++value) days += lengths[value - 1] + (value == 2 && leap(year));
    days += day - 1;
    output = (((days * 24 + hour) * 60 + minute) * 60 + second) * 1000 + fractionalMs;
    return true;
}

class PairingCodeLease {
 public:
    static constexpr uint64_t maximumAgeMs = 5 * 60 * 1000;
    // Clear before expiry so bounded HTTP waits cannot leave an expired QR
    // visible. Each network wait is <=3s; the prompt loop waits <=1s.
    static constexpr uint64_t displayMarginMs = 10 * 1000;
    static constexpr uint64_t retryIntervalMs = 5 * 1000;

    bool accept(std::string_view code, std::string_view expiry, uint64_t wallMs,
                uint64_t monotonicMs, bool trustedClock) {
        if (code.size() != 6 || !std::all_of(code.begin(), code.end(), [](char ch) {
                return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
            })) return false;
        uint64_t parsed = 0;
        if (!expiry.empty() && (!pairingExpiryMs(expiry, parsed) ||
                                !trustedClock || parsed <= wallMs)) return false;
        code_ = code;
        expiresAtMs_ = parsed;
        receivedAtMs_ = monotonicMs;
        maximumLifetimeMs_ = parsed ? std::min(maximumAgeMs, parsed - wallMs) : maximumAgeMs;
        return true;
    }

    std::string visible(uint64_t wallMs, uint64_t monotonicMs, bool trustedClock) const {
        return remaining(wallMs, monotonicMs, trustedClock) > displayMarginMs ? code_ : "";
    }

    bool refreshDue(uint64_t wallMs, uint64_t monotonicMs, bool trustedClock) const {
        return remaining(wallMs, monotonicMs, trustedClock) <= displayMarginMs &&
               (!attempted_ || monotonicMs < lastAttemptMs_ || monotonicMs - lastAttemptMs_ >= retryIntervalMs);
    }

    void attempted(uint64_t monotonicMs) { attempted_ = true; lastAttemptMs_ = monotonicMs; }

 private:
    uint64_t remaining(uint64_t wallMs, uint64_t monotonicMs, bool trustedClock) const {
        if (code_.empty() || monotonicMs < receivedAtMs_ || monotonicMs - receivedAtMs_ >= maximumLifetimeMs_) return 0;
        uint64_t value = maximumLifetimeMs_ - (monotonicMs - receivedAtMs_);
        if (expiresAtMs_) {
            if (!trustedClock || wallMs >= expiresAtMs_) return 0;
            value = std::min(value, expiresAtMs_ - wallMs);
        }
        return value;
    }
    std::string code_;
    uint64_t expiresAtMs_ = 0;
    uint64_t receivedAtMs_ = 0;
    uint64_t maximumLifetimeMs_ = 0;
    uint64_t lastAttemptMs_ = 0;
    bool attempted_ = false;
};
}  // namespace lockbox::security
