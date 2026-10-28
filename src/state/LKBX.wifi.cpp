#include "LKBX.h"

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "constants/dashboard.h"
#include "services/mqtt.h"
#include "services/ntp.h"
#include "services/rad_ble.h"
#include "services/wm.h"
#include "utils/dashboard.hpp"
#include "utils/PairingPromptPoll.h"
namespace {

    void persistLockboxAuthToNvs(const AuthResponse &response) {
        if (!response.valid) {
            return;
        }
        if (response.isPaired) markPairingCodePaired();
        else acceptPairingCode(response.pairingCode, response.pairingCodeExpiresAt);
        if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) !=
            pdTRUE) {
            return;
        }
        preferences.begin(Keys::base, false);
        preferences.putString(Keys::Base::pairingCode, response.pairingCode);
        preferences.putInt(Keys::Base::hueShift, response.hueShift);
        if (response.pairingStatusKnown && response.isPaired) {
            preferences.putBool(Keys::Base::isPaired, true);
        } else if (response.pairingStatusKnown) {
            const bool wasConsideredPaired =
                preferences.getBool(Keys::Base::isPaired, false);
            if (wasConsideredPaired) {
                preferences.putBool(Keys::Base::isPaired, false);
                preferences.end();
                xSemaphoreGive(preferencesMutex);
                esp_restart();
            }
        }
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

}  // namespace

void LKBX::checkWifiTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);

    vTaskDelay(1);  // Minimal delay to yield
    plkbx->sm->process_event(Done{});
    vTaskDelete(nullptr);
}

void LKBX::startWifiConnectionTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);

    // Uses global WIFI_CONNECT_TAG from LogTags.h
    constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
    constexpr uint32_t WIFI_CONNECT_POLL_MS = 250;

    if (!plkbx->pendingWifiSsid.isEmpty()) {
        ESP_LOGD(WIFI_CONNECT_TAG,
                 "Starting WiFi connection using BLE-provisioned credentials");
        WiFi.begin(plkbx->pendingWifiSsid.c_str(),
                   plkbx->pendingWifiPassword.c_str());
    } else if (WiFiClass::status() != WL_CONNECTED) {
        ESP_LOGD(WIFI_CONNECT_TAG,
                 "Starting WiFi connection using saved credentials");
        WiFi.begin();
    } else {
        ESP_LOGD(WIFI_CONNECT_TAG, "WiFi already connected, skipping begin()");
    }

    const uint32_t startMs = millis();
    while (WiFiClass::status() != WL_CONNECTED &&
           (millis() - startMs) < WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(WIFI_CONNECT_POLL_MS / portTICK_PERIOD_MS);
    }

    if (WiFiClass::status() != WL_CONNECTED) {
        const wl_status_t status = WiFiClass::status();
        ErrorT error = ErrorT::WIFI_NETWORK_UNREACHABLE;
        if (status == WL_CONNECT_FAILED) {
            error = ErrorT::WIFI_AUTH_FAILED;
        } else if (status == WL_NO_SSID_AVAIL) {
            error = ErrorT::WIFI_NETWORK_UNREACHABLE;
        }
        ESP_LOGW(WIFI_CONNECT_TAG,
                 "Timed out waiting for WiFi connection (status=%d)", status);
        plkbx->sm->process_event(Error{.error = error});
        vTaskDelete(nullptr);
        return;
    }

    // WL_CONNECTED fires before DHCP assigns an IP.
    // Wait for a routable IP before attempting any HTTP requests.
    while (WiFi.localIP() == IPAddress(INADDR_NONE) &&
           (millis() - startMs) < WIFI_CONNECT_TIMEOUT_MS) {
        ESP_LOGD(WIFI_CONNECT_TAG, "Waiting for IP assignment...");
        vTaskDelay(WIFI_CONNECT_POLL_MS / portTICK_PERIOD_MS);
    }

    if (WiFi.localIP() == IPAddress(INADDR_NONE)) {
        ESP_LOGW(WIFI_CONNECT_TAG, "Timed out waiting for IP assignment");
        plkbx->sm->process_event(Error{.error = ErrorT::WIFI_NO_IP});
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGD(WIFI_CONNECT_TAG,
             "WiFi connected with IP %s, running server health check",
             WiFi.localIP().toString().c_str());

    // Obtain authenticated time before cloud state synchronization. A rejected
    // time request leaves the application clock unchanged.
    if (!isTimeSynced) {
        updateTime();
    }

    String healthUrl = String(RAD_SERVER) + Str::Api::HEALTH_CHECK;
    int responseCode = -1;

    for (int attempt = 0; attempt < 10; attempt++) {
        responseCode = get(healthUrl, nullptr);
        if (responseCode == 200) {
            break;
        }
        ESP_LOGW(WIFI_CONNECT_TAG, "Health check failed. HTTP code: %d",
                 responseCode);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if (responseCode == 200) {
        ESP_LOGD(WIFI_CONNECT_TAG, "Health check succeeded");
        finalizeHealthyFirmwareBoot();
        plkbx->sm->process_event(Done{});
    } else {
        ESP_LOGW(WIFI_CONNECT_TAG, "Health check failed. HTTP code: %d",
                 responseCode);
        plkbx->sm->process_event(Error{.error = ErrorT::SERVER_UNREACHABLE});
    }

    vTaskDelete(nullptr);
}

void LKBX::startWifiConnection() {
    ESP_LOGD(TASKDEBUG_TAG, "Creating wifi connection task");
    xTaskCreatePinnedToCore(startWifiConnectionTask, Tasks::wifiConnect.name,
                            Tasks::wifiConnect.stackSize, this,
                            Tasks::wifiConnect.priority,
                            &TaskHandles::wifiConnect, Tasks::wifiConnect.core);
}

// Onboarding update check task.
// Skips if offline, otherwise checks for major/minor version bumps
// and triggers the update flow or proceeds to pairing check.
void LKBX::checkForUpdatesTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);

    // Skip update check if device is offline
    if (WiFiClass::status() != WL_CONNECTED) {
        ESP_LOGD(UPDATE_CHECK_TAG, "Offline - skipping update check");
        plkbx->sm->process_event(Done{});
        vTaskDelete(nullptr);
        return;
    }

    // Check for mandatory updates (major/minor version bumps)
    bool updateIsAvailable = shouldForceUpdate();
    if (updateIsAvailable) {
        ESP_LOGI(UPDATE_CHECK_TAG, "Update available - triggering update");
        plkbx->sm->process_event(ForceUpdate{});
    } else {
        ESP_LOGD(UPDATE_CHECK_TAG, "No updates available");
        plkbx->sm->process_event(Done{});
    }

    vTaskDelete(nullptr);
}

// Onboarding pairing check — phase 1.
// One-shot check that decides whether the pairing code needs to be shown. Sets
// initNeedsPairing and fires Done, or Error if pairing status is unavailable.
void LKBX::startPairingCheckTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);

    // Offline devices can't pair — skip straight past the pairing prompt.
    if (WiFiClass::status() != WL_CONNECTED) {
        ESP_LOGD(PAIRING_CHECK_TAG, "Offline - skipping pairing check");
        plkbx->initNeedsPairing = false;
        TaskHandles::pairingCheck = nullptr;
        plkbx->sm->process_event(Done{});
        vTaskDelete(nullptr);
        return;
    }

    // MQTT authentication already returned pairing status and saved the QR
    // code. Only query again if that authentication never succeeded.
    if (!plkbx->initPairingStatusKnown) {
        const PairingStatus status = getPairingStatus(3000);
        if (status == PairingStatus::Unknown) {
            ESP_LOGW(PAIRING_CHECK_TAG, "Pairing check unavailable; not treating a network failure as unpaired");
            TaskHandles::pairingCheck = nullptr;
            plkbx->sm->process_event(Error{.error = ErrorT::SERVER_UNREACHABLE});
            vTaskDelete(nullptr);
            return;
        }
        plkbx->initNeedsPairing = status == PairingStatus::Unpaired;
        if (plkbx->initNeedsPairing) {
            // This task owns the HTTP wait, before entering the state machine
            // or pausing the animation. One bounded attempt, then cached QR.
            const AuthResponse response = getAuthToken(3000);
            if (response.valid) {
                persistLockboxAuthToNvs(response);
                if (response.pairingStatusKnown) {
                    plkbx->initNeedsPairing = !response.isPaired;
                }
            }
        }
    }
    ESP_LOGI(PAIRING_CHECK_TAG, "needsPairing: %d", plkbx->initNeedsPairing);

    TaskHandles::pairingCheck = nullptr;
    plkbx->sm->process_event(Done{});
    ESP_LOGD(PAIRING_CHECK_TAG, "Pairing transition complete; stack free minimum: %u bytes",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    vTaskDelete(nullptr);
}

// Onboarding pairing prompt — phase 2.
// One persistent worker serves both QR states. It remains dormant elsewhere,
// avoiding task-handle races when a prompt is exited/re-entered during HTTP.
void LKBX::pollPairingTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);
    struct Prompt {
        LKBX* device;
        String lastVisible;
        bool active() const {
            return device->sm->is("init.preflight.pairingPrompt"_s) || device->sm->is("pairing"_s);
        }
        bool onboarding() const { return device->sm->is("init.preflight.pairingPrompt"_s); }
        bool connected() const { return WiFi.status() == WL_CONNECTED; }
        bool refreshDue() const { return pairingCodeRefreshDue(); }
        void markAttempt() { markPairingCodeRefreshAttempt(); }
        lockbox::security::PairingRefreshResponse authenticate(int timeout) {
            return getPairingCodeRefresh(timeout);
        }
        void accept(const lockbox::security::PairingRefreshResponse& response) {
            // Never reconfigure the MQTT client or restart when refreshing QR.
            if (response.isPaired) markPairingCodePaired();
            else acceptPairingCode(response.pairingCode.c_str(), response.pairingCodeExpiresAt.c_str());
        }
        PairingStatus pairingStatus(int timeout) { return getPairingStatus(timeout); }
        void confirm() { device->sm->process_event(PairingConfirmed{}); }
        void repaintIfChanged() {
            if (!active()) { lastVisible = ""; return; }
            String visible = currentPairingCode();
            if (visible.isEmpty()) visible = pairingCodeIsPaired() ? "[paired]" : "[waiting]";
            if (visible != lastVisible) {
                lastVisible = visible;
                device->sm->process_event(PairingCodeChanged{});
            }
        }
    };
    Prompt prompt{plkbx, ""};
    for (;;) {
        pollPairingPrompt(prompt);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void LKBX::syncLockStateOnBootTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);

    bool offline = isOffline();
    // The pairing endpoint can wait for a network route. Do not call it
    // before resuming a saved session when WiFi is unavailable.
    bool paired = !offline && isPaired();
    ESP_LOGI(SYNC_LOCK_STATE_TAG, "offline: %d, paired: %d", offline, paired);

    if (offline || !paired) {
        ESP_LOGW(SYNC_LOCK_STATE_TAG, "Offline or unpaired - skipping sync");
        bootHttpDone = true;
        plkbx->sm->process_event(Done{});
        vTaskDelete(nullptr);
        return;
    }

    SyncResult result = pullSession();
    ESP_LOGI(SYNC_LOCK_STATE_TAG, "pullSession result: %d",
             static_cast<int>(result));
    switch (result) {
        case SyncResult::PENDING:
            plkbx->sm->process_event(LockOffered{});
            break;
        case SyncResult::LOCKED:
            ESP_LOGI(SYNC_LOCK_STATE_TAG, "Server reports locked session");
            plkbx->sm->process_event(LockTimer{});
            break;
        case SyncResult::BREAK:
            ESP_LOGI(SYNC_LOCK_STATE_TAG, "Server reports active break");
            plkbx->sm->process_event(Break{});
            break;
        case SyncResult::UNLOCKED:
            ESP_LOGI(SYNC_LOCK_STATE_TAG, "Server reports no active session");
            if (isPaddleLocked()) {
                ESP_LOGW(SYNC_LOCK_STATE_TAG,
                         "Paddle physically locked with no session - "
                         "unlocking motor");
                plkbx->motor.unlock();
            }
            plkbx->sm->process_event(Done{});
            break;
        case SyncResult::FAILED:
        case SyncResult::NO_CHANGE:
        default:
            ESP_LOGD(SYNC_LOCK_STATE_TAG,
                     "Sync failed or no change - proceeding to home");
            plkbx->sm->process_event(Done{});
            break;
    }

    bootHttpDone = true;
    vTaskDelete(nullptr);
}

void LKBX::startMqttConnectionTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);

    constexpr int AUTH_MAX_ATTEMPTS = 3;
    constexpr uint32_t AUTH_RETRY_MS = 5000;
    constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 30000;
    constexpr uint32_t MQTT_POLL_MS = 500;

    bool bootPending = true;
    const bool returningDevice = hasFinishedOnboarding();
    AuthResponse response;
    while (!response.valid) {
        if (WiFiClass::status() != WL_CONNECTED) {
            if (bootPending) {
                ESP_LOGI(MQTT_CONNECT_TAG,
                         "Resuming offline; MQTT will connect when WiFi returns");
                bootPending = false;
                plkbx->sm->process_event(Done{});
            }
            vTaskDelay(AUTH_RETRY_MS / portTICK_PERIOD_MS);
            continue;
        }
        bootHttpDone = true;
        for (int attempt = 0; attempt < AUTH_MAX_ATTEMPTS; attempt++) {
            response = getAuthToken();
            if (response.valid) break;
            ESP_LOGW(MQTT_CONNECT_TAG, "Auth attempt %d/%d failed, retrying...",
                     attempt + 1, AUTH_MAX_ATTEMPTS);
            vTaskDelay(AUTH_RETRY_MS / portTICK_PERIOD_MS);
        }
        if (!response.valid && bootPending) {
            bootPending = false;
            plkbx->sm->process_event(Error{.error = ErrorT::MQTT_AUTH_FAILED});
            if (!returningDevice) {
                TaskHandles::mqttConnect = nullptr;
                vTaskDelete(nullptr);
                return;
            }
        }
    }

    vTaskDelay(1);

    persistLockboxAuthToNvs(response);
    plkbx->initNeedsPairing = !response.isPaired;
    plkbx->initPairingStatusKnown = response.pairingStatusKnown;

    static String lwt = createStateMessage(Str::Protocol::DISCONNECTED, false);

    // RAD BLE v1 remains available after provisioning. LKBX has PSRAM and the
    // MQTT client is now expected to coexist with the peripheral service.
    if (NimBLEDevice::isInitialized())
        ESP_LOGI(MQTT_CONNECT_TAG, "Keeping RAD BLE active alongside MQTT");

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.uri = mqtt_server;
    mqtt_cfg.port = static_cast<uint32_t>(mqtt_port);
    mqtt_cfg.client_id = response.clientId.c_str();
    mqtt_cfg.username = response.username.c_str();
    mqtt_cfg.password = response.password.c_str();
    mqtt_cfg.lwt_topic = response.clientId.c_str();
    mqtt_cfg.lwt_msg = lwt.c_str();
    mqtt_cfg.lwt_qos = 2;
    mqtt_cfg.lwt_retain = true;
    mqtt_cfg.keepalive = 15;
    mqtt_cfg.buffer_size = 4096;
    mqtt_cfg.cert_pem = root_ca;

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqttClient ||
        esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY,
                                      mqtt_event_handler, nullptr) != ESP_OK ||
        esp_mqtt_client_start(mqttClient) != ESP_OK) {
        if (mqttClient) esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
        plkbx->sm->process_event(Error{.error = ErrorT::MQTT_AUTH_FAILED});
        vTaskDelete(nullptr);
        return;
    }

    const uint32_t startMs = millis();
    while (!mqttConnected && (millis() - startMs) < MQTT_CONNECT_TIMEOUT_MS) {
        vTaskDelay(MQTT_POLL_MS / portTICK_PERIOD_MS);
    }

    if (bootPending && mqttConnected) {
        ESP_LOGI(MQTT_CONNECT_TAG, "MQTT connected");
        plkbx->sm->process_event(Done{});
    } else {
        if (bootPending) {
            ESP_LOGW(MQTT_CONNECT_TAG, "MQTT connection timed out after %lums",
                     (unsigned long)MQTT_CONNECT_TIMEOUT_MS);
            plkbx->sm->process_event(Error{.error = ErrorT::MQTT_TIMEOUT});
        }
        // The MQTT client retries its connection. Keep local controls running
        // and reconcile the current server session once connectivity returns.
        if (returningDevice || !bootPending) {
            while (!mqttConnected) vTaskDelay(AUTH_RETRY_MS / portTICK_PERIOD_MS);
            updateTime();
            SyncOutcome outcome;
            const SyncResult reconciled = syncState(&outcome);
            if (outcome.freezeChanged) bumpFreezeDisplayVersion();
            ESP_LOGI(MQTT_CONNECT_TAG, "Reconciled: result=%d effect=%d",
                     static_cast<int>(reconciled),
                     static_cast<int>(outcome.effect));
            // Reconnect reconciliation is idempotent: an unchanged session comes
            // back as NONE/TIMER_META and emits nothing, so a flapping link can
            // never replay the locking sequence.
            switch (outcome.effect) {
                case SyncEffect::OFFER:
                    plkbx->sm->process_event(LockOffered{});
                    break;
                case SyncEffect::TIMER_META:
                    plkbx->sm->process_event(FreezeChange{});
                    break;
                case SyncEffect::MOTOR_LOCK:
                    // A genuine new session resumed after a reconnect still
                    // re-runs preflight; an already accepted lock never gets
                    // here (it is classified NONE/TIMER_META).
                    if (isLockedTimer()) plkbx->sm->process_event(LockTimer{});
                    else plkbx->sm->process_event(Lock{});
                    break;
                case SyncEffect::MOTOR_UNLOCK:
                    plkbx->sm->process_event(Unlock{});
                    break;
                case SyncEffect::BREAK:
                    plkbx->sm->process_event(Break{});
                    break;
                case SyncEffect::NONE:
                default:
                    break;
            }
            // The server never recorded the lock this box holds: its accept
            // report was lost. Repeat it; the report carries the box's dates.
            if (outcome.serverMissedLock && isLocked()) {
                ESP_LOGW(MQTT_CONNECT_TAG,
                         "Server has no record of the held lock, re-reporting");
                plkbx->sendMessage(Str::Protocol::LOCKED);
            }
            // Offline expiry can leave the state unchanged by reconciliation.
            // Publish the physical state even when no transition was needed.
            plkbx->sendActivityLog("reconnected");
        }
    }

    TaskHandles::mqttConnect = nullptr;
    vTaskDelete(nullptr);
}

void LKBX::startMqttConnection() {
    ESP_LOGD(TASKDEBUG_TAG, "Creating MQTT connection task");
    xTaskCreatePinnedToCore(startMqttConnectionTask, Tasks::mqttConnect.name,
                            Tasks::mqttConnect.stackSize, this,
                            Tasks::mqttConnect.priority,
                            &TaskHandles::mqttConnect, Tasks::mqttConnect.core);
}

// update methods
void LKBX::startUpdate() {
    xTaskCreatePinnedToCore(
        updateTask, Tasks::update.name, Tasks::update.stackSize, this,
        Tasks::update.priority, &TaskHandles::update, Tasks::update.core);
}

void LKBX::updateTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);

    // Resolve and persist the immutable release before stopping services. If
    // the install fails, the next boot can skip this exact automatic retry and
    // finish startup; a manual retry or a different release remains available.
    if (!prepareSoftwareUpdate()) {
        ESP_LOGW(OTA_TAG, "No safe firmware update is ready to install");
        plkbx->sm->process_event(
            Error{.error = ErrorT::NO_UPDATE_AVAILABLE});
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(OTA_TAG, "=== Pre-OTA cleanup (free heap: %lu) ===",
             (unsigned long)esp_get_free_heap_size());

    if (TaskHandles::hourlyWeb) {
        vTaskDelete(TaskHandles::hourlyWeb);
        TaskHandles::hourlyWeb = nullptr;
        ESP_LOGI(OTA_TAG, "Stopped hourlyWeb task");
    }

    if (TaskHandles::mqttConnect) {
        vTaskDelete(TaskHandles::mqttConnect);
        TaskHandles::mqttConnect = nullptr;
    }
    if (!stopHttpWorker()) {
        ESP_LOGW(OTA_TAG, "HTTP shutdown is still draining; deferring update");
        plkbx->sm->process_event(Error{.error = ErrorT::OTA_FAILED});
        vTaskDelete(nullptr);
        return;
    }

    if (mqttClient) {
        esp_mqtt_client_stop(mqttClient);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
        mqttConnected = false;
        ESP_LOGI(OTA_TAG, "Stopped MQTT client");
    }

    if (NimBLEDevice::isInitialized()) {
        radBleServer.end();
        if (TaskHandles::nimble) {
            vTaskDelete(TaskHandles::nimble);
            TaskHandles::nimble = nullptr;
        }
        NimBLEDevice::deinit(true);
        ESP_LOGI(OTA_TAG, "Stopped NimBLE");
    }

    if (TaskHandles::idle) {
        vTaskDelete(TaskHandles::idle);
        TaskHandles::idle = nullptr;
    }
    if (TaskHandles::headerBar) {
        vTaskDelete(TaskHandles::headerBar);
        TaskHandles::headerBar = nullptr;
    }
#ifdef MEMORY_LOGGING
    if (TaskHandles::taskMonitor) {
        vTaskDelete(TaskHandles::taskMonitor);
        TaskHandles::taskMonitor = nullptr;
    }
#endif

    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP_LOGI(OTA_TAG, "=== Post-cleanup (free heap: %lu) ===",
             (unsigned long)esp_get_free_heap_size());

    if (updateSoftware()) {
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGE(OTA_TAG, "OTA failed (free heap: %lu)",
             (unsigned long)esp_get_free_heap_size());
    plkbx->sm->process_event(Error{.error = ErrorT::OTA_FAILED});
    vTaskDelete(nullptr);
}
