#include "mqtt.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <atomic>
#include <mutex>

#include "constants/LogTags.h"
#include "services/tlsTurn.h"
#include "utils/mqttPause.h"

namespace {
// The client is off the broker on purpose, or was never started: its
// connection is closed for a setup portal, or its task does not exist.
bool mqttPaused = false;
// Paused with its task kept: only the connection is closed, and the client
// does not connect again by itself. False for a client whose task is gone.
bool mqttTaskKept = false;
// The last attempt to create the client's task failed.
bool startFailing = false;
// When a kept client that had connected after all was last told to
// disconnect. Only serviceMqttPause touches it.
uint32_t keptClientToldMs = 0;

// Serializes stopping, starting and destroying the client with the portal
// bookkeeping, across the portal task, the activity log task, the sign-in
// task and an update.
std::mutex pauseMutex;
MqttPause portalPause;
// Connections that follow a drop. Only serviceMqttPause touches it.
MqttReconnect reconnectWatch;

// A stop is in progress: the MQTT task must not wait for its turn to connect
// (services/tlsTurn.h) while another task waits for it to end or to let go
// of its connection.
std::atomic<bool> stopRequested{false};
// The client is connecting or connected. Set by the MQTT task before every
// connection attempt and cleared when the connection has closed.
std::atomic<bool> linkActive{false};
// The client must not connect again by itself: it is kept for a setup portal.
// Read by the MQTT task when it sets its last will, which replaces this
// setting along with the others (applyLastWill).
std::atomic<bool> autoReconnectOff{false};

// How long a client is given to close its connection when it is asked to.
// Connected, it does so within about a second.
constexpr uint32_t kLetGoWaitMs = 5000;

// What the client was created with, minus everything it already holds a copy
// of (see rememberMqttSettings).
esp_mqtt_client_config_t rememberedSettings{};
bool settingsRemembered = false;
String rememberedWillTopic;

// Stops the MQTT task and waits for it to end. Called with pauseMutex held.
esp_err_t stopClientTask() {
    stopRequested.store(true);
    const esp_err_t result = esp_mqtt_client_stop(mqttClient);
    stopRequested.store(false);
    // A client stopped in the middle of connecting gives its turn back here.
    endTlsTurn(TlsUser::MQTT);
    linkActive.store(false);
    return result;
}

// Whether the client reconnects by itself after a drop. esp_mqtt_set_config
// replaces every setting, so it starts from the ones the client was created
// with. Takes the client's lock: during a connection attempt it waits for
// that attempt to end.
esp_err_t setAutoReconnect(bool on) {
    autoReconnectOff.store(!on);
    if (!settingsRemembered) return ESP_ERR_INVALID_STATE;
    esp_mqtt_client_config_t next = rememberedSettings;
    next.disable_auto_reconnect = !on;
    return esp_mqtt_set_config(mqttClient, &next);
}

// Closes the client's connection and keeps its task: the client is told not
// to connect again by itself and to disconnect, which is a clean MQTT
// disconnect, so the broker publishes no last will. Its TLS session comes
// back to the heap; its task, with the 10 KB stack that could not always be
// found again later, stays. Called with pauseMutex held. False when the
// client did not let go in time; the caller then stops its task.
bool closeClientConnection() {
    stopRequested.store(true);
    esp_err_t result = setAutoReconnect(false);
    if (result == ESP_OK) result = esp_mqtt_client_disconnect(mqttClient);
    bool closed = false;
    if (result == ESP_OK) {
        const uint32_t startMs = millis();
        while (true) {
            closed = !linkActive.load();
            if (closed ||
                static_cast<uint32_t>(millis() - startMs) >= kLetGoWaitMs) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    stopRequested.store(false);
    return closed;
}

// Takes a running client off the broker. Its task is kept where possible
// (closeClientConnection); otherwise the task is stopped, as before. Called
// with pauseMutex held.
bool pauseClient() {
    if (mqttClient == nullptr || mqttPaused) return true;

    if (closeClientConnection()) {
        mqttTaskKept = true;
    } else {
        ESP_LOGW(MQTT_TAG,
                 "MQTT client did not close its connection in time; "
                 "stopping its task");
        const esp_err_t result = stopClientTask();
        if (result != ESP_OK) {
            ESP_LOGE(MQTT_TAG, "Could not stop MQTT client: %s",
                     esp_err_to_name(result));
            // Running as before: it may connect again by itself.
            setAutoReconnect(true);
            return false;
        }
        mqttTaskKept = false;
    }

    mqttPaused = true;
    mqttConnected = false;
    ESP_LOGI(MQTT_TAG, "MQTT client paused%s",
             mqttTaskKept ? " (its task is kept)" : "");
    return true;
}
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
    if (mqttClient == nullptr) return true;
    // Already without a task: stopped before, or never started.
    if (mqttPaused && !mqttTaskKept) return true;

    const esp_err_t result = stopClientTask();
    if (result != ESP_OK) {
        ESP_LOGE(MQTT_TAG, "Could not stop MQTT client: %s",
                 esp_err_to_name(result));
        return false;
    }

    mqttTaskKept = false;
    mqttPaused = true;
    mqttConnected = false;
    ESP_LOGI(MQTT_TAG, "MQTT client paused");
    return true;
}

bool startMqtt() {
    if (mqttClient == nullptr || !mqttPaused) return true;

    // A client that was kept for a portal was told not to connect by itself.
    if (autoReconnectOff.load()) {
        const esp_err_t result = setAutoReconnect(true);
        if (result != ESP_OK) {
            ESP_LOGW(MQTT_TAG, "Could not turn MQTT reconnecting back on: %s",
                     esp_err_to_name(result));
            // The MQTT task turns it back on with its next last will.
        }
    }

    if (mqttTaskKept) {
        // The task never ended, so nothing has to be created: it only
        // connects again. A client that is not waiting to reconnect at this
        // moment refuses the request and connects by itself instead.
        esp_mqtt_client_reconnect(mqttClient);
        mqttTaskKept = false;
        mqttPaused = false;
        startFailing = false;
        ESP_LOGI(MQTT_TAG, "MQTT client resumed (its task was kept)");
        return true;
    }

    const esp_err_t result = esp_mqtt_client_start(mqttClient);
    if (result != ESP_OK) {
        startFailing = true;
        ESP_LOGE(MQTT_TAG, "Could not restart MQTT client: %s",
                 esp_err_to_name(result));
        return false;
    }

    startFailing = false;
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
    if (pauseClient()) portalPause.stopped();
}

bool releaseMqttTaskForPortal() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    if (mqttClient == nullptr || !mqttPaused || !mqttTaskKept) return false;

    // The task is waiting to reconnect and looks for a stop every five
    // seconds, so this can take that long.
    const esp_err_t result = stopClientTask();
    if (result != ESP_OK) {
        ESP_LOGE(MQTT_TAG, "Could not stop MQTT client: %s",
                 esp_err_to_name(result));
        return false;
    }
    mqttTaskKept = false;
    ESP_LOGW(MQTT_TAG,
             "MQTT client task stopped to make room for Wi-Fi setup");
    return true;
}

void resumeMqttAfterPortal() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    portalPause.portalClosed(millis());
}

void retryMqttStartLater() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    if (mqttClient == nullptr) return;
    mqttPaused = true;
    mqttTaskKept = false;
    // The sign-in task has just tried, more than once.
    startFailing = true;
    portalPause.startFailed(millis());
}

bool mqttStopRequested() { return stopRequested.load(); }

void noteMqttLink(bool active) { linkActive.store(active); }

bool mqttClientCannotStart() {
    // Never waits behind a stop that is in progress: the last answer stands
    // meanwhile, so a busy moment does not read as a client that runs.
    static std::atomic<bool> last{false};
    std::unique_lock<std::mutex> lock(pauseMutex, std::try_to_lock);
    if (!lock.owns_lock()) return last.load();
    const bool cannot = mqttClient != nullptr && mqttPaused &&
                        !mqttTaskKept && startFailing &&
                        !portalPause.portalOpen();
    last.store(cannot);
    return cannot;
}

bool mqttClientExpected() {
    // Never waits behind a stop that is in progress.
    std::unique_lock<std::mutex> lock(pauseMutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    return mqttClient != nullptr && !portalPause.portalOpen();
}

void restartMqttForRecovery() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    // A portal owns the client while it is open, and a client that is
    // already waiting to be started needs nothing more.
    if (mqttClient == nullptr || portalPause.portalOpen() || mqttPaused) return;
    // The same route as a client that could not start at boot: started again
    // in two seconds, tried until its task fits, and the state is read once
    // it has connected. This one stops the task as well: a box that is cut
    // off has had its internal RAM in pieces, and the stack is the largest
    // piece the client can give back. If the task then cannot be created
    // again, network recovery restarts the box (mqttClientCannotStart).
    if (stopMqtt()) portalPause.startFailed(millis());
}

bool serviceMqttPause() {
    // Never waits behind a stop that is in progress.
    std::unique_lock<std::mutex> lock(pauseMutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;

    // The client connects again by itself after a drop, and the broker keeps
    // nothing for a client that was away: whatever the dashboard published
    // meanwhile was not heard (2026-11-16 rig run: a command sent during a
    // short drop was lost). So every connection that follows a drop asks for
    // the dashboard's state, as a start after the portal does.
    const bool connected = mqttConnected;
    const bool reconnected = reconnectWatch.seen(connected);
    if (reconnected) {
        ESP_LOGI(MQTT_TAG, "MQTT connected again; reading the state");
    }

    // A kept client stays off the broker for as long as it is paused. One
    // that was about to connect when it was told to let go has connected
    // after all (the request is dropped when an attempt begins): it is told
    // again, at most once a second.
    if (mqttPaused && mqttTaskKept && mqttClient != nullptr &&
        linkActive.load()) {
        const uint32_t now = millis();
        if (static_cast<uint32_t>(now - keptClientToldMs) >= 1000) {
            keptClientToldMs = now;
            esp_mqtt_client_disconnect(mqttClient);
        }
    }

    switch (portalPause.next(millis(), connected)) {
        case MqttPauseStep::START_CLIENT: {
            if (mqttClient == nullptr) {
                mqttPaused = false;
                mqttTaskKept = false;
                startFailing = false;
                portalPause.clientGone();
                return reconnected;
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
            return reconnected;
        }
        case MqttPauseStep::RECONCILE:
            portalPause.reconcileAsked();
            return true;
        case MqttPauseStep::NONE:
        default:
            return reconnected;
    }
}

void destroyMqtt() {
    std::lock_guard<std::mutex> lock(pauseMutex);
    if (mqttClient != nullptr) {
        stopClientTask();
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
    }
    mqttConnected = false;
    mqttPaused = false;
    mqttTaskKept = false;
    startFailing = false;
    autoReconnectOff.store(false);
    linkActive.store(false);
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
    // This call replaces every setting. A client that is kept for a setup
    // portal must not start reconnecting by itself because of it.
    next.disable_auto_reconnect = autoReconnectOff.load();

    const esp_err_t result = esp_mqtt_set_config(mqttClient, &next);
    if (result != ESP_OK) {
        ESP_LOGW(MQTT_TAG, "Could not set the last will: %s",
                 esp_err_to_name(result));
        return false;
    }
    return true;
}
