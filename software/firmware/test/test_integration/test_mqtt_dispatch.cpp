#include <ArduinoJson.h>
#include <CommandEnvelope.h>

#include "constants/dashboard.h"
#include "services/http_worker.h"
#include "services/settings.h"
#include "state/LKBX.h"
#include "utils/getEfuseMac.h"

#include "test_utils.h"

// The server only sends sync and reboot. Direct lock commands were removed, so
// holding broker access alone can never move the motor.
void test_mqtt_ignores_process_lock_from_home() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    simulateMqttData(R"({"type":"process","message":"lock","id":"t-lock-1"})");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    TEST_ASSERT_TRUE_MESSAGE(currentState().startsWith("home"),
                             "MQTT process lock must not change SM state");

    uint64_t lockEnd = nowMs() + 30000;
    char payload[256];
    snprintf(payload, sizeof(payload),
             R"({"type":"process","message":"lockTimer","id":"t-lt-1","lockEnd":%llu})",
             lockEnd);
    simulateMqttData(String(payload));
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    TEST_ASSERT_TRUE_MESSAGE(currentState().startsWith("home"),
                             "MQTT process lockTimer must not change SM state");
}

void test_mqtt_ignores_process_unlock_from_locked() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureLocked()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach locked state");
        return;
    }

    simulateMqttData(R"({"type":"process","message":"unlock","id":"t-unlock-1"})");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    TEST_ASSERT_TRUE_MESSAGE(smIsLockedOk(),
                             "MQTT process unlock must not unlock the SM");

    ensureHome();
}

// Runs the mbedTLS primitives against the vector shared with the server suite;
// native tests can only cover the host implementation.
void test_command_envelope_shared_vector_on_device() {
    lockbox::security::CommandGate gate;
    const char* key = "BWt_KAZ_bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOA";  // gitleaks:allow
    TEST_ASSERT_TRUE(gate.setKey(key, strlen(key)));
    const char* frame =
        R"({"v":2,"ts":1788800000000,"iv":"AAECAwQFBgcICQoLDA0ODw",)"
        R"("ct":"3-KeLqCpZZdL_p0ISyHDGrbDXx1hvZzT1FjyiUclTBU",)"
        R"("sig":"omqryKgNjSUIBgfZn-PHMKSvFnv2b0rpHRkZ_rpXe-A"})";
    std::string command;
    TEST_ASSERT_EQUAL(lockbox::security::CommandGateResult::opened,
                      gate.open("AA:BB:CC:DD:EE:FF", frame, strlen(frame), command));
    TEST_ASSERT_EQUAL_STRING(R"({"type":"sync","clear":true})", command.c_str());
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

// Real broker authentication and controller-only session publication are tested
// by software/dev/fixture/test_compose_integration.py. Devices never receive the controller
// credential or call a live broker administration test endpoint.

void register_mqtt_dispatch_tests() {
    // Simulated MQTT tests (fast, no network)
    RUN_TEST(test_command_envelope_shared_vector_on_device);
    RUN_TEST(test_mqtt_ignores_process_lock_from_home);
    RUN_TEST(test_mqtt_ignores_process_unlock_from_locked);
    RUN_TEST(test_mqtt_receive_sync_locked_from_home);
    RUN_TEST(test_mqtt_receive_sync_break_from_locked);
    RUN_TEST(test_mqtt_receive_sync_clear_from_locked);
    RUN_TEST(test_mqtt_receive_invalid_json);
    RUN_TEST(test_mqtt_receive_unknown_type);
}
