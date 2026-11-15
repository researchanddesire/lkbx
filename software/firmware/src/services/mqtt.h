#ifndef LOCKBOX_MQTT_H
#define LOCKBOX_MQTT_H

#include <WiFi.h>
#include <mqtt_client.h>

// Declare externals (no static here)
extern bool mqttConnected;
extern volatile bool bootHttpDone;
extern esp_mqtt_client_handle_t mqttClient;
extern const char* mqtt_server;
extern const int mqtt_port;

extern const char* root_ca;

bool startMqtt();
bool stopMqtt();

// The broker publishes a client's last will every time that client's
// connection drops, and the client reconnects on its own for as long as the
// box is up. So the will is built again before every connection attempt
// (MQTT_EVENT_BEFORE_CONNECT), not once per boot.
//
// rememberMqttSettings keeps what the client was created with, because
// esp_mqtt_set_config replaces the TLS settings and the task stack as well as
// the will. `willTopic` is copied.
void rememberMqttSettings(const esp_mqtt_client_config_t& config,
                          const char* willTopic);
// The MQTT client id the client was created with: the topic of its last will.
const String& mqttClientId();
// Gives the client `will` for its next connection, or no will at all when it
// is empty. Call from the MQTT task only (MQTT_EVENT_BEFORE_CONNECT).
bool applyLastWill(const String& will);

void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                        int32_t event_id, void* event_data);

#endif  // LOCKBOX_MQTT_H
