#include "mqtt.h"

#include <Arduino.h>

#include "constants/LogTags.h"

namespace {
bool mqttPaused = false;
}

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
