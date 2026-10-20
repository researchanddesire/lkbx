#ifndef TEST_INTEGRATION_HELPERS_H
#define TEST_INTEGRATION_HELPERS_H

#include <Arduino.h>
#include <Preferences.h>
#include <esp_event.h>
#include <esp_system.h>
#include <mqtt_client.h>

#include "constants/Colors.h"
#include "constants/Sizes.h"
#include "services/display.h"
#include "services/http_worker.h"
#include "utils/dashboard.hpp"
#include "utils/trustedClock.h"

// Declared in initMQTT.h, defined in board.cpp's translation unit.
extern void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                               int32_t event_id, void* event_data);

// Shared test state — defined in main.cpp, declared extern in test_utils.h.

// ─── Heap monitoring ─────────────────────────────────────────────────────
//
// mbedTLS needs ~40-48KB contiguous for a TLS handshake.
// 50KB threshold gives margin for concurrent allocations.

static constexpr uint32_t TLS_MIN_HEAP = 50000;

// Log heap stats to serial. Uses TEST_MESSAGE so output appears in the
// Unity test report regardless of debug level.
static void logHeap(const char* label) {
    char msg[192];
    snprintf(msg, sizeof(msg),
             "[heap:%s] free=%lu  min_ever=%lu  max_block=%lu",
             label,
             (unsigned long)ESP.getFreeHeap(),
             (unsigned long)ESP.getMinFreeHeap(),
             (unsigned long)ESP.getMaxAllocHeap());
    TEST_MESSAGE(msg);
}

// Assert that enough contiguous heap exists for a TLS handshake.
// Fails the test immediately with diagnostics instead of letting
// mbedTLS surface an opaque ALLOC_FAILED / "connection refused".
static void assertHeapForTLS(const char* label) {
    uint32_t maxBlock = ESP.getMaxAllocHeap();
    uint32_t freeHeap = ESP.getFreeHeap();

    char msg[256];
    snprintf(msg, sizeof(msg),
             "[heap:%s] free=%lu  max_block=%lu  (TLS needs >=%lu)",
             label,
             (unsigned long)freeHeap,
             (unsigned long)maxBlock,
             (unsigned long)TLS_MIN_HEAP);
    TEST_MESSAGE(msg);

    if (maxBlock < TLS_MIN_HEAP) {
        snprintf(msg, sizeof(msg),
                 "Insufficient heap for TLS: largest block %lu < %lu. "
                 "HTTPS will fail with MBEDTLS_ERR_SSL_ALLOC_FAILED.",
                 (unsigned long)maxBlock, (unsigned long)TLS_MIN_HEAP);
        TEST_FAIL_MESSAGE(msg);
    }
}

// ─── Crash detection ─────────────────────────────────────────────────────
//
// NVS key tracks whether a test run was in progress when the device reset.
// If the device reboots mid-test (crash loop), the next boot sees the flag
// still set AND a crash reset reason → hard fail.

static const char* TEST_NVS_NS   = "itest";
static const char* TEST_NVS_KEY  = "running";

static const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:  return "POWER_ON";
        case ESP_RST_SW:       return "SOFTWARE";
        case ESP_RST_PANIC:    return "PANIC (crash)";
        case ESP_RST_INT_WDT:  return "INTERRUPT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO:     return "SDIO";
        default:               return "UNKNOWN";
    }
}

static bool isCrashReset(esp_reset_reason_t r) {
    return r == ESP_RST_PANIC ||
           r == ESP_RST_INT_WDT ||
           r == ESP_RST_TASK_WDT ||
           r == ESP_RST_WDT ||
           r == ESP_RST_BROWNOUT;
}

// Call at the very start of setup(), before UNITY_BEGIN().
// Returns true if the device just came out of a crash — the caller
// should fail immediately.
static bool checkForCrashOnBoot() {
    esp_reset_reason_t reason = esp_reset_reason();

    Preferences p;
    p.begin(TEST_NVS_NS, false);
    bool wasRunning = p.getBool(TEST_NVS_KEY, false);

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Reset reason: %s (%d) | test-was-running flag: %d",
             resetReasonName(reason), (int)reason, wasRunning);
    Serial.println(msg);

    if (isCrashReset(reason)) {
        p.putBool(TEST_NVS_KEY, false);
        p.end();
        snprintf(msg, sizeof(msg),
                 "FATAL: Device reset due to %s — refusing to run tests "
                 "(would crash-loop). Fix the crash first.",
                 resetReasonName(reason));
        Serial.println(msg);
        return true;
    }

    // ESP_RST_SW is normal during PIO test workflows — PIO toggles
    // DTR/RTS to reset the device after upload, which registers as a
    // software reset.  Only genuine crash reset reasons (panic, WDT,
    // brownout) should block the run.
    if (wasRunning && reason == ESP_RST_SW) {
        Serial.println(
            "NOTE: Software reset while running flag was set — "
            "likely PIO auto-reset, not a crash. Continuing.");
    }

    p.putBool(TEST_NVS_KEY, true);
    p.end();
    return false;
}

// Call at the end of setup() (after UNITY_END) to clear the flag,
// indicating a clean completion.
static void markTestRunComplete() {
    Preferences p;
    p.begin(TEST_NVS_NS, false);
    p.putBool(TEST_NVS_KEY, false);
    p.end();
}

// Inline sanity check — call between test phases to verify the device
// hasn't silently rebooted. If it did, esp_reset_reason would reflect
// the crash and the NVS flag would still be set.
static void assertNotCrashed(const char* phase) {
    esp_reset_reason_t reason = esp_reset_reason();
    if (isCrashReset(reason)) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Device crashed (%s) during/before %s",
                 resetReasonName(reason), phase);
        TEST_FAIL_MESSAGE(msg);
    }
}

// Update the display status bar with the current test phase name.
static void showTestPhase(const char* name) {
    if (!displayMutex) return;
    if (xSemaphoreTake(displayMutex, portTICK_PERIOD_MS * 500) == pdTRUE) {
        display.fillRect(0, 0, Display::WIDTH, Display::StatusbarHeight,
                         Colors::bgGray900);
        display.setTextColor(ST77XX_WHITE);
        display.setTextSize(1);
        display.setCursor(Display::Padding::P2,
                          (Display::StatusbarHeight - 8) / 2);
        display.printf("TEST: %s", name);
        xSemaphoreGive(displayMutex);
    }
}

// Show pass/fail result for a completed test phase, then pause so it's readable.
static void showTestResult(const char* name, bool passed) {
    if (!displayMutex) return;
    if (xSemaphoreTake(displayMutex, portTICK_PERIOD_MS * 500) == pdTRUE) {
        display.fillRect(0, 0, Display::WIDTH, Display::StatusbarHeight,
                         Colors::bgGray900);
        display.setTextSize(1);

        uint16_t color = passed ? 0x07E0 : 0xF800;  // green : red
        const char* icon = passed ? "OK" : "FAIL";
        display.setTextColor(color);
        display.setCursor(Display::Padding::P2,
                          (Display::StatusbarHeight - 8) / 2);
        display.printf("%s  %s", icon, name);
        xSemaphoreGive(displayMutex);
    }
    delay(1000);
}

// Block until a condition lambda returns true, or timeout expires.
// Returns true if the condition was met, false on timeout.
template <typename Fn>
static bool waitFor(Fn condition, uint32_t timeoutMs,
                    uint32_t pollMs = 100) {
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        if (condition()) return true;
        vTaskDelay(pollMs / portTICK_PERIOD_MS);
    }
    return condition();
}

// Print a prompt over serial and wait for the user to type a line.
// Useful for interactive tests (e.g. "Press ENTER button now...").
static void serialPrompt(const char *message) {
    Serial.println();
    Serial.println("=============================================");
    Serial.println(message);
    Serial.println("=============================================");
    Serial.println();
}

// Synthesize an MQTT_EVENT_DATA and feed it straight into the production
// handler, bypassing the network.  This exercises the full
// parseJsonMessage -> process_event dispatch path in initMQTT.h.
static void simulateMqttData(const String &jsonPayload) {
    String topic = getMacAddresss();
    esp_mqtt_event_t fakeEvent = {};
    fakeEvent.event_id = MQTT_EVENT_DATA;
    fakeEvent.data = const_cast<char *>(jsonPayload.c_str());
    fakeEvent.data_len = jsonPayload.length();
    fakeEvent.total_data_len = jsonPayload.length();
    fakeEvent.current_data_offset = 0;
    fakeEvent.topic = const_cast<char *>(topic.c_str());
    fakeEvent.topic_len = topic.length();
    mqtt_event_handler(nullptr, nullptr, MQTT_EVENT_DATA, &fakeEvent);
}

// Return the current epoch in milliseconds (same calculation the firmware uses).
static uint64_t nowMs() {
    struct timeval tv {};
    lockboxGetTimeOfDay(&tv, nullptr);
    return ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;
}

// ─── Test pairing helpers ────────────────────────────────────────────────
//
// Pair this generated device in the disposable local fixture.
// This ensures getAuthToken() returns isPaired=true, preventing the restart
// loop that occurs when NVS says isPaired=true but server says isPaired=false.
//
// Must be called AFTER WiFi is connected.
// This uses the per-device local credential added by the HTTP worker.
//
// Idempotent: returns 200 whether device is newly paired or already paired.

static int triggerPairing() {
    String url = String(RAD_SERVER) + "/api/lockbox/test-pair";
    String mac = getMacAddresss();

    JsonDocument doc;
    doc["macAddress"] = mac;

    String responsePayload;
    int code = post(url, doc, &responsePayload, 10000);

    // 200 = newly paired
    // 409 = already paired (idempotent success)
    // Normalize both to 200 for caller simplicity
    if (code == 409) {
        TEST_MESSAGE("Device already paired in server (409 -> 200)");
        return 200;
    }

    return code;
}

#endif  // TEST_INTEGRATION_HELPERS_H
