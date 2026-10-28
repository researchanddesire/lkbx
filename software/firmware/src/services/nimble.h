#ifndef LOCKBOX_NIMBLE_H
#define LOCKBOX_NIMBLE_H

#include "utils/trustedClock.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <time.h>
#include <cstring>

#include "constants/LogTags.h"
#include "constants/Pins.h"
#include "constants/strings.h"
#include "services/machine.h"
#include "services/rad_ble.h"
#include "state/LKBX.h"
#include "structs/Version.h"
#include "utils/ble.h"

static constexpr const char* LKBX_SERVICE_UUID = Str::Ble::SERVICE_UUID;
static constexpr const char* LKBX_CHAR_UUID = Str::Ble::CHAR_UUID;

static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pLkbxChar = nullptr;

struct BLEMessage {
    uint8_t type;
    size_t length;
    char raw[128];
};

static QueueHandle_t bleQueue = nullptr;

class MyCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pCharacteristic,
                NimBLEConnInfo& /* connInfo */) override {
        // Format: "AA:BB:CC:DD:EE:FF;ESP32-S3;<wifiConnected>;<md5>;<version>"
        uint8_t wifiConnected = (WiFiClass::status() == WL_CONNECTED) ? 1 : 0;
        char deviceInfo[128];
        snprintf(deviceInfo, sizeof(deviceInfo), "%s;%s;%u;%s;%s",
                 getMacAddresss().c_str(), ESP.getChipModel(), wifiConnected,
                 ESP.getSketchMD5().c_str(), currentVersionString);
        pCharacteristic->setValue(deviceInfo);
    }

    void onWrite(NimBLECharacteristic* pCharacteristic,
                 NimBLEConnInfo& /* connInfo */) override {
        const auto rawValue = pCharacteristic->getValue();
        const std::string_view value(rawValue.c_str(), rawValue.length());
        if (!lockbox::security::validLegacyMessage(value)) {
            ESP_LOGW(NIMBLE_TAG, "Rejected malformed BLE command");
            return;
        }

        // Use a fixed buffer because the FreeRTOS queue copies its contents.
        // Parsing above rejects oversized and embedded-NUL input before copying.
        BLEMessage msg{};
        msg.type = static_cast<uint8_t>(value[0] - '0');
        msg.length = value.size();
        memcpy(msg.raw, value.data(), value.size());
        msg.raw[value.size()] = '\0';
        if (!bleQueue || xQueueSend(bleQueue, &msg, 0) != pdTRUE) {
            ESP_LOGW(NIMBLE_TAG, "BLE command queue unavailable");
            return;
        }
        setNotIdle(Str::Input::NIMBLE_WRITE);
    }
};

class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* /* pServer */,
                   NimBLEConnInfo& connInfo) override {
        radBleServer.onConnect(connInfo.getConnHandle());
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo,
                      int /* reason */) override {
        radBleServer.onDisconnect(connInfo.getConnHandle());
        // Restart advertising so the user can retry without rebooting
        if (!isBlePaused()) pServer->startAdvertising();
    }
};

static void initNimble(LKBX* lkbx) {
    bleQueue = xQueueCreate(8, sizeof(BLEMessage));

    static String deviceName = radble::loadDeviceName(Str::Ble::DEVICE_NAME);
    NimBLEDevice::init(deviceName.c_str());
    NimBLEDevice::setMTU(512);
    NimBLEDevice::setSecurityAuth(false, false, false);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    NimBLEService* pLkbxService = pServer->createService(LKBX_SERVICE_UUID);
    pLkbxChar = pLkbxService->createCharacteristic(
        LKBX_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::NOTIFY);

    pLkbxChar->setCallbacks(new MyCallbacks());

    if (!initRadBle(pServer)) {
        ESP_LOGE(NIMBLE_TAG, "Universal RAD BLE service failed to initialize");
    }

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName(deviceName.c_str());
    pAdvertising->addServiceUUID(pLkbxService->getUUID());
    pAdvertising->enableScanResponse(true);
    pAdvertising->start();
}

// FreeRTOS task — drains the BLE message queue every 50ms.
// Uses the global lkbx pointer directly. initBoard() creates this task before
// lkbx is constructed, so pvParameters would be nullptr — hence the global.
void nimbleTask(void* /* param */) {
    BLEMessage msg;

    while (true) {
        while (bleQueue && xQueueReceive(bleQueue, &msg, 0) == pdTRUE) {
            // lkbx is initialized after this task starts — skip until ready
            if (!lkbx || !lkbx->sm) continue;

            // Validate again at the consumer boundary; callers that directly
            // enqueue commands must not bypass parsing or supply a false type.
            std::string_view value;
            if (!lockbox::security::legacyQueuePayload(msg.raw, sizeof(msg.raw),
                                                       msg.length, value) ||
                msg.type != static_cast<uint8_t>(value[0] - '0')) {
                ESP_LOGW(NIMBLE_TAG, "Rejected malformed queued BLE command");
                continue;
            }

            if (msg.type == 9) {
                ParsedWiFiCredentials parsed;
                if (!parseWifiCredentials(value, parsed)) continue;
                WiFiSync wifiCredentials{};
                wifiCredentials.ssid = String(parsed.ssid.data(), parsed.ssid.size());
                wifiCredentials.password = String(parsed.password.data(), parsed.password.size());
                if (wifiCredentials.ssid.length() != parsed.ssid.size() ||
                    wifiCredentials.password.length() != parsed.password.size()) {
                    ESP_LOGW(NIMBLE_TAG, "Could not allocate BLE WiFi credentials");
                    continue;
                }
                lkbx->sm->process_event(wifiCredentials);
                continue;
            }

            SmartLockboxState result;
            if (!parseSmartLockboxState(value, result) ||
                !lockbox::security::representableLegacyTime<time_t>(result.currentDate)) {
                ESP_LOGW(NIMBLE_TAG, "Rejected invalid BLE clock or lock command");
                continue;
            }

            struct timeval tv{};
            tv.tv_sec = result.currentDate / 1000;
            tv.tv_usec = (result.currentDate % 1000) * 1000;
            if (lockboxSetTimeOfDay(&tv, nullptr) != 0) {
                ESP_LOGW(NIMBLE_TAG, "Could not apply BLE clock update");
                continue;
            }

            if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) ==
                pdTRUE) {
                preferences.begin(Keys::base, false);
                preferences.putULong64(Keys::Base::lastUpdated,
                                       result.currentDate);
                preferences.end();

                preferences.begin(Keys::session, false);
                preferences.putULong64(Keys::Session::lockEnd, result.lockEnd);
                preferences.end();
                xSemaphoreGive(preferencesMutex);
            }

            if (result.operation == lockbox::security::LegacyOperation::Unlock) {
                ESP_LOGD(NIMBLE_TAG, "Unlocking because lockState is 0");
                lkbx->sm->process_event(Unlock{});
            } else if (result.operation == lockbox::security::LegacyOperation::Lock) {
                ESP_LOGD(NIMBLE_TAG, "Locking because lockState is 1");
                lkbx->sm->process_event(Lock{});
            } else if (result.operation == lockbox::security::LegacyOperation::LockTimer) {
                ESP_LOGD(NIMBLE_TAG, "Locking because lockState is 2");
                lkbx->sm->process_event(LockTimer{
                    .lockEnd = result.lockEnd,
                });
            }
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

#endif  // LOCKBOX_NIMBLE_H
