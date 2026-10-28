#include <mqtt_client.h>

#include "constants/dashboard.h"
#include "services/mqtt.h"
#include "utils/getEfuseMac.h"

#include "test_utils.h"

void test_mqtt_status() {
    if (!testWifiUp) {
        TEST_FAIL_MESSAGE("Skipped: no WiFi");
        return;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "MQTT state: connected=%d client=%p broker=%s:%d",
             mqttConnected, mqttClient, mqtt_server, mqtt_port);
    TEST_MESSAGE(msg);

    if (mqttConnected && mqttClient) {
        TEST_MESSAGE("MQTT already connected from SM boot");
        return;
    }

    TEST_MESSAGE("MQTT not connected from SM boot, attempting manual connect");

    if (!cachedAuth.valid) {
        cachedAuth = getAuthToken();
    }
    if (!cachedAuth.valid) {
        TEST_FAIL_MESSAGE("Cannot connect MQTT: auth failed");
        return;
    }

    if (mqttClient) {
        esp_mqtt_client_stop(mqttClient);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
        mqttConnected = false;
    }

    static String lwt = createStateMessage("disconnected", false);

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.uri = mqtt_server;
    mqtt_cfg.port = static_cast<uint32_t>(mqtt_port);
    mqtt_cfg.client_id = cachedAuth.clientId.c_str();
    mqtt_cfg.username = cachedAuth.username.c_str();
    mqtt_cfg.password = cachedAuth.password.c_str();
    mqtt_cfg.lwt_topic = cachedAuth.clientId.c_str();
    mqtt_cfg.lwt_msg = lwt.c_str();
    mqtt_cfg.lwt_qos = 2;
    mqtt_cfg.lwt_retain = true;
    mqtt_cfg.keepalive = 15;
    mqtt_cfg.buffer_size = 4096;
    mqtt_cfg.cert_pem = root_ca;

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    TEST_ASSERT_NOT_NULL_MESSAGE(mqttClient, "MQTT client should init");

    esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY,
                                   mqtt_event_handler, nullptr);
    esp_mqtt_client_start(mqttClient);

    bool connected = waitFor([]() { return mqttConnected; }, 15000, 500);

    snprintf(msg, sizeof(msg), "Manual MQTT connect: connected=%d", connected);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(connected, "MQTT should connect within 15s");
}

void test_mqtt_subscribe() {
    if (!mqttConnected) {
        TEST_FAIL_MESSAGE("Skipped: MQTT not connected");
        return;
    }
    String topic = getMacAddresss();
    int msgId = esp_mqtt_client_subscribe(mqttClient, topic.c_str(), 2);

    char msg[128];
    snprintf(msg, sizeof(msg), "Subscribe %s -> msgId=%d", topic.c_str(),
             msgId);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(msgId >= 0, "Subscribe should succeed");
}

void test_mqtt_publish_locked() {
    if (!mqttConnected) {
        TEST_FAIL_MESSAGE("Skipped: MQTT not connected");
        return;
    }
    String topic = getMacAddresss();
    String stateMsg = createStateMessage("locked", true);
    int msgId = esp_mqtt_client_enqueue(mqttClient, topic.c_str(),
                                        stateMsg.c_str(), 0, 2, false, true);
    char msg[128];
    snprintf(msg, sizeof(msg), "Publish 'locked' -> msgId=%d", msgId);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(msgId >= 0, "Publish 'locked' should succeed");
}

void test_mqtt_publish_unlocked() {
    if (!mqttConnected) {
        TEST_FAIL_MESSAGE("Skipped: MQTT not connected");
        return;
    }
    String topic = getMacAddresss();
    String stateMsg = createStateMessage("unlocked", true);
    int msgId = esp_mqtt_client_enqueue(mqttClient, topic.c_str(),
                                        stateMsg.c_str(), 0, 2, false, true);
    char msg[128];
    snprintf(msg, sizeof(msg), "Publish 'unlocked' -> msgId=%d", msgId);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(msgId >= 0, "Publish 'unlocked' should succeed");
}

void test_mqtt_publish_disconnected() {
    if (!mqttConnected) {
        TEST_FAIL_MESSAGE("Skipped: MQTT not connected");
        return;
    }
    String topic = getMacAddresss();
    String stateMsg = createStateMessage("disconnected", true);
    int msgId = esp_mqtt_client_enqueue(mqttClient, topic.c_str(),
                                        stateMsg.c_str(), 0, 2, false, true);
    char msg[128];
    snprintf(msg, sizeof(msg), "Publish 'disconnected' -> msgId=%d", msgId);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(msgId >= 0,
                             "Publish 'disconnected' should succeed");
}

void test_mqtt_publish_emergency() {
    if (!mqttConnected) {
        TEST_FAIL_MESSAGE("Skipped: MQTT not connected");
        return;
    }
    String topic = getMacAddresss();
    String stateMsg = createStateMessage("emergency", true);
    int msgId = esp_mqtt_client_enqueue(mqttClient, topic.c_str(),
                                        stateMsg.c_str(), 0, 2, false, true);
    char msg[128];
    snprintf(msg, sizeof(msg), "Publish 'emergency' -> msgId=%d", msgId);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(msgId >= 0,
                             "Publish 'emergency' should succeed");
}

void test_mqtt_publish_break() {
    if (!mqttConnected) {
        TEST_FAIL_MESSAGE("Skipped: MQTT not connected");
        return;
    }
    String topic = getMacAddresss();
    String stateMsg = createStateMessage("break", true);
    int msgId = esp_mqtt_client_enqueue(mqttClient, topic.c_str(),
                                        stateMsg.c_str(), 0, 2, false, true);
    char msg[128];
    snprintf(msg, sizeof(msg), "Publish 'break' -> msgId=%d", msgId);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(msgId >= 0, "Publish 'break' should succeed");
}

void test_mqtt_publish_activity_log() {
    if (!mqttConnected) {
        TEST_FAIL_MESSAGE("Skipped: MQTT not connected");
        return;
    }
    isTimeSynced = true;
    String topic = getMacAddresss() + "/log";
    String logMsg = createActivityLogMessage("state_change", "test");
    int msgId = esp_mqtt_client_enqueue(mqttClient, topic.c_str(),
                                        logMsg.c_str(), 0, 2, false, true);
    char msg[128];
    snprintf(msg, sizeof(msg), "Publish activity log -> msgId=%d", msgId);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(msgId >= 0,
                             "Publish activity log should succeed");
}

void register_mqtt_tests() {
    RUN_TEST(test_mqtt_status);
    RUN_TEST(test_mqtt_subscribe);
    RUN_TEST(test_mqtt_publish_locked);
    RUN_TEST(test_mqtt_publish_unlocked);
    RUN_TEST(test_mqtt_publish_disconnected);
    RUN_TEST(test_mqtt_publish_emergency);
    RUN_TEST(test_mqtt_publish_break);
    RUN_TEST(test_mqtt_publish_activity_log);
}
