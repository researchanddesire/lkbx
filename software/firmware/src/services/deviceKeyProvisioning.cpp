#include "deviceKeyProvisioning.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_log.h>
#include <mbedtls/platform_util.h>

#include "DeviceKey.h"
#include "constants/LogTags.h"
#include "constants/PerferenceKeys.h"
#include "constants/Pins.h"
#include "constants/Version.h"
#include "constants/dashboard.h"
#include "constants/strings.h"
#include "services/deviceIdentity.h"
#include "services/http_worker.h"
#include "services/ina.h"
#include "services/serverOnline.h"
#include "services/settings.h"
#include "utils/getEfuseMac.h"
#include "utils/trustedClock.h"

namespace {
using lockbox::security::DeviceKeyFailure;
using lockbox::security::ProvisionAnswer;

constexpr int PROVISION_TIMEOUT_MS = 8000;
// eFuse programming needs a steady supply: USB power, or a battery well above
// the brown-out range. Same threshold the BLE OTA safety check uses.
constexpr float MIN_BURN_BATTERY_VOLTS = 3.5f;

bool attemptedThisBoot = false;

struct KeyRequestRecord {
    bool refused = false;
    uint64_t retryAtMs = 0;
    int failure = 0;
};

KeyRequestRecord loadRecord() {
    KeyRequestRecord record;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) {
        record.refused = true;  // Unknown: do not ask this boot.
        return record;
    }
    preferences.begin(Keys::identity, true);
    record.refused = preferences.getBool(Keys::Identity::keyRequestRefused, false);
    record.retryAtMs = preferences.getULong64(Keys::Identity::keyRequestRetryAt, 0);
    record.failure = preferences.getInt(Keys::Identity::keyFailure, 0);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    return record;
}

template <typename Write>
void saveRecord(Write write) {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return;
    preferences.begin(Keys::identity, false);
    write();
    preferences.end();
    xSemaphoreGive(preferencesMutex);
}

bool trustedNowMs(uint64_t& nowMs) {
    if (!isTimeSynced.load(std::memory_order_acquire)) return false;
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    nowMs = static_cast<uint64_t>(tv.tv_sec) * 1000 +
            static_cast<uint64_t>(tv.tv_usec) / 1000;
    return true;
}

bool powerSafeForBurn() {
    if (digitalRead(Pins::Power::usbDetect) == HIGH) return true;
    return inaReady && ina.getBusVoltage_V() >= MIN_BURN_BATTERY_VOLTS;
}

void wipe(String& text) {
    for (unsigned int index = 0; index < text.length(); ++index) {
        text.setCharAt(index, '\0');
    }
    text = String();
}

void failWith(DeviceKeyFailure failure) {
    const int code = lockbox::security::failureCode(failure);
    ESP_LOGE(IDENTITY_TAG, "E-LOCK-999: device key not stored (%d)", code);
    saveRecord([code] { preferences.putInt(Keys::Identity::keyFailure, code); });
}
}  // namespace

DeviceKeyOutcome prepareDeviceKey() {
    if (deviceKeyState() == DeviceKeyState::present) return DeviceKeyOutcome::ready;
    if (attemptedThisBoot || WiFiClass::status() != WL_CONNECTED) {
        return DeviceKeyOutcome::unavailable;
    }

    const KeyRequestRecord record = loadRecord();
    uint64_t nowMs = 0;
    const bool clockTrusted = trustedNowMs(nowMs);
    if (!lockbox::security::mayRequestDeviceKey(record.refused, record.retryAtMs,
                                                nowMs, clockTrusted)) {
        return DeviceKeyOutcome::unavailable;
    }
    // Every check that could refuse the burn runs before the request, so a
    // key is only ever requested when it can be stored at once.
    if (!runningImageConfirmed() || !deviceKeySlotAvailable() || !powerSafeForBurn()) {
        ESP_LOGI(IDENTITY_TAG, "Device key request deferred to a later boot");
        return DeviceKeyOutcome::unavailable;
    }
    attemptedThisBoot = true;

    JsonDocument request;
    request[Str::Json::MAC] = getMacAddresss();
    request[Str::Json::CHIP] = ESP.getChipModel();
    request[Str::Json::MD5] = ESP.getSketchMD5();
    request[Str::Json::DEVICE] = Str::Api::DEVICE_NAME;
    request[Str::Json::VERSION_KEY] = VERSION;
    // Binds the request to the v1 identity: an enrolled device must present
    // it, and only it may replace a key that never reached the eFuses.
    const String deviceSecret = deviceSecretForRequest();
    if (deviceSecret.length() > 0) request[Str::Json::DEVICE_SECRET] = deviceSecret;
    if (record.failure > 0) request[Str::Json::FAILURE_CODE] = record.failure;

    String payload;
    const int status = post(String(RAD_SERVER) + Str::Api::LOCKBOX_V2_PROVISION,
                            request, &payload, PROVISION_TIMEOUT_MS);
    switch (lockbox::security::provisionAnswer(status)) {
        case ProvisionAnswer::neverAgain:
            ESP_LOGW(IDENTITY_TAG, "Dashboard will not issue a device key (%d)", status);
            saveRecord([] { preferences.putBool(Keys::Identity::keyRequestRefused, true); });
            return DeviceKeyOutcome::unavailable;
        case ProvisionAnswer::retryLater:
            ESP_LOGW(IDENTITY_TAG, "Device key request rate limited");
            if (clockTrusted) {
                const uint64_t retryAt = nowMs + lockbox::security::PROVISION_RETRY_MS;
                saveRecord([retryAt] {
                    preferences.putULong64(Keys::Identity::keyRequestRetryAt, retryAt);
                });
            }
            return DeviceKeyOutcome::unavailable;
        case ProvisionAnswer::nextBoot:
            ESP_LOGI(IDENTITY_TAG, "No device key this boot (%d)", status);
            return DeviceKeyOutcome::unavailable;
        case ProvisionAnswer::burn:
            break;
    }

    uint8_t key[lockbox::security::DEVICE_KEY_BYTES];
    bool parsed = false;
    {
        JsonDocument response;
        parsed = !deserializeJson(response, payload) &&
                 lockbox::security::provisionedDeviceKey(
                     response.as<JsonVariantConst>(), key);
    }
    wipe(payload);
    if (!parsed) {
        failWith(DeviceKeyFailure::invalidKey);
        return DeviceKeyOutcome::failed;
    }

    const DeviceKeyFailure result = burnDeviceKey(key);
    mbedtls_platform_zeroize(key, sizeof(key));
    if (result != DeviceKeyFailure::none) {
        failWith(result);
        return DeviceKeyOutcome::failed;
    }
    saveRecord([] {
        preferences.remove(Keys::Identity::keyFailure);
        preferences.remove(Keys::Identity::keyRequestRetryAt);
    });
    return DeviceKeyOutcome::ready;
}
