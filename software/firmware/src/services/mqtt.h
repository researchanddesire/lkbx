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

// Starts a client that is paused: one whose task was kept only connects
// again, one without a task gets a new task, which needs a free 10 KB block of
// internal RAM and can fail.
bool startMqtt();
// Stops the client's task and waits for it to end.
bool stopMqtt();

// The Wi-Fi setup portal and a connected client do not fit in internal RAM
// together on R8 hardware, so the client is taken off the broker while a
// portal is open (see utils/mqttPause.h). The activity log task calls
// pauseMqttForPortal before it creates the portal's task (services/wm.cpp);
// resumeMqttAfterPortal is called once the portal has closed, or could not be
// opened. Never call pauseMqttForPortal from the MQTT task or from a state
// machine action: it waits for the MQTT task.
//
// Pausing closes the client's connection (a clean MQTT disconnect, so the
// broker publishes no last will) and keeps its task. The TLS session, which
// is most of what the client holds, comes back to the heap; the task and its
// 10 KB stack stay. Stopping the task as well gave the portal that stack, but
// after the portal the stack had to be found again in one piece, and on R8 it
// was not always there (2026-11-16 rig run: `Error create mqtt task` for more
// than ten minutes, the largest free block 5.6 KB). A task that never ends
// never has to be created again.
void pauseMqttForPortal();
void resumeMqttAfterPortal();
// The portal's own task did not fit beside the client's task. Stops the
// client's task after all, which frees its stack, and returns true; false
// when there was no kept task to stop. The client then needs a new task
// after the portal, as it did before its task was kept. Same rule as for
// pauseMqttForPortal: never from the MQTT task or a state machine action.
bool releaseMqttTaskForPortal();
// The client exists but its task could not be created (no free 10 KB block of
// internal RAM at boot). serviceMqttPause starts it, as it does after a
// portal, trying every two seconds until it fits.
void retryMqttStartLater();
// Called often from a task that is always running. Starts a client that is
// not running (paused for a portal, or never started) once no portal is
// open, trying again until its task fits in memory. True once per start, when
// the client has connected again, and once for every connection that follows
// a drop: the caller then asks for the dashboard's state to be read
// (LKBX::confirmServerClear), because the box heard nothing while it was away.
bool serviceMqttPause();
// A stop of the client is in progress, or it is being told to close its
// connection. The MQTT task reads this while it waits for its turn to connect
// (services/tlsTurn.h), so neither ever waits behind that.
bool mqttStopRequested();
// From the MQTT task: a connection attempt begins (true), the connection has
// closed or the attempt has failed (false).
void noteMqttLink(bool active);
// The client exists and no Wi-Fi setup portal holds it stopped: the box means
// to be on the broker. False while another task is stopping the client.
bool mqttClientExpected();
// The client is meant to run and its task cannot be created: the last attempt
// failed, and it is tried again every two seconds. Not true for a client that
// is paused for a portal or only waiting for its first attempt. Network
// recovery restarts the box when this lasts (utils/networkRecovery.h): the
// fault is the box's own, whatever the dashboard answers.
bool mqttClientCannotStart();
// Network recovery (utils/networkRecovery.h): stops the client, which gives
// back its task stack and its TLS session, and has serviceMqttPause start it
// again. Waits for the MQTT task to end, so the same rule as for
// pauseMqttForPortal applies: never from the MQTT task or a state machine
// action.
void restartMqttForRecovery();
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
