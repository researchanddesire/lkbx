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
// utils/mqttPause.h). The activity log task calls pauseMqttForPortal before it
// creates the portal's task (services/wm.cpp), so that task never has to fit
// beside the client; resumeMqttAfterPortal is called once the portal has
// closed, or could not be opened. Never call pauseMqttForPortal from the MQTT
// task or from a state machine action: it waits for the MQTT task to stop.
void pauseMqttForPortal();
void resumeMqttAfterPortal();
// The client exists but its task could not be created (no free 10 KB block of
// internal RAM at boot). serviceMqttPause starts it, as it does after a
// portal, trying every two seconds until it fits.
void retryMqttStartLater();
// Called often from a task that is always running. Starts a client that is
// not running (stopped for a portal, or never started) once no portal is
// open, trying again until its task fits in memory. True once per start, when
// the client has connected again: the caller then asks for the dashboard's
// state to be read (LKBX::confirmServerClear).
bool serviceMqttPause();
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
