#include "LKBX.h"

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include "constants/dashboard.h"
#include "services/deviceKeyProvisioning.h"
#include "services/http_worker.h"
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
        applyAuthCommandKey(response.commandKey);
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

    // Apply what a state pull changed. Reconciliation is idempotent: an
    // unchanged session comes back as NONE/TIMER_META and emits nothing, so a
    // flapping link can never replay the locking sequence.
    void applyReconciledEffect(LKBX *plkbx, SyncEffect effect) {
        switch (effect) {
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
    if (!startTaskWithRetry(startWifiConnectionTask, Tasks::wifiConnect, this,
                            &TaskHandles::wifiConnect)) {
        // Nothing would ever end this boot step. Report it as a network that
        // could not be reached: a returning box resumes its saved session
        // offline, a new one gets the Wi-Fi error page with Retry.
        sm->process_event(Error{.error = ErrorT::WIFI_NETWORK_UNREACHABLE});
    }
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
    constexpr uint32_t AUTH_REJECTED_RETRY_MS = 60000;
    constexpr uint32_t AUTH_RATE_LIMITED_RETRY_MS = 30000;
    constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 30000;
    constexpr uint32_t MQTT_POLL_MS = 500;
    constexpr int MQTT_START_ATTEMPTS = 10;
    constexpr int MQTT_START_ATTEMPTS_RETURNING = 3;
    constexpr uint32_t MQTT_START_RETRY_MS = 2000;

    bool bootPending = true;
    const bool returningDevice = hasFinishedOnboarding();

    // Pre-auth: a chip without its v2 key asks for one and burns it. Any
    // failure leaves the device signing in with v1. Only first setup shows
    // E-LOCK-999; an onboarded device carries on as before.
    const DeviceKeyOutcome keyOutcome = prepareDeviceKey();
    if (keyOutcome == DeviceKeyOutcome::failed && !returningDevice) {
        plkbx->sm->process_event(Error{.error = ErrorT::DEVICE_KEY_FAILED});
        TaskHandles::mqttConnect = nullptr;
        vTaskDelete(nullptr);
        return;
    }

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
            if (response.valid || response.rejected || response.rateLimited)
                break;
            ESP_LOGW(MQTT_CONNECT_TAG, "Auth attempt %d/%d failed, retrying...",
                     attempt + 1, AUTH_MAX_ATTEMPTS);
            vTaskDelay(AUTH_RETRY_MS / portTICK_PERIOD_MS);
        }
        if (!response.valid && bootPending) {
            bootPending = false;
            plkbx->sm->process_event(Error{
                .error = response.rateLimited ? ErrorT::AUTH_RATE_LIMITED
                                              : ErrorT::DASHBOARD_SIGN_IN_FAILED});
            if (!returningDevice) {
                TaskHandles::mqttConnect = nullptr;
                vTaskDelete(nullptr);
                return;
            }
        }
        // A refused identity will not change within seconds. Local operation
        // continues as it does for any other failed authentication.
        if (response.rejected) {
            vTaskDelay(AUTH_REJECTED_RETRY_MS / portTICK_PERIOD_MS);
        }
        // Wait out the dashboard's rate-limit window before trying again.
        if (response.rateLimited) {
            vTaskDelay(AUTH_RATE_LIMITED_RETRY_MS / portTICK_PERIOD_MS);
        }
    }

    // A new box is asked to sign in first: that v1 sign-in enrolled its
    // secret, so it asks once more now and, once the key is burned, signs in
    // again with v2 before it connects. Any failure keeps the v1 sign-in for
    // this boot; the next boot signs in with the burned key.
    if (keyOutcome == DeviceKeyOutcome::afterSignIn) {
        const DeviceKeyOutcome second = prepareDeviceKey();
        if (second == DeviceKeyOutcome::ready) {
            AuthResponse signedIn = getAuthToken();
            if (signedIn.valid) {
                response = signedIn;
            } else {
                ESP_LOGW(MQTT_CONNECT_TAG,
                         "v2 sign-in after the key burn failed; v1 for this boot");
            }
        } else if (second == DeviceKeyOutcome::failed && !returningDevice) {
            plkbx->sm->process_event(Error{.error = ErrorT::DEVICE_KEY_FAILED});
            TaskHandles::mqttConnect = nullptr;
            vTaskDelete(nullptr);
            return;
        }
    }

    vTaskDelay(1);

    persistLockboxAuthToNvs(response);
    plkbx->initNeedsPairing = !response.isPaired;
    plkbx->initPairingStatusKnown = response.pairingStatusKnown;

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
    // The will itself is built before every connection attempt, sealed and
    // unique to that connection (MQTT_EVENT_BEFORE_CONNECT, utils/lastWill.h).
    // The client is created without one.
    mqtt_cfg.lwt_qos = 2;
    mqtt_cfg.lwt_retain = true;
    mqtt_cfg.keepalive = 15;
    // Above SPIRAM_MALLOC_ALWAYSINTERNAL (4096), so both message buffers come
    // from PSRAM and leave internal RAM for the client task stack and TLS.
    mqtt_cfg.buffer_size = 8 * 1024;
    mqtt_cfg.cert_pem = root_ca;
    // Commands are opened (signature check, decryption, JSON) and reports
    // sealed on the MQTT task: the 6 KB default overflowed once a box held a
    // command key. The stack comes from internal RAM, which TLS and AES DMA
    // also need; 12 KB left none at boot on staging hardware.
    mqtt_cfg.task_stack = 10 * 1024;

    // A sign-in repeated from an error page: the client of the attempt before
    // must not stay behind, running or waiting to be started.
    if (mqttClient != nullptr) destroyMqtt();

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    // The broker names the will by the client id, as it always has.
    rememberMqttSettings(mqtt_cfg, response.clientId.c_str());
    bool mqttStarted = false;
    const bool mqttReady =
        mqttClient &&
        esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY,
                                       mqtt_event_handler, nullptr) == ESP_OK;
    if (mqttReady) {
        // The client task needs one contiguous 10 KB block of internal RAM,
        // which boot-time HTTPS can hold for a few seconds. A box that has
        // been set up waits only briefly: it carries on to its saved session
        // while the client is started in the background (below). First setup
        // waits longer, because there the next page is an error page.
        const int attempts =
            returningDevice ? MQTT_START_ATTEMPTS_RETURNING : MQTT_START_ATTEMPTS;
        for (int attempt = 0; attempt < attempts && !mqttStarted; ++attempt) {
            mqttStarted = esp_mqtt_client_start(mqttClient) == ESP_OK;
            if (!mqttStarted) vTaskDelay(MQTT_START_RETRY_MS / portTICK_PERIOD_MS);
        }
    }
    if (!mqttStarted) {
        if (mqttReady) {
            // The task still does not fit (2026-11-16 rig run on R8: ten
            // attempts over 20 s, then off the broker until a restart). Keep
            // the client: the activity log task starts it every two seconds
            // until it fits, as it does after the Wi-Fi setup portal, and the
            // box then reads its state once. This task ends here, which gives
            // back its own 8.3 KB stack for the client's.
            ESP_LOGW(MQTT_CONNECT_TAG,
                     "MQTT client task does not fit yet (largest free "
                     "internal block %u bytes, %u bytes free); starting it "
                     "in the background",
                     static_cast<unsigned>(heap_caps_get_largest_free_block(
                         MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(
                         heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
            retryMqttStartLater();
        } else {
            destroyMqtt();
        }
        TaskHandles::mqttConnect = nullptr;
        // Only the boot step waits for this. A box that came online after
        // boot is on some other page, and the error is not about that page.
        if (bootPending) {
            plkbx->sm->process_event(Error{.error = ErrorT::MQTT_AUTH_FAILED});
        }
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
            applyReconciledEffect(plkbx, outcome.effect);
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
    if (!startTaskWithRetry(startMqttConnectionTask, Tasks::mqttConnect, this,
                            &TaskHandles::mqttConnect)) {
        // Nothing would ever end this boot step. Report it as a broker that
        // did not answer: a returning box carries on to its saved session
        // (off the broker until it restarts), a new one gets the error page
        // with Retry.
        TaskHandles::mqttConnect = nullptr;
        sm->process_event(Error{.error = ErrorT::MQTT_TIMEOUT});
    }
}

// The state read (utils/stateRead.h). A clear without a session id comes from
// a dashboard older than this firmware: the box unlocks only if the
// dashboard's own state says it has no active session, and any other answer
// keeps the session it holds. The same read follows every MQTT connection
// after a drop or a stop (the Wi-Fi setup portal, network recovery), a sync
// that names another session, and an answer from Chaster that does not know
// the session.
//
// One pass of the delay task; never waits for the network. The read used to
// run on a task of its own (confirmClear, 8.3 KB stack), which after a
// reconnect could not be created 5 times in 6. Here it needs no task, and it
// is tried until one read has completed.
namespace {
    // The request at the HTTP worker, until its answer has been taken. Only
    // the delay task touches it.
    HttpPending *stateReadPending = nullptr;
}  // namespace

void LKBX::serviceStateRead() {
    if (stateReadPending != nullptr) {
        int status = -1;
        String payload;
        if (!takeHttpResult(stateReadPending, status, &payload)) return;

        // A local session is the box's own: an answer that was on its way
        // when the wearer started one must not end it.
        if (isLocalPlay()) {
            stateReadPlan.finished(true, millis());
            ESP_LOGI(MQTT_TAG, "State read dropped: a local session started");
            return;
        }

        SyncOutcome outcome;
        const SyncResult result =
            applySessionResponse(status, payload, &outcome);
        const bool completed = result != SyncResult::FAILED;
        stateReadPlan.finished(completed, millis());
        if (!completed) {
            ESP_LOGW(MQTT_TAG,
                     "State read failed (HTTP %d, %u in a row); trying again",
                     status, static_cast<unsigned>(stateReadPlan.failures()));
            return;
        }
        if (outcome.freezeChanged) bumpFreezeDisplayVersion();
        ESP_LOGI(MQTT_TAG, "Confirmed clear: result=%d effect=%d",
                 static_cast<int>(result), static_cast<int>(outcome.effect));
        // The read now follows every reconnect, so it can land right after
        // the box has ended a break, before the dashboard has heard of it.
        // That stale "break" must not open the box again: the same rule as
        // for a break that arrives by MQTT.
        if (outcome.effect == SyncEffect::BREAK && serverBreakOnCooldown()) {
            ESP_LOGW(MQTT_TAG,
                     "Ignoring the break in the state read -- within "
                     "post-break cooldown");
        } else {
            applyReconciledEffect(this, outcome.effect);
        }
        // The server never recorded the lock this box holds: its accept
        // report was lost. Repeat it; the report carries the box's dates.
        if (outcome.serverMissedLock && isLocked()) {
            ESP_LOGW(MQTT_TAG,
                     "Server has no record of the held lock, re-reporting");
            sendMessage(Str::Protocol::LOCKED);
        }
        // This stack is shared with the Done dispatches and the Chaster
        // question. Its low-water mark goes to the log for the rig.
        ESP_LOGI(MQTT_TAG,
                 "State read on the delay task: at least %u bytes of its "
                 "stack have stayed free",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        return;
    }

    const uint32_t now = millis();
    if (!stateReadPlan.due(now)) return;
    stateReadPlan.begin(now);

    // Nothing to read for a session the dashboard does not manage.
    if (isLocalPlay()) {
        stateReadPlan.finished(true, now);
        ESP_LOGI(MQTT_TAG, "State read skipped: local session");
        return;
    }
    // Offline there is nobody to ask; the read stays wanted.
    if (WiFiClass::status() != WL_CONNECTED) {
        stateReadPlan.finished(false, now);
        return;
    }

    stateReadPending = startSessionPull(stateRead::kTimeoutMs);
    if (stateReadPending == nullptr) {
        // The HTTP worker's queue is full, or there is no memory for the
        // request. Tried again like a read that failed.
        stateReadPlan.finished(false, now);
        ESP_LOGW(MQTT_TAG,
                 "Could not send the state read (%u in a row): largest free "
                 "internal block %u bytes, %u bytes free",
                 static_cast<unsigned>(stateReadPlan.failures()),
                 static_cast<unsigned>(
                     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        return;
    }
    stateReadPlan.sent();
}

// update methods
void LKBX::startUpdate() {
    if (!startTaskWithRetry(updateTask, Tasks::update, this,
                            &TaskHandles::update)) {
        // The Updating page has no buttons, and a locked box can be sent here
        // (forceUpdate), so it must not wait for a task that does not exist.
        // Leave through the "no update" page: any button returns home, which
        // restores a lock. Restarting instead (as a failed install does) could
        // loop on a forced update while memory stays short.
        TaskHandles::update = nullptr;
        ESP_LOGE(OTA_TAG, "Could not start the update; carrying on without it");
        sm->process_event(Error{.error = ErrorT::NO_UPDATE_AVAILABLE});
    }
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
        destroyMqtt();
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
