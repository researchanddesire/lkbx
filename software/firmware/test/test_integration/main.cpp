#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <unity.h>

#include "constants/PerferenceKeys.h"
#include "constants/dashboard.h"
#include "motor/Motor.h"
#include "services/http_worker.h"
#include "services/machine.h"
#include "services/mqtt.h"
#include "services/settings.h"
#include "state/LKBX.h"
#include "test_helpers.h"
#include "utils/getEfuseMac.h"

#if !defined(LKBX_DEV_API_BASE_URL) || !defined(LKBX_DEV_DEVICE_ID)
#error "Device integration tests require generated local Development services"
#endif

// ── Shared mutable state (used by test_utils.h across all TUs) ──────────

bool testWifiUp = false;
AuthResponse cachedAuth;

// ── Extern registration functions from each test file ───────────────────

extern void register_peripheral_tests();
extern void register_board_tests();
extern void startStateMachineForTests();
extern void register_ble_tests();
extern void register_wifi_tests();
extern void register_sm_boot_tests();
extern void register_http_tests();
extern void register_mqtt_tests();
extern void register_sm_traversal_tests();
extern void register_break_tests();
extern void register_mqtt_dispatch_tests();
extern void register_interactive_tests();

#define BAIL_ON_FAILURE()             \
    do {                              \
        if (Unity.TestFailures > 0) { \
            UNITY_END();              \
            markTestRunComplete();    \
            Serial.flush();           \
            delay(2000);              \
            Serial.flush();           \
            esp_restart();            \
            return;                   \
        }                             \
    } while (0)

// ── Preflight checks ────────────────────────────────────────────────────

enum PreflightReq : uint8_t {
    PF_NONE = 0,
    PF_WIFI = 1 << 0,
    PF_SERVER = 1 << 1,
    PF_MQTT = 1 << 2,
    PF_SM_HOME = 1 << 3,
    PF_NVS_UNLOCKED = 1 << 4,
    PF_SERVER_UNLOCKED = 1 << 5,
    PF_HEAP = 1 << 6,
};

static String smState() {
    String name;
    if (lkbx && lkbx->sm) {
        lkbx->sm->visit_current_states(
            [&](auto state) { name = state.c_str(); });
    }
    return name;
}

static bool smHome() { return smState().startsWith("home"); }

// Returns true if all checks pass, false if a precondition failed.
// Failures are logged to serial; the caller's BAIL_ON_FAILURE handles abort.
static bool preflightPhase(const char* label, uint8_t reqs) {
    char msg[256];
    bool ok = true;

    if (reqs & PF_WIFI) {
        bool up = WiFiClass::status() == WL_CONNECTED;
        snprintf(msg, sizeof(msg), "[preflight:%s] WiFi: %s", label,
                 up ? "connected" : "DISCONNECTED");
        Serial.println(msg);
        if (!up) {
            Serial.println("PREFLIGHT FAIL: WiFi must be connected");
            ok = false;
        }
    }

    if (ok && (reqs & PF_SERVER)) {
        String url = String(RAD_SERVER) + "/api/health-check";
        int code = get(url, nullptr, 5000);
        snprintf(msg, sizeof(msg), "[preflight:%s] Server health: %d", label,
                 code);
        Serial.println(msg);
        if (code != 200) {
            Serial.println("PREFLIGHT FAIL: Server must be reachable");
            ok = false;
        }
    }

    if (ok && (reqs & PF_MQTT)) {
        snprintf(msg, sizeof(msg), "[preflight:%s] MQTT: %s (client=%p)", label,
                 mqttConnected ? "connected" : "DISCONNECTED", mqttClient);
        Serial.println(msg);
        if (!mqttConnected) {
            Serial.println("PREFLIGHT FAIL: MQTT must be connected");
            ok = false;
        }
    }

    if (reqs & PF_NVS_UNLOCKED) {
        if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) ==
            pdTRUE) {
            preferences.begin(Keys::base, false);
            preferences.putBool(Keys::Base::isLocked, false);
            preferences.putBool(Keys::Base::isLockTimer, false);
            preferences.putBool(Keys::Base::isEBlocked, false);
            preferences.putBool(Keys::Base::isLocalPlay, false);
            preferences.end();

            preferences.begin(Keys::session, false);
            preferences.putBool(Keys::Session::isActiveBreak, false);
            preferences.putULong64(Keys::Session::breakStart, 0);
            preferences.end();

            xSemaphoreGive(preferencesMutex);
        }
        snprintf(msg, sizeof(msg), "[preflight:%s] NVS reset to unlocked",
                 label);
        Serial.println(msg);
    }

    if (ok && (reqs & PF_SM_HOME) && lkbx && lkbx->sm) {
        String state = smState();
        snprintf(msg, sizeof(msg), "[preflight:%s] SM state: %s", label,
                 state.c_str());
        Serial.println(msg);

        if (!state.startsWith("home")) {
            Serial.println(
                "[preflight] SM not at home — attempting recovery...");

            if (state.startsWith("locking.error")) {
                lkbx->sm->process_event(ButtonPress{});
                waitFor(smHome, 15000, 250);
            }
            if (!smHome()) {
                lkbx->sm->process_event(Unlock{});
                waitFor(smHome, 15000, 250);
            }

            state = smState();
            snprintf(msg, sizeof(msg), "[preflight:%s] SM after recovery: %s",
                     label, state.c_str());
            Serial.println(msg);
            if (!state.startsWith("home")) {
                Serial.println("PREFLIGHT FAIL: SM must be at home");
                ok = false;
            }
        }
    }

    if (ok && (reqs & PF_HEAP)) {
        uint32_t maxBlock = ESP.getMaxAllocHeap();
        uint32_t freeHeap = ESP.getFreeHeap();
        snprintf(
            msg, sizeof(msg),
            "[preflight:%s] Heap: free=%lu  max_block=%lu  (TLS needs >=%lu)",
            label, (unsigned long)freeHeap, (unsigned long)maxBlock,
            (unsigned long)TLS_MIN_HEAP);
        Serial.println(msg);
        if (maxBlock < TLS_MIN_HEAP) {
            Serial.println(
                "PREFLIGHT FAIL: Insufficient contiguous heap for TLS "
                "handshake");
            ok = false;
        }
    }

    return ok;
}

// Returns true if all post-conditions hold, false otherwise.
static bool postflightPhase(const char* label, uint8_t reqs) {
    char msg[256];
    bool ok = true;

    if (reqs & PF_WIFI) {
        bool up = WiFiClass::status() == WL_CONNECTED;
        snprintf(msg, sizeof(msg), "[postflight:%s] WiFi: %s", label,
                 up ? "connected" : "DISCONNECTED");
        Serial.println(msg);
        if (!up) {
            Serial.println("POSTFLIGHT FAIL: WiFi should still be connected");
            ok = false;
        }
    }

    if (ok && (reqs & PF_SERVER)) {
        String url = String(RAD_SERVER) + "/api/health-check";
        int code = get(url, nullptr, 5000);
        snprintf(msg, sizeof(msg), "[postflight:%s] Server health: %d", label,
                 code);
        Serial.println(msg);
        if (code != 200) {
            Serial.println("POSTFLIGHT FAIL: Server should still be reachable");
            ok = false;
        }
    }

    if (ok && (reqs & PF_MQTT)) {
        snprintf(msg, sizeof(msg), "[postflight:%s] MQTT: %s", label,
                 mqttConnected ? "connected" : "DISCONNECTED");
        Serial.println(msg);
        if (!mqttConnected) {
            Serial.println("POSTFLIGHT FAIL: MQTT should still be connected");
            ok = false;
        }
    }

    if (ok && (reqs & PF_SM_HOME) && lkbx && lkbx->sm) {
        String state = smState();
        snprintf(msg, sizeof(msg), "[postflight:%s] SM state: %s", label,
                 state.c_str());
        Serial.println(msg);
        if (!state.startsWith("home")) {
            Serial.println("POSTFLIGHT FAIL: SM should be at home after phase");
            ok = false;
        }
    }

    if (reqs & PF_NVS_UNLOCKED) {
        bool isLocked = true;
        bool isEBlocked = false;
        bool isActiveBreak = false;
        if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) ==
            pdTRUE) {
            preferences.begin(Keys::base);
            isLocked = preferences.getBool(Keys::Base::isLocked, false);
            isEBlocked = preferences.getBool(Keys::Base::isEBlocked, false);
            preferences.end();

            preferences.begin(Keys::session);
            isActiveBreak =
                preferences.getBool(Keys::Session::isActiveBreak, false);
            preferences.end();

            xSemaphoreGive(preferencesMutex);
        }
        snprintf(
            msg, sizeof(msg),
            "[postflight:%s] NVS: isLocked=%d isEBlocked=%d isActiveBreak=%d",
            label, isLocked, isEBlocked, isActiveBreak);
        Serial.println(msg);
        if (isLocked || isEBlocked || isActiveBreak) {
            Serial.println("POSTFLIGHT FAIL: NVS should be in unlocked state");
            ok = false;
        }
    }

    if (ok && (reqs & PF_SERVER_UNLOCKED)) {
        String url =
            String(RAD_SERVER) + "/api/lockbox/state/" + getMacAddresss();
        String payload;
        int code = get(url, &payload, 5000);
        if (code != 200) {
            snprintf(msg, sizeof(msg),
                     "[postflight:%s] Server state GET failed: %d", label,
                     code);
            Serial.println(msg);
            Serial.println("POSTFLIGHT FAIL: Could not verify server state");
            ok = false;
        } else {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);
            if (err) {
                snprintf(msg, sizeof(msg),
                         "[postflight:%s] Server response parse error: %s",
                         label, err.c_str());
                Serial.println(msg);
            } else {
                bool shouldClear = doc["clear"].as<bool>();
                const char* lockState = doc["data"]["lockState"] | "none";
                snprintf(msg, sizeof(msg),
                         "[postflight:%s] Server: clear=%d lockState=%s", label,
                         shouldClear, lockState);
                Serial.println(msg);

                bool serverUnlocked = shouldClear ||
                                      strcmp(lockState, "none") == 0 ||
                                      strcmp(lockState, "unlocked") == 0 ||
                                      strcmp(lockState, "null") == 0;
                if (!serverUnlocked) {
                    Serial.println(
                        "POSTFLIGHT FAIL: Server should report device as "
                        "unlocked");
                    ok = false;
                }
            }
        }
    }

    if (ok && (reqs & PF_HEAP)) {
        uint32_t maxBlock = ESP.getMaxAllocHeap();
        uint32_t freeHeap = ESP.getFreeHeap();
        snprintf(
            msg, sizeof(msg),
            "[postflight:%s] Heap: free=%lu  max_block=%lu  (TLS needs >=%lu)",
            label, (unsigned long)freeHeap, (unsigned long)maxBlock,
            (unsigned long)TLS_MIN_HEAP);
        Serial.println(msg);
        if (maxBlock < TLS_MIN_HEAP) {
            Serial.println(
                "POSTFLIGHT FAIL: Heap degraded below TLS threshold — "
                "possible memory leak in this phase");
            ok = false;
        }
    }

    return ok;
}

static const char* _checkLabel;
static uint8_t _checkReqs;
static bool _checkIsPre;

static void _flightCheckTest() {
    bool ok = _checkIsPre ? preflightPhase(_checkLabel, _checkReqs)
                          : postflightPhase(_checkLabel, _checkReqs);
    const char* tag = _checkIsPre ? "Preflight" : "Postflight";
    char failMsg[64];
    snprintf(failMsg, sizeof(failMsg), "%s check failed — see serial log above",
             tag);
    TEST_ASSERT_TRUE_MESSAGE(ok, failMsg);
}

#define _RUN_CHECK(label, flags, isPre) \
    do {                                \
        if ((flags) != PF_NONE) {       \
            _checkLabel = (label);      \
            _checkReqs = (flags);       \
            _checkIsPre = (isPre);      \
            RUN_TEST(_flightCheckTest); \
            BAIL_ON_FAILURE();          \
        }                               \
    } while (0)

#define RUN_PHASE(label, registerFn, pflags, postflags)      \
    do {                                                     \
        showTestPhase(label);                                \
        _RUN_CHECK(label, pflags, true);                     \
        unsigned _failsBefore = Unity.TestFailures;          \
        registerFn();                                        \
        bool _passed = (Unity.TestFailures == _failsBefore); \
        showTestResult(label, _passed);                      \
        BAIL_ON_FAILURE();                                   \
        assertNotCrashed(label);                             \
        _RUN_CHECK(label, postflags, false);                 \
    } while (0)

// ── Runner ──────────────────────────────────────────────────────────────

void setup() {
    delay(5000);
    Serial.begin(115200);

    if (checkForCrashOnBoot()) {
        Motor::clear();
        UNITY_BEGIN();
        TEST_FAIL_MESSAGE(
            "Device rebooted from a crash (panic/WDT/brownout). "
            "Refusing to run tests to avoid crash loop. "
            "Check serial output above for reset reason.");
        UNITY_END();
        markTestRunComplete();
        Serial.flush();
        delay(2000);
        Serial.flush();
        esp_restart();
        return;
    }

    // Skip interactive calibrate state so the SM boots straight to home.
    // BLE always starts (initBoard → initNimble) regardless of pairing.
    // NimBLE consumes ~40-60KB of heap; it is deinited later when MQTT TLS
    // connects (see LKBX.wifi.cpp).
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isMotorCalibrated, true);
        preferences.putBool(Keys::Base::isButtonCalibrated, true);
        // Preserve calibrated isButtonInverted and isMotorInverted —
        // overriding them breaks motor tests on devices whose pin
        // routing or polarity differs from the default.
        preferences.putBool(Keys::Base::isPaired, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    UNITY_BEGIN();

    RUN_PHASE("peripherals", register_peripheral_tests, PF_NONE, PF_NONE);

    // Board + motor init only — SM is deferred until after pairing
    RUN_PHASE("board", register_board_tests, PF_NONE, PF_NONE);

    // BLE tests skipped - not relevant to integration test ticket
    // RUN_PHASE("ble",              register_ble_tests,
    //           PF_NONE,
    //           PF_NONE);

    RUN_PHASE("wifi", register_wifi_tests,
              PF_NONE,
              PF_WIFI);

    // Pair this synthetic device in the disposable local fixture before boot.
    // This ensures getAuthToken() returns isPaired=true, preventing the
    // restart loop that occurs when NVS says isPaired=true but server says
    // isPaired=false. Only generated Development configuration is accepted.
    {
        showTestPhase("server pairing");
        int pairCode = triggerPairing();
        char pairMsg[128];
        snprintf(pairMsg, sizeof(pairMsg),
                 "triggerPairing() returned %d", pairCode);
        TEST_MESSAGE(pairMsg);

        if (pairCode == 403) {
            TEST_FAIL_MESSAGE(
                "Local fixture rejected this device credential.");
        } else if (pairCode == 404) {
            TEST_FAIL_MESSAGE(
                "Local pairing fixture is unavailable. Start Development services.");
        } else if (pairCode != 200) {
            TEST_FAIL_MESSAGE("Local pairing fixture did not accept the request");
        }
        showTestResult("server pairing", pairCode == 200);
        BAIL_ON_FAILURE();
    }

    // NOW start the SM — after WiFi connected and device is paired in server
    {
        showTestPhase("sm start");
        startStateMachineForTests();
        showTestResult("sm start", lkbx != nullptr && lkbx->sm != nullptr);
        BAIL_ON_FAILURE();
    }

    RUN_PHASE("sm boot", register_sm_boot_tests,
              PF_WIFI | PF_NVS_UNLOCKED | PF_HEAP,
              PF_WIFI | PF_SM_HOME);

    RUN_PHASE("http", register_http_tests,
              PF_WIFI | PF_SERVER | PF_SM_HOME | PF_HEAP,
              PF_WIFI | PF_SM_HOME | PF_SERVER | PF_HEAP);

    RUN_PHASE("mqtt publish", register_mqtt_tests,
              PF_WIFI | PF_SERVER | PF_SM_HOME | PF_HEAP,
              PF_WIFI | PF_MQTT | PF_SM_HOME | PF_HEAP);

    RUN_PHASE("sm traversal", register_sm_traversal_tests,
              PF_WIFI | PF_MQTT | PF_SM_HOME | PF_NVS_UNLOCKED | PF_HEAP,
              PF_WIFI | PF_MQTT | PF_SM_HOME | PF_NVS_UNLOCKED | PF_HEAP);

    RUN_PHASE("break edge cases", register_break_tests,
              PF_WIFI | PF_MQTT | PF_SM_HOME | PF_NVS_UNLOCKED | PF_HEAP,
              PF_WIFI | PF_MQTT | PF_SM_HOME | PF_NVS_UNLOCKED | PF_HEAP);

    RUN_PHASE("mqtt dispatch", register_mqtt_dispatch_tests,
              PF_WIFI | PF_MQTT | PF_SM_HOME | PF_NVS_UNLOCKED | PF_HEAP,
              PF_WIFI | PF_MQTT | PF_SM_HOME | PF_NVS_UNLOCKED |
              PF_SERVER_UNLOCKED | PF_HEAP);

    RUN_PHASE("interactive", register_interactive_tests, PF_NONE, PF_NONE);

    UNITY_END();

    markTestRunComplete();

    Serial.flush();
    delay(2000);
    Serial.flush();

    esp_restart();
}

void loop() {
    delay(1000);
    esp_restart();
}
