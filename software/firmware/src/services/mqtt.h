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

// The Wi-Fi setup portal and the client do not fit in internal RAM together
// on R8 hardware, so the client is stopped while a portal is open (see
// utils/mqttPause.h). The portal task calls pauseMqttForPortal before it opens
// the portal and resumeMqttAfterPortal once it has closed it. Never call
// pauseMqttForPortal from the MQTT task or from a state machine action: it
// waits for the MQTT task to stop.
void pauseMqttForPortal();
void resumeMqttAfterPortal();
// Called often from a task that is always running. Starts a paused client
// again once the last portal has closed, trying again until its task fits in
// memory. True when the client has connected again and the caller should now
// read the dashboard's state; report that attempt with noteMqttReconcile.
bool serviceMqttPause();
void noteMqttReconcile(bool started);
// Stops and frees the client, for a firmware update or a client that could not
// be started.
void destroyMqtt();

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
