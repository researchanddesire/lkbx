#ifndef LOCKBOX_DASHBOARD_HPP
#define LOCKBOX_DASHBOARD_HPP



#include <Arduino.h>
#include <ArduinoJson.h>
#include "CloudInput.h"
#include "DeviceKey.h"

#include "constants/LogTags.h"
#include "constants/Version.h"
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
    String deviceId = "";
    bool rejected = false;
    bool rateLimited = false;
    // A v2 sign-in found no v2 for this key here (404, or 401 unknown_device).
    bool v2Unavailable = false;
};

static PairingStatus getPairingStatus(int timeoutMs = 5000) {
    String deviceId = getMacAddresss();
    JsonDocument doc;
    String url = String(RAD_SERVER) + Str::Api::LOCKBOX_IS_PAIRED;
    const String token = signedInWithV2() ? deviceTokenForRequest() : String();
    if (token.length() > 0) {
        doc[Str::Json::DEVICE_TOKEN] = token;
        url = String(RAD_SERVER) + Str::Api::LOCKBOX_V2_IS_PAIRED;
    } else {
        doc[Str::Json::MAC_ADDRESS] = deviceId;
        const String deviceSecret = deviceSecretForRequest();
        if (deviceSecret.length() > 0) doc[Str::Json::DEVICE_SECRET] = deviceSecret;
    }

    String payload;

    int httpResponseCode = post(url, doc, &payload, timeoutMs);
    const PairingStatus status = pairingStatusFromHttp(httpResponseCode);
    if (status != PairingStatus::Paired)
        return status;

    // A paired response only counts when it names this device: by its device
    // id after a v2 sign-in, or by its MAC from a dashboard that sends none.
    JsonDocument response;
    if (deserializeJson(response, payload)) return PairingStatus::Unknown;
    if (token.length() > 0 && !response["deviceId"].isNull()) {
        return response["deviceId"].is<const char*>() &&
                       deviceIdForRequest() == response["deviceId"].as<const char*>()
                   ? PairingStatus::Paired
                   : PairingStatus::Unknown;
    }
    const bool namesThisDevice =
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
// The v2 sign-in names the device only by its eFuse token, never its MAC.
static void populateV2AuthRequest(JsonDocument& doc, const String& token) {
    doc[Str::Json::DEVICE_TOKEN] = token;
    doc[Str::Json::CHIP] = ESP.getChipModel();
    doc[Str::Json::MD5] = ESP.getSketchMD5();
    doc[Str::Json::DEVICE] = Str::Api::DEVICE_NAME;
    doc[Str::Json::VERSION_KEY] = VERSION;
}

static lockbox::security::PairingRefreshResponse getPairingCodeRefresh(int timeoutMs = 3000) {
    JsonDocument request;
    String url = RAD_AUTH;
    const String token = signedInWithV2() ? deviceTokenForRequest() : String();
    if (token.length() > 0) {
        populateV2AuthRequest(request, token);
        url = String(RAD_SERVER) + Str::Api::LOCKBOX_V2_AUTH;
    } else {
        populateAuthRequest(request);
    }
    request["purpose"] = "pairing_refresh";
    String payload;
    if (post(url, request, &payload, timeoutMs) != 200) return {};
    JsonDocument response;
    if (deserializeJson(response, payload)) return {};
    return lockbox::security::pairingRefreshResponse(response.as<JsonVariantConst>());
}

// Sends one sign-in request. v1 and v2 answer with the same response shape.
static AuthResponse requestSignIn(const String& url, const JsonDocument& doc,
                                  int timeoutMs) {
    String requestBody;
    serializeJson(doc, requestBody);

    String payload;
    int httpResponseCode = post(url, requestBody, &payload, timeoutMs);

    ESP_LOGD(UTILS_TAG, "HTTP Response code: %d", httpResponseCode);

    JsonDocument errorBody;
    const bool v2Unavailable =
        (httpResponseCode == 404 || httpResponseCode == 401) &&
        !deserializeJson(errorBody, payload) &&
        lockbox::security::v2SignInFallsBackToV1(httpResponseCode,
                                                 errorBody.as<JsonVariantConst>());
    if (httpResponseCode == 404) {
        AuthResponse missing;
        missing.v2Unavailable = true;
        return missing;
    }

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
        rejected.v2Unavailable = v2Unavailable;
        return rejected;
    }

    // The dashboard's firewall limits authentication requests per network.
    // Retrying at the normal pace only keeps the network over that limit.
    if (httpResponseCode == 429) {
        ESP_LOGW(UTILS_TAG, "Authentication request was rate limited");
        AuthResponse limited;
        limited.rateLimited = true;
        return limited;
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
            .commandKey = response[Str::Json::COMMAND_KEY] | "",
            .deviceId = response["deviceId"] | ""};
}

// Request device-scoped runtime broker credentials from the configured API. A
// chip with a burned key signs in with its token. Only a dashboard without v2
// (404) or without this key (401 unknown_device) sends it to v1 for this boot;
// any other failure is retried on v2, so an outage cannot turn signing off.
// The deadline covers queue time, the TLS handshake and the dashboard's reply,
// which took 3.5 s on staging from Asia (2026-11-04); 5 s timed sign-ins out.
static AuthResponse getAuthToken(int timeoutMs = 15000) {
    const String token = deviceTokenForRequest();
    if (token.length() > 0) {
        JsonDocument doc;
        populateV2AuthRequest(doc, token);
        AuthResponse signedIn = requestSignIn(
            String(RAD_SERVER) + Str::Api::LOCKBOX_V2_AUTH, doc, timeoutMs);
        if (signedIn.valid) {
            // The answer must name this chip's device id and keep signing on.
            const String deviceId = deviceIdForRequest();
            if (deviceId.length() == 0 || signedIn.deviceId != deviceId ||
                signedIn.commandKey.length() == 0) {
                ESP_LOGW(UTILS_TAG, "v2 sign-in did not name this device");
                noteSignIn(false);
                return {};
            }
            noteSignIn(true);
            return signedIn;
        }
        if (!signedIn.v2Unavailable) {
            noteSignIn(false);
            return signedIn;
        }
        ESP_LOGW(UTILS_TAG, "Dashboard has no v2 identity for this key, signing in with v1");
    }
    noteSignIn(false);
    JsonDocument doc;
    populateAuthRequest(doc);
    return requestSignIn(RAD_AUTH, doc, timeoutMs);
}

#endif  // LOCKBOX_DASHBOARD_HPP
