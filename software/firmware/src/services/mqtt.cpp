#include "mqtt.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <mutex>

#include "constants/LogTags.h"
#include "utils/mqttPause.h"

namespace {
bool mqttPaused = false;

// Serializes stopping, starting and destroying the client with the portal
// bookkeeping, across the portal task, the activity log task, the sign-in
// task and an update.
std::mutex pauseMutex;
MqttPause portalPause;

// What the client was created with, minus everything it already holds a copy
// of (see rememberMqttSettings).
esp_mqtt_client_config_t rememberedSettings{};
bool settingsRemembered = false;
String rememberedWillTopic;
}  // namespace

// Define the global variables here
bool mqttConnected = false;
volatile bool bootHttpDone = false;
esp_mqtt_client_handle_t mqttClient = nullptr;

// MQTT server configuration
#if defined(MQTT_SERVER)
const char* mqtt_server = MQTT_SERVER;
#else
#error "MQTT_SERVER must be set by an explicit firmware profile"
#endif

#if defined(MQTT_PORT)
const int mqtt_port = MQTT_PORT;
#else
#error "MQTT_PORT must be set by an explicit firmware profile"
#endif

bool stopMqtt() {
    if (mqttClient == nullptr || mqttPaused) return true;

    const esp_err_t result = esp_mqtt_client_stop(mqttClient);
    if (result != ESP_OK) {
        ESP_LOGE(MQTT_TAG, "Could not stop MQTT client: %s",
                 esp_err_to_name(result));
        return false;
    }

    mqttPaused = true;
    mqttConnected = false;
    ESP_LOGI(MQTT_TAG, "MQTT client paused");
    return true;
}

bool startMqtt() {
    if (mqttClient == nullptr || !mqttPaused) return true;

    const esp_err_t result = esp_mqtt_client_start(mqttClient);
    if (result != ESP_OK) {
        ESP_LOGE(MQTT_TAG, "Could not restart MQTT client: %s",
                 esp_err_to_name(result));
        return false;
    }

    mqttPaused = false;
    ESP_LOGI(MQTT_TAG, "MQTT client resumed");
    return true;
}

void pauseMqttForPortal() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    portalPause.portalOpened();
    if (!portalPause.shouldStop() || mqttClient == nullptr) return;
    // A client that was never started stays as it is; it has nothing to
    // give back and its own start is still to come.
    if (stopMqtt()) portalPause.stopped();
}

void resumeMqttAfterPortal() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    portalPause.portalClosed(millis());
}

void retryMqttStartLater() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    if (mqttClient == nullptr) return;
    mqttPaused = true;
    portalPause.startFailed(millis());
}

bool serviceMqttPause() {
    // Never waits behind a stop that is in progress.
    std::unique_lock<std::mutex> lock(pauseMutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;

    switch (portalPause.next(millis(), mqttConnected)) {
        case MqttPauseStep::START_CLIENT: {
            if (mqttClient == nullptr) {
                mqttPaused = false;
                portalPause.clientGone();
                return false;
            }
            const bool started = startMqtt();
            if (!started) {
                ESP_LOGW(MQTT_TAG,
                         "MQTT client not resumed yet: largest free internal "
                         "block %u bytes, %u bytes free",
                         static_cast<unsigned>(heap_caps_get_largest_free_block(
                             MALLOC_CAP_INTERNAL)),
                         static_cast<unsigned>(
                             heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
            }
            portalPause.startTried(started, millis());
            return false;
        }
        case MqttPauseStep::RECONCILE:
            portalPause.reconcileAsked();
            return true;
        case MqttPauseStep::NONE:
        default:
            return false;
    }
}

void destroyMqtt() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    if (mqttClient != nullptr) {
        esp_mqtt_client_stop(mqttClient);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
    }
    mqttConnected = false;
    mqttPaused = false;
    portalPause.clientGone();
}

void rememberMqttSettings(const esp_mqtt_client_config_t& config,
                          const char* willTopic) {
    rememberedSettings = config;
    // The client keeps its own copies of these, and a null leaves them as
    // they are. The originals belong to the sign-in and do not outlive it.
    rememberedSettings.uri = nullptr;
    rememberedSettings.host = nullptr;
    rememberedSettings.path = nullptr;
    rememberedSettings.client_id = nullptr;
    rememberedSettings.username = nullptr;
    rememberedSettings.password = nullptr;
    rememberedSettings.lwt_topic = nullptr;
    rememberedSettings.lwt_msg = nullptr;
    rememberedSettings.lwt_msg_len = 0;
    rememberedWillTopic = willTopic ? willTopic : "";
    settingsRemembered = true;
}

const String& mqttClientId() { return rememberedWillTopic; }

bool applyLastWill(const String& will) {
    if (mqttClient == nullptr || !settingsRemembered) return false;

    esp_mqtt_client_config_t next = rememberedSettings;
    // An empty will topic makes the client connect without a will.
    const bool hasWill = !will.isEmpty() && !rememberedWillTopic.isEmpty();
    next.lwt_topic = hasWill ? rememberedWillTopic.c_str() : "";
    next.lwt_msg = hasWill ? will.c_str() : "";
    next.lwt_msg_len = hasWill ? static_cast<int>(will.length()) : 0;

    const esp_err_t result = esp_mqtt_set_config(mqttClient, &next);
    if (result != ESP_OK) {
        ESP_LOGW(MQTT_TAG, "Could not set the last will: %s",
                 esp_err_to_name(result));
        return false;
    }
    return true;
}
