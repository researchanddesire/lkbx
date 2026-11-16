#ifndef LOCKBOX_INITMQTT_H
#define LOCKBOX_INITMQTT_H

#include "utils/trustedClock.h"
#include <WiFi.h>
#include <esp_log.h>
#include <mqtt_client.h>

#include "FirmwareProvenance.h"
#include "MqttFrame.h"

#include "constants/LogTags.h"
#include "constants/strings.h"
#include "services/deviceIdentity.h"
#include "services/getId.h"
#include "services/hardwareHealth.h"
#include "services/machine.h"
#include "services/tlsTurn.h"
#include "state/LKBX.h"
#include "structs/Version.h"
#include "utils/dashboard.hpp"
#include "utils/emergencyReport.h"
#include "utils/emergencyStore.h"
#include "utils/heldSession.h"
#include "utils/lastWill.h"
#include "utils/lockedReport.h"
#include "utils/serverClear.h"

// TLS CA certificates for MQTT broker verification
#ifdef VERSIONDEV
const char* root_ca = LKBX_DEV_CA_PEM;
#else
const char* root_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
    "MrY=\n"
    "-----END CERTIFICATE-----";
#endif

// "AA:BB:CC:DD:EE:FF" after a v1 sign-in, "v2/<32 hex device id>" after v2.
static char mqttTopic[40] = "";
static lockbox::security::MqttFrame<> mqttFrame;

void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                        int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    String mqtt_data_string;
    JsonDocument doc;
    ParsedMessage message;
    // Whether the held session shows its timer (MQTT_EVENT_DATA only).
    bool timerDisplayed = true;

    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            // From here until MQTT_EVENT_DISCONNECTED the client is
            // connecting or connected (services/mqtt.cpp).
            noteMqttLink(true);
            // One secure connection is set up at a time (utils/tlsGate.h):
            // while an HTTPS request is opening its connection the client
            // waits here, a few seconds at most, and then connects anyway. A
            // stop of the client ends the wait at once. The turn is given
            // back when the client has connected or the attempt has failed
            // (the two events below); one that is never given back runs out.
            waitForTlsTurn(TlsUser::MQTT, tlsGate::kMqttWaitMs,
                           [] { return mqttStopRequested(); });
            // Every connection attempt gets its own last will: a new seal for
            // a box that signs, a new id otherwise. Built once per boot, the
            // dashboard saw the second disconnect of a boot as a repeat of the
            // first, and refused the plain will of a box that signs.
            applyLastWill(buildLastWill(mqttClientId()));
            break;
        case MQTT_EVENT_CONNECTED:
            hardwareHealthMqttConnected(mqtt_server);
            Serial.println(Str::Protocol::MQTT_CONNECTED);
            strncpy(mqttTopic, cloudTopic().c_str(), sizeof(mqttTopic) - 1);
            esp_mqtt_client_subscribe(mqttClient, mqttTopic, 2);
            mqttConnected = true;
            // An unconfirmed LOCKED report gets a fresh retry budget, and so
            // does an emergency report that is still owed.
            lockedReport::tracker.onConnected();
            emergencyReport::owed.onConnected();
            {
                const auto token = firmware::provenance::currentToken();
                JsonDocument provenance;
                provenance["provenanceCapability"] = 1;
                provenance["provenance"] = token;
                provenance["provenanceId"] =
                    firmware::provenance::currentTokenId();
                provenance["imageSha256"] =
                    firmware::provenance::runningImageSha256();
                provenance["track"] = FIRMWARE_TRACK;
                provenance["version"] = VERSION;
                provenance["buildSha"] = FIRMWARE_BUILD_SHA;
                String payload;
                serializeJson(provenance, payload);
                const String topic = String(mqttTopic) + "/firmware";
                esp_mqtt_client_publish(mqttClient, topic.c_str(),
                                        payload.c_str(), payload.length(), 1,
                                        true);
            }
            // Connected. The turn is kept a moment longer: the subscription,
            // the provenance record and any waiting reports go out now, and
            // an HTTPS connection set up on top of them is what ran R8 out
            // of memory.
            endTlsTurnAfter(TlsUser::MQTT, tlsGate::kMqttSettleMs);
            break;
        case MQTT_EVENT_DISCONNECTED:
            // Also how a failed connection attempt ends.
            endTlsTurn(TlsUser::MQTT);
            noteMqttLink(false);
            mqttFrame.reset();
            Serial.println(Str::Protocol::MQTT_DISCONNECTED);
            mqttConnected = false;
            break;
        case MQTT_EVENT_PUBLISHED:
            // QoS 2 completes the full handshake before this fires, so the
            // broker really has the message.
            lockedReport::tracker.onPublished(event->msg_id);
            emergencyReport::owed.onPublished(event->msg_id);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            Serial.println(Str::Protocol::MQTT_SUBSCRIBED);
            break;
        case MQTT_EVENT_DATA:
            if (mqttFrame.append(mqttTopic, event->topic, event->topic_len,
                                 event->data, event->data_len,
                                 event->total_data_len, event->current_data_offset,
                                 event->retain) !=
                lockbox::security::FrameResult::complete) break;
            if (!openCloudCommand(mqttTopic, mqttFrame.data(), mqttFrame.size(),
                                  mqtt_data_string)) {
                mqttFrame.reset();
                break;
            }
            mqttFrame.reset();
            message = parseJsonMessage(mqtt_data_string, doc);

            if (!message.valid) {
                ESP_LOGE(MQTT_TAG, "Invalid JSON message");
                break;
            }

            // A message lights the screen, except a sync while the session
            // hides its timer: that one waits until it is known to change the
            // page (syncWakesScreen), so a silent change of the end date
            // cannot say the end is near.
            timerDisplayed = isTimerDisplayed();
            if (timerDisplayed || message.type != MessageType::SYNC) {
                setNotIdle(Str::Input::MQTT_DATA);
            }

            if (message.type == MessageType::SYNC) {
                auto isLocalPlay = false;
                String savedSessionId;
                String savedLockState;

                if (xSemaphoreTake(preferencesMutex,
                                   portTICK_PERIOD_MS * 1000) != pdTRUE) {
                    // Without the held session the sync cannot be checked
                    // against it, so the state is read from the dashboard.
                    ESP_LOGW(MQTT_TAG, "Could not read the held session -- asking the dashboard");
                    lkbx->confirmServerClear();
                    return;
                }
                preferences.begin(Keys::base);
                isLocalPlay = preferences.getBool(Keys::Base::isLocalPlay, false);
                preferences.end();
                preferences.begin(Keys::session);
                savedSessionId = preferences.getString(Keys::Session::sessionId, "");
                savedLockState = preferences.getString(Keys::Session::lockState, "");
                preferences.end();
                xSemaphoreGive(preferencesMutex);

                const String incomingSessionId = doc[Str::Json::SESSION_ID].as<String>();
                if (isLocalPlay && (incomingSessionId.isEmpty() || incomingSessionId == savedSessionId)) {
                    ESP_LOGD(MQTT_TAG, "Local play is enabled. Skipping sync");
                    return;
                }

                if (doc[Str::Json::CLEAR].as<bool>()) {
                    switch (classifyServerClear(doc[Str::Json::SESSION_ID] | "",
                                                savedSessionId.c_str())) {
                        case ServerClear::APPLY: {
                            // The dashboard ended the session the box holds.
                            // It also does that in answer to the box's own
                            // emergency report, and the lockout outlives
                            // that clear (utils/emergencyRecord.h).
                            bool recordRead = false;
                            const bool lockoutStays =
                                emergencyLockoutOutlastsClear(recordRead);
                            if (!recordRead) {
                                ESP_LOGW(MQTT_TAG,
                                         "Could not read the emergency record "
                                         "-- asking the dashboard");
                                lkbx->confirmServerClear();
                                break;
                            }
                            clearState();
                            if (!lockoutStays) {
                                lkbx->sm->process_event(Unlock{});
                            }
                            break;
                        }
                        case ServerClear::CONFIRM:
                            ESP_LOGW(MQTT_TAG,
                                     "Clear names no session -- asking the "
                                     "dashboard");
                            lkbx->confirmServerClear();
                            break;
                        case ServerClear::IGNORE:
                            ESP_LOGW(MQTT_TAG,
                                     "Ignoring a clear for another session");
                            break;
                    }
                    return;
                }

                // A locked session only ends through its own messages. A sync
                // for any other session is checked with the dashboard first.
                if (!isLocalPlay &&
                    classifyHeldSessionSync(doc[Str::Json::SESSION_ID] | "",
                                            savedSessionId.c_str(),
                                            savedLockState.c_str()) ==
                        HeldSessionSync::CONFIRM) {
                    ESP_LOGW(MQTT_TAG,
                             "Sync names another session while one is locked "
                             "-- asking the dashboard");
                    lkbx->confirmServerClear();
                    return;
                }

                // MQTT duration is seconds; HTTP pull duration is already ms.
                // A pending session is an offer, never an automatic lock.
                SyncMessage session = {
                    .isBreak = doc[Str::Json::IS_BREAK].as<bool>(),
                    .lockEnd = doc[Str::Json::LOCK_END].as<uint64_t>(),
                    .breakMax = doc[Str::Json::BREAK_MAX].as<uint64_t>(),
                    .breakReg = doc[Str::Json::BREAK_REG].as<uint64_t>(),
                    .breakLast = doc[Str::Json::BREAK_LAST].as<uint64_t>(),
                    .duration = doc[Str::Json::DURATION].as<uint64_t>() * 1000ULL,
                    .isEUnlock = doc[Str::Json::IS_E_UNLOCK].as<bool>(),
                    .lockStart = doc[Str::Json::LOCK_START].as<uint64_t>(),
                    .lockState = doc[Str::Json::LOCK_STATE].as<String>(),
                    .sessionId = doc[Str::Json::SESSION_ID].as<String>(),
                    .lockName = doc[Str::Json::LOCK_NAME] | "",
                    .requestedBy = doc[Str::Json::REQUESTED_BY] | "",
                    .isTestLock = doc[Str::Json::IS_TEST_LOCK].as<bool>(),
                    .isTimeDisp = doc[Str::Json::IS_TIME_DISP].as<bool>(),
                    .displayElapsedTime = doc[Str::Json::DISPLAY_ELAPSED_TIME].as<bool>(),
                };
                applyFreezeSync(doc, session);
                applyChasterSync(doc, session);
                session.breakKeyholderOnly =
                    doc[Str::Json::BREAK_KEYHOLDER_ONLY] | false;

                SyncOutcome outcome;
                saveState(session, &outcome);
                if (outcome.freezeChanged) bumpFreezeDisplayVersion();
                // The sync may have revealed the timer; read it again.
                if (!timerDisplayed &&
                    syncWakesScreen(outcome.effect, isTimerDisplayed())) {
                    setNotIdle(Str::Input::MQTT_DATA);
                }

                // Metadata changes reconcile the main display only. Identical
                // syncs emit no event and no acknowledgement echo.
                if (outcome.effect == SyncEffect::TIMER_META) {
                    lkbx->sm->process_event(FreezeChange{});
                } else if (outcome.effect == SyncEffect::OFFER) {
                    lkbx->sm->process_event(LockOffered{});
                } else if (outcome.effect == SyncEffect::BREAK) {
                    // Ignore a server-pushed break that lands right after the
                    // device ended one (the stale-"break" echo).
                    if (serverBreakOnCooldown()) {
                        ESP_LOGW(MQTT_TAG,
                                 "Ignoring server break -- within post-break "
                                 "cooldown");
                    } else {
                        lkbx->sm->process_event(Break{});
                    }
                } else if (outcome.effect == SyncEffect::MOTOR_UNLOCK) {
                    lkbx->sm->process_event(Unlock{});
                } else if (outcome.effect == SyncEffect::MOTOR_LOCK) {
                    // A timed lock routes through the countdown, a permanent
                    // lock through the locked page; both re-run preflight.
                    if (session.lockEnd != 0) {
                        lkbx->sm->process_event(LockTimer{});
                    } else {
                        lkbx->sm->process_event(Lock{});
                    }
                }

                return;
            }

            if (message.type == MessageType::REBOOT) {
                ESP_LOGI(MQTT_TAG,
                         "Reboot requested — clearing isPaired and restarting");
                if (xSemaphoreTake(preferencesMutex,
                                   portTICK_PERIOD_MS * 1000) == pdTRUE) {
                    preferences.begin(Keys::base, false);
                    preferences.putBool(Keys::Base::isPaired, false);
                    preferences.end();
                    xSemaphoreGive(preferencesMutex);
                }
                esp_restart();
                break;
            }

            ESP_LOGE(MQTT_TAG, "Invalid message type");
            break;
        default:
            break;
    }
}

#endif  // LOCKBOX_INITMQTT_H
