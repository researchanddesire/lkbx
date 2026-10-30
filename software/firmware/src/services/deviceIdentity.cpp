#include "deviceIdentity.h"

#include <WiFi.h>
#include <esp_efuse.h>
#include <esp_hmac.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_random.h>

#include <mutex>
#include <string>

#include "Base64Url.h"
#include "CommandEnvelope.h"
#include "constants/LogTags.h"
#include "constants/PerferenceKeys.h"
#include "services/settings.h"
#include "utils/getEfuseMac.h"

namespace {
constexpr size_t SECRET_BYTES = 32;
constexpr size_t SECRET_TEXT_LENGTH = 43;

std::mutex identityMutex;
String cachedSecret;
lockbox::security::CommandGate commandGate;
bool commandGateLoaded = false;

bool validSecretText(const String& text) {
    return text.length() == SECRET_TEXT_LENGTH &&
           lockbox::security::isBase64UrlOfLength(text.c_str(), text.length(),
                                                  SECRET_BYTES);
}

// A build that can still roll back must not enrol this device, or the older
// image it returns to would be locked out of its own identity.
bool runningImageConfirmed() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) return false;
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t result = esp_ota_get_state_partition(running, &state);
    if (result == ESP_ERR_NOT_SUPPORTED || result == ESP_ERR_NOT_FOUND) return true;
    return result == ESP_OK && state != ESP_OTA_IMG_PENDING_VERIFY;
}

enum class EfuseSecret { absent, derived, failed };

// Provisioned units derive the secret from a read-protected eFuse key, so it
// survives an NVS erase. Firmware only reads eFuses and never burns one.
EfuseSecret deriveEfuseSecret(String& secret) {
    esp_efuse_block_t block = EFUSE_BLK_KEY_MAX;
    if (!esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, &block)) {
        return EfuseSecret::absent;
    }
    const String message = String("lkbx-device-secret-v1:") + getMacAddresss();
    uint8_t digest[SECRET_BYTES];
    if (esp_hmac_calculate(static_cast<hmac_key_id_t>(block - EFUSE_BLK_KEY0),
                           message.c_str(), message.length(), digest) != ESP_OK) {
        return EfuseSecret::failed;
    }
    const std::string text = lockbox::security::base64UrlEncode(digest, sizeof(digest));
    secret = String(text.c_str());
    return EfuseSecret::derived;
}

// The check and create run under one lock so concurrent first requests agree.
// A secret that did not reach flash is discarded: using it would strand the
// device behind an identity it forgets on its next boot.
String loadOrCreateStoredSecret() {
    String secret;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) {
        return secret;
    }
    preferences.begin(Keys::identity, false);
    if (preferences.isKey(Keys::Identity::secret)) {
        secret = preferences.getString(Keys::Identity::secret, "");
    }
    if (!validSecretText(secret)) {
        secret = "";
        // esp_fill_random is a true random source only while the radio is on.
        if (WiFiClass::status() == WL_CONNECTED) {
            uint8_t random[SECRET_BYTES];
            esp_fill_random(random, sizeof(random));
            const std::string text =
                lockbox::security::base64UrlEncode(random, sizeof(random));
            preferences.putString(Keys::Identity::secret, text.c_str());
            const String stored = preferences.getString(Keys::Identity::secret, "");
            if (stored == text.c_str()) secret = stored;
        }
    }
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    return secret;
}

void loadCommandGate() {
    {
        std::lock_guard<std::mutex> lock(identityMutex);
        if (commandGateLoaded) return;
    }
    String key;
    uint64_t lastTimestamp = 0;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return;
    preferences.begin(Keys::identity, false);
    if (preferences.isKey(Keys::Identity::commandKey)) {
        key = preferences.getString(Keys::Identity::commandKey, "");
    }
    lastTimestamp = preferences.getULong64(Keys::Identity::commandLastTimestamp, 0);
    preferences.end();
    xSemaphoreGive(preferencesMutex);

    std::lock_guard<std::mutex> lock(identityMutex);
    if (commandGateLoaded) return;
    commandGate.restoreLastTimestamp(lastTimestamp);
    // A corrupt stored key is treated as no key until the next authentication
    // replaces it, matching a device the server has not yet enrolled.
    if (key.length() > 0) commandGate.setKey(key.c_str(), key.length());
    commandGateLoaded = true;
}

void persistLastTimestamp(uint64_t timestamp) {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return;
    preferences.begin(Keys::identity, false);
    preferences.putULong64(Keys::Identity::commandLastTimestamp, timestamp);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
}

const char* rejectionReason(lockbox::security::CommandGateResult result) {
    using lockbox::security::CommandGateResult;
    switch (result) {
        case CommandGateResult::malformed: return "not a valid envelope";
        case CommandGateResult::badSignature: return "bad signature";
        case CommandGateResult::stale: return "stale";
        case CommandGateResult::replayed: return "replayed";
        case CommandGateResult::decryptFailed: return "undecryptable";
        default: return "rejected";
    }
}
}  // namespace

String deviceSecretForRequest() {
    if (!runningImageConfirmed()) return String();
    {
        std::lock_guard<std::mutex> lock(identityMutex);
        if (cachedSecret.length() > 0) return cachedSecret;
    }
    String secret;
    switch (deriveEfuseSecret(secret)) {
        case EfuseSecret::derived:
            break;
        case EfuseSecret::failed:
            // Never fall back to a stored secret: the identity must not flip
            // between the two sources on a transient hardware failure.
            ESP_LOGW(IDENTITY_TAG, "Could not read the eFuse device identity");
            return String();
        case EfuseSecret::absent:
            secret = loadOrCreateStoredSecret();
            break;
    }
    if (secret.length() == 0) return secret;
    std::lock_guard<std::mutex> lock(identityMutex);
    cachedSecret = secret;
    return cachedSecret;
}

void applyAuthCommandKey(const String& commandKey) {
    uint64_t lastTimestamp = 0;
    bool timestampKnown = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::identity, false);
        lastTimestamp = preferences.getULong64(Keys::Identity::commandLastTimestamp, 0);
        timestampKnown = true;
        const bool stored = preferences.isKey(Keys::Identity::commandKey);
        if (commandKey.length() == 0) {
            if (stored) preferences.remove(Keys::Identity::commandKey);
        } else if (!stored ||
                   preferences.getString(Keys::Identity::commandKey, "") != commandKey) {
            preferences.putString(Keys::Identity::commandKey, commandKey);
        }
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    std::lock_guard<std::mutex> lock(identityMutex);
    if (!commandGateLoaded && timestampKnown) {
        commandGate.restoreLastTimestamp(lastTimestamp);
    }
    if (commandKey.length() == 0) {
        commandGate.clearKey();
    } else if (!commandGate.setKey(commandKey.c_str(), commandKey.length())) {
        // authResponse validated this already; fail closed if that ever differs.
        commandGate.clearKey();
    }
    commandGateLoaded = true;
}

bool openCloudCommand(const char* mac, const char* frame, size_t size,
                      String& command) {
    loadCommandGate();
    std::string opened;
    lockbox::security::CommandGateResult result;
    uint64_t before = 0, after = 0;
    {
        std::lock_guard<std::mutex> lock(identityMutex);
        // Without knowing whether a key is held, plain JSON cannot be trusted.
        if (!commandGateLoaded) return false;
        before = commandGate.lastTimestamp();
        result = commandGate.open(mac, frame, size, opened);
        after = commandGate.lastTimestamp();
    }
    if (!lockbox::security::commandGateAccepted(result)) {
        ESP_LOGW(IDENTITY_TAG, "Ignoring MQTT command: %s", rejectionReason(result));
        return false;
    }
    if (after != before) persistLastTimestamp(after);
    command = String(opened.data(), opened.size());
    return command.length() == opened.size();
}
