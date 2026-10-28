#ifndef LOCKBOX_NTP_H
#define LOCKBOX_NTP_H

#include "utils/trustedClock.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <freertos/task.h>

#include "constants/LogTags.h"
#include "constants/strings.h"
#include "machine.h"
#include "serverOnline.h"
#include "tasks/TaskParams.h"
#include "utils/clockCorrection.h"
#include "utils/syncState.h"

// SNTP supplies a bounded TLS calendar hint only. The strong sntp_sync_time
// hook stores it without advancing the application clock. Only a successful
// verified HTTPS response can make that date authoritative for lock deadlines.
static void updateTime() {
    const NetworkTimeTransaction transaction;
    if (!transaction) return;
    clearNetworkTimeHint();
    configTime(0, 0, Str::Ntp::POOL, Str::Ntp::FALLBACK);
    for (int attempt = 0; attempt < 10 && !networkTimeHintMs(); ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    sntp_stop();
    beginNtpClockCorrectionWindow();
    if (!beginTlsClockBootstrap(networkTimeHintMs())) return;
    uint64_t verifiedTime = 0;
    const bool verified = getAuthenticatedServerTimeMs(&verifiedTime);
    if (finishTlsClockBootstrap(verified ? verifiedTime : 0)) {
        isTimeSynced = true;
        recordNtpClockCorrection("HTTPS_DATE");
        processPendingLocalTimerClockCorrections();
    } else {
        ESP_LOGW(NTP_TASK_TAG, "Authenticated time unavailable; retained previous clock");
    }
}

[[noreturn]] static void hourlyWebTasks(void* pvParameters) {
    // TODO: checking if we actually need this.
    vTaskDelete(nullptr);
    return;
}

[[noreturn]] static void printSystemTimeTask(void* pvParameters) {
    String timeStr;
    while (true) {
        // Store the updated time in preferences
        struct timeval tv{};
        lockboxGetTimeOfDay(&tv, nullptr);
        if (tv.tv_sec > 1704092400) {  // If year is greater than 2024

            // TODO: Restore this - it's crashing.
            //            preferences.begin("lockbox", false);
            //            preferences.putULong64("lastUpdated", tv.tv_sec);
            //            preferences.end();
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

static void initNTP() {
    // Set initial time from preferences if available
    auto lastUpdated = 0ULL;

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        lastUpdated = preferences.getULong64(Keys::Base::lastUpdated, 0);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(NTP_TASK_TAG, "Last updated time: %llu", lastUpdated);
    if (!isWallClockSet() && lastUpdated > 1704092400 &&
        lastUpdated < 1704092400000) {
        struct timeval tv{};
        tv.tv_sec = lastUpdated;
        tv.tv_usec = 0;
        lockboxSetTimeOfDay(&tv, nullptr);

        String timeStr = ctime(reinterpret_cast<const time_t*>(&lastUpdated));
        ESP_LOGI(NTP_TASK_TAG, "Setting time from preferences: %s",
                 timeStr.c_str());
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating hourly web tasks task");
    xTaskCreatePinnedToCore(hourlyWebTasks, Tasks::hourlyWeb.name,
                            Tasks::hourlyWeb.stackSize, nullptr,
                            Tasks::hourlyWeb.priority, &TaskHandles::hourlyWeb,
                            Tasks::hourlyWeb.core);
}

#endif  // LOCKBOX_NTP_H
