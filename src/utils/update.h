#ifndef SOFTWARE_UPDATE_H
#define SOFTWARE_UPDATE_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "FirmwareUpdateRuntime.h"
#include "constants/PerferenceKeys.h"
#include "constants/Version.h"
#include "services/settings.h"
#include "utils/getEfuseMac.h"

#include "../constants/LogTags.h"

#ifndef FIRMWARE_TRACK
#define FIRMWARE_TRACK "main"
#endif

#ifndef FIRMWARE_BUILD_SHA
#define FIRMWARE_BUILD_SHA "unknown"
#endif

namespace {

firmware::Decision pendingFirmwareDecision;
bool pendingFirmwareDecisionValid = false;
bool pendingFirmwareAttemptRecorded = false;

std::string runningFirmwareHash() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    std::uint8_t digest[32] = {};
    if (running == nullptr ||
        esp_partition_get_sha256(running, digest) != ESP_OK) {
        return {};
    }
    return firmware::sha256Hex(digest);
}

std::uint32_t physicalFlashSize() {
    std::uint32_t size = 0;
    if (esp_flash_get_physical_size(nullptr, &size) != ESP_OK || size == 0) {
        size = ESP.getFlashChipSize();
    }
    return size;
}

std::uint32_t nextOtaSlotSize() {
    const esp_partition_t *partition =
        esp_ota_get_next_update_partition(nullptr);
    return partition == nullptr ? 0 : partition->size;
}

bool recordPendingFirmwareAttempt() {
    if (pendingFirmwareAttemptRecorded) return true;
    if (!pendingFirmwareDecisionValid ||
        !pendingFirmwareDecision.shouldUpdate ||
        pendingFirmwareDecision.releaseId.empty()) {
        return false;
    }

    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) != pdTRUE) return false;
    bool stored = false;
    if (preferences.begin(Keys::base, false)) {
        const String releaseId = pendingFirmwareDecision.releaseId.c_str();
        const String previous =
            preferences.getString(Keys::Base::otaFailRelease, "");
        const std::uint32_t previousCount =
            preferences.getUInt(Keys::Base::otaFailCount, 0);
        const std::uint32_t nextCount =
            previous == releaseId ? previousCount + 1 : 1;
        const String targetVersion =
            pendingFirmwareDecision.nextHopVersion.c_str();
        const String assignedTrack =
            pendingFirmwareDecision.assignedTrack.c_str();
        stored = preferences.putString(Keys::Base::otaFailRelease, releaseId) >
                     0 &&
                 preferences.putString(Keys::Base::otaFailVersion,
                                       targetVersion) > 0 &&
                 preferences.putString(Keys::Base::otaFailTrack,
                                       assignedTrack) > 0 &&
                 preferences.putBool(Keys::Base::otaInstalled, false) > 0;
        if (stored) {
            if (pendingFirmwareDecision.buildSha.empty()) {
                preferences.remove(Keys::Base::otaFailBuild);
            } else {
                stored =
                    preferences.putString(
                        Keys::Base::otaFailBuild,
                        pendingFirmwareDecision.buildSha.c_str()) > 0;
            }
        }
        if (stored) {
            preferences.putUInt(Keys::Base::otaFailCount, nextCount);
            ESP_LOGI(OTA_TAG,
                     "Recorded OTA attempt release=%s track=%s version=%s attempt=%lu",
                     releaseId.c_str(),
                     assignedTrack.c_str(),
                     targetVersion.c_str(),
                     static_cast<unsigned long>(nextCount));
        }
        preferences.end();
    }
    xSemaphoreGive(preferencesMutex);
    pendingFirmwareAttemptRecorded = stored;
    return stored;
}

void clearFailedFirmwareRelease() {
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) != pdTRUE) return;
    if (preferences.begin(Keys::base, false)) {
        preferences.remove(Keys::Base::otaFailRelease);
        preferences.remove(Keys::Base::otaFailCount);
        preferences.remove(Keys::Base::otaFailBuild);
        preferences.remove(Keys::Base::otaFailVersion);
        preferences.remove(Keys::Base::otaFailTrack);
        preferences.remove(Keys::Base::otaInstalled);
        preferences.end();
    }
    xSemaphoreGive(preferencesMutex);
    pendingFirmwareAttemptRecorded = false;
}

void markPendingFirmwareInstalled() {
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(OTA_TAG,
                 "Unable to record completed OTA write; retry guard remains active");
        return;
    }
    bool stored = false;
    if (preferences.begin(Keys::base, false)) {
        stored = preferences.putBool(Keys::Base::otaInstalled, true) > 0;
        preferences.end();
    }
    xSemaphoreGive(preferencesMutex);
    if (!stored) {
        ESP_LOGW(OTA_TAG,
                 "Unable to record completed OTA write; retry guard remains active");
    }
}

void clearCompletedFirmwareAttemptIfRunning() {
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) != pdTRUE) return;
    bool shouldClear = false;
    if (preferences.begin(Keys::base, false)) {
        const bool installed =
            preferences.getBool(Keys::Base::otaInstalled, false);
        const String expectedBuild =
            preferences.getString(Keys::Base::otaFailBuild, "");
        const String expectedVersion =
            preferences.getString(Keys::Base::otaFailVersion, "");
        const String expectedTrack =
            preferences.getString(Keys::Base::otaFailTrack, "");
        shouldClear =
            installed &&
            firmware::runningReleaseMatchesAttempt(
                expectedTrack.c_str(), FIRMWARE_TRACK,
                expectedVersion.c_str(), VERSION, expectedBuild.c_str(),
                FIRMWARE_BUILD_SHA);
        if (installed && !shouldClear) {
            ESP_LOGW(OTA_TAG,
                     "Running build does not match completed OTA attempt; retry guard remains active");
        }
        preferences.end();
    }
    xSemaphoreGive(preferencesMutex);
    if (shouldClear) {
        ESP_LOGI(OTA_TAG,
                 "Healthy boot matches completed OTA attempt; clearing retry guard");
        clearFailedFirmwareRelease();
    }
}

bool automaticUpdateIsSuppressed() {
    if (!pendingFirmwareDecisionValid ||
        !pendingFirmwareDecision.shouldUpdate ||
        pendingFirmwareDecision.releaseId.empty()) {
        return false;
    }

    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) != pdTRUE) return true;
    bool suppressed = false;
    if (preferences.begin(Keys::base, false)) {
        const String failedRelease =
            preferences.getString(Keys::Base::otaFailRelease, "");
        const String offeredRelease = pendingFirmwareDecision.releaseId.c_str();
        if (failedRelease == offeredRelease) {
            const std::uint32_t attempts =
                preferences.getUInt(Keys::Base::otaFailCount, 1);
            ESP_LOGW(OTA_TAG,
                     "Skipping automatic retry for failed release=%s attempts=%lu; manual retry remains available",
                     offeredRelease.c_str(),
                     static_cast<unsigned long>(attempts));
            suppressed = true;
        } else if (!failedRelease.isEmpty()) {
            // A new immutable release is a safe new automatic attempt.
            preferences.remove(Keys::Base::otaFailRelease);
            preferences.remove(Keys::Base::otaFailCount);
            preferences.remove(Keys::Base::otaFailBuild);
            preferences.remove(Keys::Base::otaFailVersion);
            preferences.remove(Keys::Base::otaFailTrack);
            preferences.remove(Keys::Base::otaInstalled);
        }
        preferences.end();
    }
    xSemaphoreGive(preferencesMutex);
    return suppressed;
}

firmware::DeviceReport currentFirmwareReport() {
    firmware::DeviceReport report;
    report.deviceType = "lkbx";
    report.deviceId = getMacAddresss().c_str();
    report.reportedTrack = FIRMWARE_TRACK;
    report.currentVersion = VERSION;
    report.currentBuild = FIRMWARE_BUILD_SHA;
    report.firmwareHash = runningFirmwareHash();
    report.chip = ESP.getChipModel();
    report.chipRevision = ESP.getChipRevision();
    report.chipCores = ESP.getChipCores();
#if CONFIG_SPIRAM_MODE_QUAD
    report.hardwareRevision = "esp32-s3-n16r2";
#elif CONFIG_SPIRAM_MODE_OCT
    report.hardwareRevision = "esp32-s3-n16r8";
#else
    report.hardwareRevision = "esp32-s3-psram-disabled";
#endif
    report.flashSizeBytes = physicalFlashSize();
    report.otaSlotSizeBytes = nextOtaSlotSize();
    report.psramSizeBytes = ESP.getPsramSize();
    report.partitionLayout = "lkbx-v1";
    firmware::provenance::reconcile(report);
    return report;
}

bool checkDatabaseForUpdate() {
    pendingFirmwareDecisionValid = false;
    if (WiFi.status() != WL_CONNECTED) return false;

    String error;
    const firmware::DeviceReport report = currentFirmwareReport();
    if (!firmware::postCheck(RAD_SERVER, report, pendingFirmwareDecision,
                             error)) {
        ESP_LOGW(UPDATE_TAG, "Firmware check failed: %s", error.c_str());
        return false;
    }
    firmware::provenance::observeCurrent(report, pendingFirmwareDecision);
    firmware::provenance::stageUpdate(report, pendingFirmwareDecision);
    ESP_LOGI(UPDATE_TAG,
             "Firmware tracks reported=%s assigned=%s update=%s reason=%s target=%s hop=%s release=%s build=%s",
             pendingFirmwareDecision.reportedTrack.c_str(),
             pendingFirmwareDecision.assignedTrack.c_str(),
             pendingFirmwareDecision.shouldUpdate ? "true" : "false",
             pendingFirmwareDecision.reason.c_str(),
             pendingFirmwareDecision.targetVersion.c_str(),
             pendingFirmwareDecision.nextHopVersion.c_str(),
             pendingFirmwareDecision.releaseId.c_str(),
             pendingFirmwareDecision.buildSha.c_str());
    pendingFirmwareDecisionValid = true;
    return pendingFirmwareDecision.shouldUpdate;
}

}  // namespace

static auto isUpdateAvailable = []() { return checkDatabaseForUpdate(); };

static auto shouldForceUpdate = []() {
#ifdef NO_UPDATE
    return false;
#else
    return isUpdateAvailable() && !automaticUpdateIsSuppressed();
#endif
};

inline bool prepareSoftwareUpdate() {
    if ((!pendingFirmwareDecisionValid ||
         !pendingFirmwareDecision.shouldUpdate) &&
        !checkDatabaseForUpdate()) {
        return false;
    }
    if (!recordPendingFirmwareAttempt()) {
        ESP_LOGE(OTA_TAG,
                 "Refusing OTA because the failure-loop guard could not be persisted");
        return false;
    }
    return true;
}

inline bool updateSoftware() {
    if (!prepareSoftwareUpdate()) return false;

    String error;
    if (!firmware::installApplicationAndFilesystem(pendingFirmwareDecision,
                                                   error)) {
        ESP_LOGE(OTA_TAG, "OTA failed: %s", error.c_str());
        return false;
    }

    // Keep the retry marker across reboot. It is cleared only after the new
    // version/build completes the server health check, so a bootloader fallback
    // to the old image cannot immediately retry the same broken release.
    markPendingFirmwareInstalled();
    ESP_LOGI(OTA_TAG, "Firmware hop installed; rebooting");
    delay(250);
    ESP.restart();
    return true;
}

inline bool confirmRunningFirmwareIfPending() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr) {
        ESP_LOGW(OTA_TAG, "Unable to identify running OTA partition");
        return false;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t stateResult = esp_ota_get_state_partition(running, &state);
    if (stateResult == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGD(OTA_TAG, "OTA rollback state tracking is not enabled");
        return true;
    }
    if (stateResult != ESP_OK) {
        ESP_LOGW(OTA_TAG, "Unable to read running OTA state: %s",
                 esp_err_to_name(stateResult));
        return false;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return true;

    const esp_err_t confirmResult = esp_ota_mark_app_valid_cancel_rollback();
    if (confirmResult != ESP_OK) {
        ESP_LOGE(OTA_TAG, "Unable to confirm running OTA image: %s",
                 esp_err_to_name(confirmResult));
        return false;
    }
    ESP_LOGI(OTA_TAG, "Confirmed running OTA image after server health check");
    return true;
}

inline void finalizeHealthyFirmwareBoot() {
    if (confirmRunningFirmwareIfPending()) {
        clearCompletedFirmwareAttemptIfRunning();
    }
}

#endif  // SOFTWARE_UPDATE_H
