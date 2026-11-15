#include "mqtt.h"

#include <Arduino.h>

#include "constants/LogTags.h"

namespace {
bool mqttPaused = false;

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
