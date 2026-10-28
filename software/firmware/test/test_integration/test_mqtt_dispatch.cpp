#include <ArduinoJson.h>

#include "constants/dashboard.h"
#include "services/http_worker.h"
#include "services/settings.h"
#include "state/LKBX.h"
#include "utils/getEfuseMac.h"

#include "test_utils.h"

void test_mqtt_receive_process_lock_from_home() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    simulateMqttData(R"({"type":"process","message":"lock","id":"t-lock-1"})");

    bool reached = waitFor(smIsLockedOk, 10000, 250);

    String state = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "MQTT process lock -> %s", state.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(),
                              "SM should not be in an error state");
    TEST_ASSERT_TRUE_MESSAGE(reached,
                             "MQTT lock should transition SM to locked/locking");
}

void test_mqtt_receive_process_unlock_from_locked() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureLocked()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach locked state");
        return;
    }

    simulateMqttData(R"({"type":"process","message":"unlock","id":"t-unlock-1"})");

    bool reached = waitFor(smAtHome, 15000, 250);

    if (!reached && smInErrorState()) {
        char retry[128];
        snprintf(retry, sizeof(retry), "MQTT unlock retry — SM at: %s",
                 currentState().c_str());
        TEST_MESSAGE(retry);
        reached = ensureHome();
    }

    String state = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "MQTT process unlock -> %s", state.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(reached,
                             "MQTT unlock should transition SM to home");
}

void test_mqtt_receive_process_lock_timer_from_home() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    uint64_t lockEnd = nowMs() + 30000;

    char payload[256];
    snprintf(payload, sizeof(payload),
             R"({"type":"process","message":"lockTimer","id":"t-lt-1","lockEnd":%llu})",
             lockEnd);

    simulateMqttData(String(payload));

    bool reached = waitFor(smIsLockedOk, 10000, 250);

    String state = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "MQTT process lockTimer -> %s", state.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(),
                              "SM should not be in an error state");
    TEST_ASSERT_TRUE_MESSAGE(reached,
                             "MQTT lockTimer should transition SM to locked/locking");

    uint64_t storedLockEnd = 0;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        storedLockEnd = preferences.getULong64(Keys::Session::lockEnd, 0);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    TEST_ASSERT_TRUE_MESSAGE(lockEnd == storedLockEnd,
                             "lockEnd should be persisted to NVS");

    ensureHome();
}

void test_mqtt_receive_sync_locked_from_home() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    uint64_t lockStart = nowMs();
    uint64_t lockEnd = lockStart + 120000;

    char payload[512];
    snprintf(payload, sizeof(payload),
             R"({"type":"sync","isBreak":false,"lockEnd":%llu,"breakMax":0,)"
             R"("breakReg":0,"breakLast":0,"isEUnlock":false,"lockStart":%llu,)"
             R"("lockState":"locked","sessionId":"test-sync-session-1",)"
             R"("isTestLock":true,"isTimeDisp":true,"duration":120})",
             lockEnd, lockStart);

    simulateMqttData(String(payload));

    bool reached = waitFor(smIsLockedOk, 10000, 250);

    String state = currentState();
    char msg[256];
    snprintf(msg, sizeof(msg), "MQTT sync locked -> %s", state.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(),
                              "SM should not be in an error state");
    TEST_ASSERT_TRUE_MESSAGE(reached,
                             "MQTT sync with lockState:locked should lock SM");

    String storedSessionId;
    bool storedIsLocked = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        storedSessionId = preferences.getString(Keys::Session::sessionId, "");
        preferences.end();

        preferences.begin(Keys::base);
        storedIsLocked = preferences.getBool(Keys::Base::isLocked, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test-sync-session-1",
                                     storedSessionId.c_str(),
                                     "sessionId should be persisted to NVS");
    TEST_ASSERT_TRUE_MESSAGE(storedIsLocked, "isLocked should be true in NVS");

    ensureHome();
}

static bool smInActiveBreak() { return currentState() == "locked.break"; }

void test_mqtt_receive_sync_break_from_locked() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureLocked()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach locked state");
        return;
    }

    uint64_t lockStart = nowMs();
    uint64_t lockEnd = lockStart + 120000;

    char payload[512];
    snprintf(payload, sizeof(payload),
             R"({"type":"sync","isBreak":true,"lockEnd":%llu,"breakMax":120000,)"
             R"("breakReg":0,"breakLast":0,"isEUnlock":false,"lockStart":%llu,)"
             R"("lockState":"break","sessionId":"test-sync-break-1",)"
             R"("isTestLock":true,"isTimeDisp":true,"duration":120})",
             lockEnd, lockStart);

    simulateMqttData(String(payload));

    bool reached = waitFor(smInActiveBreak, 10000, 250);

    String state = currentState();
    char msg[256];
    snprintf(msg, sizeof(msg), "MQTT sync break -> %s", state.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(),
                              "SM should not be in an error state");
    TEST_ASSERT_TRUE_MESSAGE(
        reached,
        "MQTT sync with lockState:break should move SM to locked.break");

    ensureHome();
}

void test_mqtt_receive_sync_clear_from_locked() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }

    if (!ensureLocked()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach locked state");
        return;
    }

    simulateMqttData(R"({"type":"sync","clear":true})");

    bool reached = waitFor(smAtHome, 15000, 250);

    String state = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "MQTT sync clear -> %s", state.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(),
                              "SM should not be in an error state");
    TEST_ASSERT_TRUE_MESSAGE(reached,
                             "MQTT sync clear should unlock SM to home");

    bool storedIsLocked = true;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        storedIsLocked = preferences.getBool(Keys::Base::isLocked, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    TEST_ASSERT_FALSE_MESSAGE(storedIsLocked,
                              "isLocked should be false after sync clear");

    ensureHome();
}

void test_mqtt_receive_invalid_json() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    simulateMqttData("{malformed json that won't parse");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    String after = currentState();

    char msg[256];
    snprintf(msg, sizeof(msg), "Invalid JSON: after=%s", after.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(after.startsWith("home"),
                             "Invalid JSON should not change SM state");
}

void test_mqtt_receive_unknown_type() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    simulateMqttData(R"({"type":"banana","message":"x","id":"t-unk-1"})");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    String after = currentState();

    char msg[256];
    snprintf(msg, sizeof(msg), "Unknown type: after=%s", after.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(after.startsWith("home"),
                             "Unknown MQTT type should not change SM state");
}

void test_mqtt_receive_process_lock_while_locked() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureLocked()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach locked state");
        return;
    }

    String before = currentState();
    simulateMqttData(R"({"type":"process","message":"lock","id":"t-lock-dup"})");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    String after = currentState();

    char msg[256];
    snprintf(msg, sizeof(msg), "Lock while locked: before=%s after=%s",
             before.c_str(), after.c_str());
    TEST_MESSAGE(msg);

    TEST_ASSERT_TRUE_MESSAGE(smIsLockedOk(),
                             "SM should stay in locked/locking state (no error)");

    ensureHome();
}

void test_mqtt_receive_process_unlock_from_home() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    String before = currentState();
    simulateMqttData(R"({"type":"process","message":"unlock","id":"t-unlock-home"})");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    bool atHome = waitFor(smAtHome, 3000, 250);

    String after = currentState();
    char msg[256];
    snprintf(msg, sizeof(msg), "Unlock from home: before=%s after=%s",
             before.c_str(), after.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(atHome,
                             "SM should stay at or return to home after unlock from home");
}

// Real broker authentication and controller-only session publication are tested
// by software/dev/fixture/test_compose_integration.py. Devices never receive the controller
// credential or call a live broker administration test endpoint.

void register_mqtt_dispatch_tests() {
    // Simulated MQTT tests (fast, no network)
    RUN_TEST(test_mqtt_receive_process_lock_from_home);
    RUN_TEST(test_mqtt_receive_process_unlock_from_locked);
    RUN_TEST(test_mqtt_receive_process_lock_timer_from_home);
    RUN_TEST(test_mqtt_receive_sync_locked_from_home);
    RUN_TEST(test_mqtt_receive_sync_break_from_locked);
    RUN_TEST(test_mqtt_receive_sync_clear_from_locked);
    RUN_TEST(test_mqtt_receive_invalid_json);
    RUN_TEST(test_mqtt_receive_unknown_type);
    RUN_TEST(test_mqtt_receive_process_lock_while_locked);
    RUN_TEST(test_mqtt_receive_process_unlock_from_home);

}
