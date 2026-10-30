#ifndef LOCKBOX_DASHBOARD_HPP
#define LOCKBOX_DASHBOARD_HPP



#include <Arduino.h>
#include <ArduinoJson.h>
#include "CloudInput.h"

#include "constants/LogTags.h"
#include "constants/dashboard.h"
#include "constants/strings.h"
#include "getEfuseMac.h"
#include "pairingStatus.h"
#include "services/deviceIdentity.h"
#include "services/wm.h"

struct AuthResponse {
    String clientId = "";
    String username = "";
    String password = "";
    String sessionId = "";
    String pairingCode = "";
    String pairingCodeExpiresAt = "";
    int hueShift = 0;
    bool isPaired = false;
    bool pairingStatusKnown = false;
    bool valid = false;
    String commandKey = "";
    bool rejected = false;
};

static PairingStatus getPairingStatus(int timeoutMs = 5000) {
    String deviceId = getMacAddresss();
    JsonDocument doc;
    doc[Str::Json::MAC_ADDRESS] = deviceId;
    const String deviceSecret = deviceSecretForRequest();
    if (deviceSecret.length() > 0) doc[Str::Json::DEVICE_SECRET] = deviceSecret;

    String payload;

    int httpResponseCode =
        post(String(RAD_SERVER) + Str::Api::LOCKBOX_IS_PAIRED, doc, &payload,
             timeoutMs);
    const PairingStatus status = pairingStatusFromHttp(httpResponseCode);
    if (status != PairingStatus::Paired)
        return status;

    // A paired response only counts when it names this device.
    JsonDocument response;
    const bool namesThisDevice =
        !deserializeJson(response, payload) &&
        lockbox::security::boundedString(response["macAddress"], 17, 17) &&
        response["macAddress"].as<String>().equalsIgnoreCase(deviceId);
    return namesThisDevice ? PairingStatus::Paired : PairingStatus::Unknown;
}

static bool isPaired() {
    return getPairingStatus() == PairingStatus::Paired;
}

static void populateAuthRequest(JsonDocument& doc) {
    doc[Str::Json::MAC] = getMacAddresss();
    doc[Str::Json::CHIP] = ESP.getChipModel();
    doc[Str::Json::MD5] = ESP.getSketchMD5();
    doc[Str::Json::DEVICE] = Str::Api::DEVICE_NAME;
    const String deviceSecret = deviceSecretForRequest();
    if (deviceSecret.length() > 0) doc[Str::Json::DEVICE_SECRET] = deviceSecret;
}

// Web must deploy this additive purpose before the firmware rollout. The early
// branch changes only an existing unowned device's code/expiry: it does not
// register firmware, count boots or allocate/reconfigure broker credentials.
static lockbox::security::PairingRefreshResponse getPairingCodeRefresh(int timeoutMs = 3000) {
    JsonDocument request;
    populateAuthRequest(request);
    request["purpose"] = "pairing_refresh";
    String payload;
    if (post(RAD_AUTH, request, &payload, timeoutMs) != 200) return {};
    JsonDocument response;
    if (deserializeJson(response, payload)) return {};
    return lockbox::security::pairingRefreshResponse(response.as<JsonVariantConst>());
}

// Request device-scoped runtime broker credentials from the configured API.
static AuthResponse getAuthToken(int timeoutMs = 5000) {
    JsonDocument doc;
    populateAuthRequest(doc);

    String requestBody;
    serializeJson(doc, requestBody);

    String payload;
    int httpResponseCode = post(RAD_AUTH, requestBody, &payload, timeoutMs);

    ESP_LOGD(UTILS_TAG, "HTTP Response code: %d", httpResponseCode);

    // if the error code is 403 then send the 'unpaired' event and return
    // nothing.
    if (httpResponseCode == 403) {
        ESP_LOGD(UTILS_TAG, "Device is not paired. Sending unpaired event.");
        return {
            .clientId = "", .username = "", .password = "", .sessionId = ""};
    }

    // The server refused this device's identity. This is the same failed
    // authentication as any other: it changes no lock, session or pairing state.
    if (httpResponseCode == 401) {
        ESP_LOGW(UTILS_TAG, "Server rejected this device's identity");
        AuthResponse rejected;
        rejected.rejected = true;
        return rejected;
    }

    if (httpResponseCode != 200) {
        ESP_LOGW(UTILS_TAG, "Authentication request failed with HTTP status %d",
                 httpResponseCode);
        return {};
    }

    // JSON Parse the response
    JsonDocument response;
    const DeserializationError parseError = deserializeJson(response, payload);
    if (parseError) {
        ESP_LOGW(UTILS_TAG, "Authentication response was not valid JSON: %s",
                 parseError.c_str());
        return {};
    }


    if (!lockbox::security::authResponse(response.as<JsonVariantConst>())) {
        ESP_LOGW(UTILS_TAG, "Authentication response has invalid fields");
        return {};
    }

    return {.clientId = response[Str::Json::CLIENT_ID],
            .username = response[Str::Json::USERNAME],
            .password = response[Str::Json::PASSWORD],
            .sessionId = response[Str::Json::SESSION_ID],
            .pairingCode = response[Str::Json::PAIRING_CODE],
            .pairingCodeExpiresAt = response["pairingCodeExpiresAt"] | "",
            .hueShift = response[Str::Json::HUE_SHIFT],
            .isPaired = response[Str::Json::IS_PAIRED] | false,
            .pairingStatusKnown = response[Str::Json::IS_PAIRED].is<bool>(),
            .valid = true,
            .commandKey = response[Str::Json::COMMAND_KEY] | ""};
}

#endif  // LOCKBOX_DASHBOARD_HPP
