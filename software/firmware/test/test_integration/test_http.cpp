#include <ArduinoJson.h>

#include "constants/dashboard.h"
#include "services/http_worker.h"
#include "services/ina.h"
#include "utils/getEfuseMac.h"
#include "utils/syncState.h"
#include "utils/update.h"

#include "test_utils.h"

void test_http_health_check() {
    if (!testWifiUp) {
        TEST_FAIL_MESSAGE("Skipped: no WiFi");
        return;
    }
    assertHeapForTLS("pre health-check");
    String url = String(RAD_SERVER) + "/api/health-check";
    int code = get(url, nullptr);
    logHeap("post health-check");

    char msg[128];
    snprintf(msg, sizeof(msg), "GET /api/health-check -> %d", code);
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, code, "Health check should return 200");
}

void test_http_auth_token() {
    if (!testWifiUp) {
        TEST_FAIL_MESSAGE("Skipped: no WiFi");
        return;
    }
    assertHeapForTLS("pre auth");
    cachedAuth = getAuthToken();
    logHeap("post auth");
    TEST_ASSERT_TRUE_MESSAGE(cachedAuth.valid, "Auth response should be valid");
    TEST_ASSERT_TRUE_MESSAGE(cachedAuth.clientId.length() > 0,
                             "clientId should not be empty");
    TEST_ASSERT_TRUE_MESSAGE(cachedAuth.username.length() > 0,
                             "username should not be empty");
    TEST_ASSERT_TRUE_MESSAGE(cachedAuth.password.length() > 0,
                             "password should not be empty");

    char msg[128];
    snprintf(msg, sizeof(msg),
             "POST /api/lockbox/auth -> clientId=%s paired=%d",
             cachedAuth.clientId.c_str(), cachedAuth.isPaired);
    TEST_MESSAGE(msg);
}

void test_http_is_paired() {
    if (!testWifiUp) {
        TEST_FAIL_MESSAGE("Skipped: no WiFi");
        return;
    }
    assertHeapForTLS("pre is-paired");
    String deviceId = getMacAddresss();
    JsonDocument doc;
    doc["macAddress"] = deviceId;

    String payload;
    int code =
        post(String(RAD_SERVER) + "/api/lockbox/is-paired", doc, &payload);
    logHeap("post is-paired");

    char msg[128];
    snprintf(msg, sizeof(msg), "POST /api/lockbox/is-paired -> %d", code);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(code == 200 || code == 404,
                             "is-paired should return 200 or 404");
}

void test_http_get_state() {
    if (!testWifiUp) {
        TEST_FAIL_MESSAGE("Skipped: no WiFi");
        return;
    }
    assertHeapForTLS("pre GET state");
    String url = String(RAD_SERVER) + "/api/lockbox/state/" + getMacAddresss();
    String payload;
    int code = get(url, &payload);
    logHeap("post GET state");

    char msg[256];
    snprintf(msg, sizeof(msg), "GET /api/lockbox/state/{MAC} -> %d (%d bytes)",
             code, (int)payload.length());
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, code, "GET state should return 200");
    TEST_ASSERT_TRUE_MESSAGE(payload.length() > 0,
                             "Response body should not be empty");

    JsonDocument data;
    DeserializationError err = deserializeJson(data, payload);
    TEST_ASSERT_TRUE_MESSAGE(err == DeserializationError::Ok,
                             "Response should be valid JSON");
}

void test_http_post_state() {
    if (!testWifiUp) {
        TEST_FAIL_MESSAGE("Skipped: no WiFi");
        return;
    }
    assertHeapForTLS("pre POST state");
    String url = String(RAD_SERVER) + "/api/lockbox/state/" + getMacAddresss();
    String stateMsg = createStateMessage("disconnected", true);
    int code = post(url, stateMsg, nullptr, 5000);
    logHeap("post POST state");

    char msg[128];
    snprintf(msg, sizeof(msg), "POST /api/lockbox/state/{MAC} -> %d", code);
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, code, "POST state should return 200");
}

void test_http_version_check() {
    if (!testWifiUp) {
        TEST_FAIL_MESSAGE("Skipped: no WiFi");
        return;
    }
    assertHeapForTLS("pre version-check");
    Version remote = getRemoteVersion();
    logHeap("post version-check");

    char msg[128];
    snprintf(msg, sizeof(msg),
             "GET version.json -> remote v%d.%d.%d",
             remote.major, remote.minor, remote.patch);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(remote.major >= 0 && remote.minor >= 0 &&
                                 remote.patch >= 0,
                             "Remote version fields should be non-negative");
}

void test_http_pull_session_parsed() {
    if (!testWifiUp) {
        TEST_FAIL_MESSAGE("Skipped: no WiFi");
        return;
    }
    assertHeapForTLS("pre pullSession");
    SyncResult result = pullSession();
    logHeap("post pullSession");

    char msg[64];
    snprintf(msg, sizeof(msg), "pullSession() -> SyncResult=%d",
             static_cast<int>(result));
    TEST_MESSAGE(msg);

    TEST_ASSERT_TRUE_MESSAGE(
        result == SyncResult::UNLOCKED || result == SyncResult::LOCKED ||
            result == SyncResult::BREAK || result == SyncResult::NO_CHANGE ||
            result == SyncResult::FAILED,
        "pullSession should return a valid SyncResult");
}

void test_http_heap_floor() {
    uint32_t maxBlock = ESP.getMaxAllocHeap();
    logHeap("http batch complete");

    char msg[192];
    snprintf(msg, sizeof(msg),
             "Heap floor after HTTP batch: max_block=%lu (need >=%lu)",
             (unsigned long)maxBlock, (unsigned long)TLS_MIN_HEAP);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(
        maxBlock >= TLS_MIN_HEAP,
        "Heap degraded below TLS threshold after HTTP tests — possible leak");
}

void register_http_tests() {
    RUN_TEST(test_http_health_check);
    RUN_TEST(test_http_auth_token);
    RUN_TEST(test_http_is_paired);
    RUN_TEST(test_http_get_state);
    RUN_TEST(test_http_post_state);
    RUN_TEST(test_http_version_check);
    RUN_TEST(test_http_pull_session_parsed);
    RUN_TEST(test_http_heap_floor);
}
