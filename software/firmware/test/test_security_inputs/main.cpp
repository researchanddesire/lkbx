#include "unity.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

#include "LegacyBleInput.h"
#include "LockboxDuration.h"
#include "../../src/utils/RecusiveMutex.h"

using namespace lockbox::security;

void setUp() {}
void tearDown() { freertos_mock::failAllocation = false; }

static std::string command(unsigned int operation, uint64_t end, uint64_t now) {
    return std::to_string(operation) + ";" + std::to_string(end) + ";" + std::to_string(now);
}

void test_valid_legacy_lock_commands() {
    SmartLockboxState state;
    for (unsigned int op = 0; op <= 2; ++op) {
        TEST_ASSERT_TRUE(parseSmartLockboxState(command(op, 1800000060000ULL, 1800000000000ULL), state));
        TEST_ASSERT_EQUAL_UINT8(op, static_cast<uint8_t>(state.operation));
        TEST_ASSERT_EQUAL_UINT64(1800000060000ULL, state.lockEnd);
        TEST_ASSERT_EQUAL_UINT64(1800000000000ULL, state.currentDate);
    }
    TEST_ASSERT_TRUE(parseSmartLockboxState("0;0;1800000000000", state));
    TEST_ASSERT_TRUE(parseSmartLockboxState("1;0;1800000000000", state));
    TEST_ASSERT_TRUE(parseSmartLockboxState("0;1700000000000;1800000000000", state));
}

void test_malformed_legacy_input_never_becomes_unlock() {
    const char* invalid[] = {
        "", "0", "0;", ";0;1800000000000", "0;0", "0;;1800000000000",
        "0;0;", "3;0;1800000000000", "9;0;1800000000000", "00;0;1800000000000",
        "-1;0;1800000000000", "x;0;1800000000000", "0;0;1800000000000;",
        "0;+1;1800000000000", "0;-1;1800000000000", "0;1x;1800000000000",
        "0;1.0;1800000000000", "0;1e3;1800000000000", "0;0; 1800000000000",
        "0;0;1800000000000\n", "0;18446744073709551616;1800000000000",
        "0;0;18446744073709551616", "0;0;000000000000000000000001800000000000"
    };
    for (const char* value : invalid) {
        SmartLockboxState state{LegacyOperation::LockTimer, 42, 43};
        TEST_ASSERT_FALSE_MESSAGE(parseSmartLockboxState(value, state), value);
        TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(state.operation));
        TEST_ASSERT_EQUAL_UINT64(42, state.lockEnd);
        TEST_ASSERT_EQUAL_UINT64(43, state.currentDate);
    }
}

void test_unsigned_decimal_checked_boundaries() {
    uint64_t result = 42;
    TEST_ASSERT_TRUE(parseUnsignedDecimal("18446744073709551615", result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
    TEST_ASSERT_FALSE(parseUnsignedDecimal("18446744073709551616", result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
    TEST_ASSERT_FALSE(parseUnsignedDecimal("-1", result));
    TEST_ASSERT_FALSE(parseUnsignedDecimal("+1", result));
    TEST_ASSERT_TRUE(parseUnsignedDecimal("0001", result));
    TEST_ASSERT_EQUAL_UINT64(1, result);
}

void test_plausible_clock_and_timed_lock_boundaries() {
    SmartLockboxState state;
    TEST_ASSERT_FALSE(parseSmartLockboxState("0;0;0", state));
    TEST_ASSERT_FALSE(parseSmartLockboxState(command(0, 0, MIN_LEGACY_CURRENT_DATE_MS - 1), state));
    TEST_ASSERT_TRUE(parseSmartLockboxState(command(0, 0, MIN_LEGACY_CURRENT_DATE_MS), state));
    TEST_ASSERT_TRUE(parseSmartLockboxState(command(0, 0, MAX_LEGACY_CURRENT_DATE_MS), state));
    TEST_ASSERT_FALSE(parseSmartLockboxState(command(0, 0, MAX_LEGACY_CURRENT_DATE_MS + 1), state));
    TEST_ASSERT_FALSE(parseSmartLockboxState("2;0;1800000000000", state));
    TEST_ASSERT_FALSE(parseSmartLockboxState("2;1800000000000;1800000000000", state));
    TEST_ASSERT_FALSE(parseSmartLockboxState("2;1799999999999;1800000000000", state));
    const uint64_t now = 1800000000000ULL;
    TEST_ASSERT_TRUE(parseSmartLockboxState(command(2, now + MAX_TIMER_DURATION_MS, now), state));
    TEST_ASSERT_FALSE(parseSmartLockboxState(command(2, now + MAX_TIMER_DURATION_MS + 1, now), state));
    TEST_ASSERT_FALSE(parseSmartLockboxState(command(2, UINT64_MAX, now), state));
    TEST_ASSERT_TRUE(representableLegacyTime<int32_t>(2147483647999ULL));
    TEST_ASSERT_FALSE(representableLegacyTime<int32_t>(2147483648000ULL));
    TEST_ASSERT_TRUE(representableLegacyTime<int64_t>(MAX_LEGACY_CURRENT_DATE_MS));
}

void test_wifi_values_are_preserved_exactly() {
    WiFiCredentials parsed;
    const std::string value = "9; test network ; " + std::string(8, 'a') + "; ";
    TEST_ASSERT_TRUE(parseWifiCredentials(value, parsed));
    TEST_ASSERT_EQUAL_STRING_LEN(" test network ", parsed.ssid.data(), parsed.ssid.size());
    const std::string expected = " " + std::string(8, 'a') + "; ";
    TEST_ASSERT_EQUAL_UINT(expected.size(), parsed.password.size());
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), parsed.password.data(), expected.size());
    TEST_ASSERT_TRUE(parseWifiCredentials("9;open-network;", parsed));
    TEST_ASSERT_TRUE(parsed.password.empty());
}

void test_wifi_length_and_format_boundaries() {
    WiFiCredentials parsed;
    const std::string ssid(32, 's');
    for (const size_t length : {8U, 63U, 64U}) {
        const std::string value = "9;" + ssid + ";" + std::string(length, 'a');
        TEST_ASSERT_TRUE(parseWifiCredentials(value, parsed));
        TEST_ASSERT_EQUAL_UINT(32, parsed.ssid.size());
        TEST_ASSERT_EQUAL_UINT(length, parsed.password.size());
    }
    const std::string invalid[] = {
        "9", "9;", "9;;", "9;ssid", "99;ssid;" + std::string(8, 'a'),
        "0;ssid;" + std::string(8, 'a'), "9;" + std::string(33, 's') + ";",
        "9;ssid;" + std::string(7, 'a'), "9;ssid;" + std::string(65, 'a'),
        "9;ssid;" + std::string(64, 'z'), "9;ssid;\t" + std::string(8, 'a')
    };
    for (const auto& value : invalid) {
        parsed = {"unchanged", "unchanged"};
        TEST_ASSERT_FALSE(parseWifiCredentials(value, parsed));
        TEST_ASSERT_TRUE(parsed.ssid == "unchanged");
        TEST_ASSERT_TRUE(parsed.password == "unchanged");
    }
}

void test_queue_and_embedded_nul_rejected_without_truncation() {
    char buffer[128]{};
    const std::string valid = "0;0;1800000000000";
    memcpy(buffer, valid.c_str(), valid.size() + 1);
    std::string_view parsed;
    TEST_ASSERT_TRUE(legacyQueuePayload(buffer, sizeof(buffer), valid.size(), parsed));
    TEST_ASSERT_FALSE(legacyQueuePayload(buffer, sizeof(buffer), 128, parsed));
    TEST_ASSERT_FALSE(legacyQueuePayload(buffer, sizeof(buffer), SIZE_MAX, parsed));
    TEST_ASSERT_FALSE(legacyQueuePayload(nullptr, 128, valid.size(), parsed));
    TEST_ASSERT_FALSE(legacyQueuePayload(buffer, 0, 0, parsed));
    TEST_ASSERT_FALSE(legacyQueuePayload(buffer, sizeof(buffer), valid.size() - 1, parsed));
    buffer[valid.size()] = 'x';
    TEST_ASSERT_FALSE(legacyQueuePayload(buffer, sizeof(buffer), valid.size(), parsed));
    buffer[valid.size()] = '\0';
    buffer[3] = '\0';
    TEST_ASSERT_FALSE(legacyQueuePayload(buffer, sizeof(buffer), valid.size(), parsed));
    TEST_ASSERT_FALSE(validLegacyMessage(std::string(128, 'x')));
    std::string embedded = valid + '\0' + "ignored";
    TEST_ASSERT_FALSE(validLegacyMessage(embedded));
    embedded = "9;ssid;" + std::string(8, 'a') + '\0' + "ignored";
    TEST_ASSERT_FALSE(validLegacyMessage(embedded));
    strcpy(buffer, "0");
    TEST_ASSERT_FALSE(legacyQueuePayload(buffer, sizeof(buffer), 1, parsed));
}

void test_long_duration_choices_do_not_overflow() {
    uint64_t duration = 0;
    const uint64_t days[] = {24, 25, 29};
    const uint64_t dayMs[] = {2073600000ULL, 2160000000ULL, 2505600000ULL};
    for (size_t i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(durationMilliseconds(days[i], DurationUnit::Days, duration));
        TEST_ASSERT_EQUAL_UINT64(dayMs[i], duration);
    }
    TEST_ASSERT_TRUE(durationMilliseconds(4, DurationUnit::Weeks, duration));
    TEST_ASSERT_EQUAL_UINT64(2419200000ULL, duration);
    TEST_ASSERT_TRUE(durationMilliseconds(7, DurationUnit::Weeks, duration));
    TEST_ASSERT_EQUAL_UINT64(4233600000ULL, duration);
    TEST_ASSERT_TRUE(durationMilliseconds(1000, DurationUnit::Years, duration));
    TEST_ASSERT_EQUAL_UINT64(31556952000000ULL, duration);
    TEST_ASSERT_TRUE(durationMilliseconds(11, DurationUnit::Months, duration));
    TEST_ASSERT_EQUAL_UINT64(28927206000ULL, duration);
    TEST_ASSERT_TRUE(durationMilliseconds(119, DurationUnit::Seconds, duration));
    TEST_ASSERT_EQUAL_UINT64(119000, duration);
    TEST_ASSERT_TRUE(durationMilliseconds(119, DurationUnit::Minutes, duration));
    TEST_ASSERT_EQUAL_UINT64(7140000, duration);
    TEST_ASSERT_TRUE(durationMilliseconds(23, DurationUnit::Hours, duration));
    TEST_ASSERT_EQUAL_UINT64(82800000, duration);
    TEST_ASSERT_FALSE(durationMilliseconds(1001, DurationUnit::Years, duration));
    TEST_ASSERT_FALSE(durationMilliseconds(UINT64_MAX, DurationUnit::Days, duration));
    TEST_ASSERT_FALSE(durationMilliseconds(1, static_cast<DurationUnit>(99), duration));
    TEST_ASSERT_EQUAL_UINT64(82800000, duration);
}

void test_deadline_addition_remains_safe_before_clock_sync() {
    uint64_t deadline = 0;
    TEST_ASSERT_TRUE(localTimerDeadline(0, 600000, deadline));
    TEST_ASSERT_EQUAL_UINT64(600000, deadline);
    TEST_ASSERT_TRUE(localTimerDeadline(1800000000000LL, MAX_TIMER_DURATION_MS, deadline));
    TEST_ASSERT_EQUAL_UINT64(33356952000000ULL, deadline);
    const int64_t latest = std::numeric_limits<int64_t>::max();
    TEST_ASSERT_TRUE(localTimerDeadline(latest - 1, 1, deadline));
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(latest), deadline);
    TEST_ASSERT_FALSE(localTimerDeadline(latest, 1, deadline));
    TEST_ASSERT_FALSE(localTimerDeadline(-1, 600000, deadline));
    TEST_ASSERT_FALSE(localTimerDeadline(0, 0, deadline));
    TEST_ASSERT_FALSE(localTimerDeadline(0, MAX_TIMER_DURATION_MS + 1, deadline));
    TEST_ASSERT_FALSE(localTimerDeadline(0, UINT64_MAX, deadline));
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(latest), deadline);
}

void test_mutex_retains_exclusion_after_one_second_and_recursion() {
    ESP32RecursiveMutex mutex;
    mutex.lock();
    mutex.lock();
    std::atomic<bool> entered{false};
    std::atomic<bool> returnedWithoutOwnership{false};
    const auto attemptsBefore = freertos_mock::takeAttempts.load();
    std::thread contender([&] {
        mutex.lock();
        entered = true;
        try { mutex.unlock(); }
        catch (const freertos_mock::AssertionFailure&) { returnedWithoutOwnership = true; }
    });
    while (freertos_mock::takeAttempts.load() == attemptsBefore) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    const bool enteredBeforeRelease = entered.load();
    mutex.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool enteredAfterPartialRelease = entered.load();
    mutex.unlock();
    contender.join();
    TEST_ASSERT_FALSE(enteredBeforeRelease);
    TEST_ASSERT_FALSE(enteredAfterPartialRelease);
    TEST_ASSERT_TRUE(entered.load());
    TEST_ASSERT_FALSE(returnedWithoutOwnership.load());
}

void test_mutex_try_lock_and_allocation_failure() {
    ESP32RecursiveMutex mutex;
    TEST_ASSERT_TRUE(mutex.try_lock());
    TEST_ASSERT_TRUE(mutex.try_lock());
    std::atomic<bool> acquired{false};
    std::thread contender([&] {
        acquired = mutex.try_lock();
        if (acquired) mutex.unlock();
    });
    contender.join();
    mutex.unlock();
    mutex.unlock();
    TEST_ASSERT_FALSE(acquired.load());
    freertos_mock::failAllocation = true;
    bool asserted = false;
    try { ESP32RecursiveMutex unavailable; }
    catch (const freertos_mock::AssertionFailure&) { asserted = true; }
    TEST_ASSERT_TRUE(asserted);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_legacy_lock_commands);
    RUN_TEST(test_malformed_legacy_input_never_becomes_unlock);
    RUN_TEST(test_unsigned_decimal_checked_boundaries);
    RUN_TEST(test_plausible_clock_and_timed_lock_boundaries);
    RUN_TEST(test_wifi_values_are_preserved_exactly);
    RUN_TEST(test_wifi_length_and_format_boundaries);
    RUN_TEST(test_queue_and_embedded_nul_rejected_without_truncation);
    RUN_TEST(test_long_duration_choices_do_not_overflow);
    RUN_TEST(test_deadline_addition_remains_safe_before_clock_sync);
    RUN_TEST(test_mutex_retains_exclusion_after_one_second_and_recursion);
    RUN_TEST(test_mutex_try_lock_and_allocation_failure);
    return UNITY_END();
}
