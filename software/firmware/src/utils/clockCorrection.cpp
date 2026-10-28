#include "utils/trustedClock.h"
#include "utils/clockCorrection.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "LockboxDuration.h"

#include "constants/LogTags.h"
#include "constants/PerferenceKeys.h"
#include "services/settings.h"

namespace {

struct ClockCorrectionEvent {
    uint32_t id = 0;
    int64_t jumpMs = 0;
    int64_t eventMonotonicMs = 0;
    const char* source = "";
};

static constexpr uint8_t kMaxCorrectionEvents = 8;

portMUX_TYPE correctionMux = portMUX_INITIALIZER_UNLOCKED;
ClockSnapshot ntpSnapshot{};
bool hasNtpSnapshot = false;
bool ntpWindowRecorded = false;

ClockCorrectionEvent events[kMaxCorrectionEvents]{};
uint8_t nextEventSlot = 0;
uint32_t nextEventId = 1;
uint32_t latestEventId = 0;

int64_t localTimerCreatedMonotonicMs = 0;
uint32_t localTimerAppliedThrough = 0;
uint32_t localTimerApplyVersion = 0;
bool localTimerSaveInProgress = false;

uint64_t addSignedDelta(uint64_t value, int64_t delta) {
    if (delta >= 0) {
        return value + static_cast<uint64_t>(delta);
    }

    uint64_t backward = static_cast<uint64_t>(-delta);
    return backward >= value ? 0 : value - backward;
}

uint32_t latestCorrectionIdLocked() {
    return latestEventId;
}

void copyPendingEvents(uint32_t appliedThrough, ClockCorrectionEvent* out,
                       uint8_t* count, uint32_t* latestSeen) {
    *count = 0;
    *latestSeen = appliedThrough;

    portENTER_CRITICAL(&correctionMux);
    for (uint8_t i = 0; i < kMaxCorrectionEvents; i++) {
        if (events[i].id > appliedThrough) {
            out[*count] = events[i];
            (*count)++;
            if (events[i].id > *latestSeen) {
                *latestSeen = events[i].id;
            }
        }
    }
    portEXIT_CRITICAL(&correctionMux);

    for (uint8_t i = 0; i < *count; i++) {
        for (uint8_t j = i + 1; j < *count; j++) {
            if (out[j].id < out[i].id) {
                ClockCorrectionEvent tmp = out[i];
                out[i] = out[j];
                out[j] = tmp;
            }
        }
    }
}

bool storeCorrectionEvent(const ClockSnapshot& snapshot, const char* source,
                          bool markNtpRecorded) {
    ClockSnapshot now = captureClockSnapshot();
    int64_t elapsedMonotonicMs = now.monotonicMs - snapshot.monotonicMs;
    int64_t expectedWallMs = snapshot.wallMs + elapsedMonotonicMs;
    int64_t jumpMs = now.wallMs - expectedWallMs;

    if (llabs(jumpMs) < CLOCK_CORRECTION_THRESHOLD_MS) {
        return false;
    }

    portENTER_CRITICAL(&correctionMux);
    if (markNtpRecorded && ntpWindowRecorded) {
        portEXIT_CRITICAL(&correctionMux);
        return false;
    }

    uint32_t eventId = nextEventId++;
    events[nextEventSlot] = {
        .id = eventId,
        .jumpMs = jumpMs,
        .eventMonotonicMs = now.monotonicMs,
        .source = source,
    };
    nextEventSlot = (nextEventSlot + 1) % kMaxCorrectionEvents;
    latestEventId = eventId;
    if (markNtpRecorded) {
        ntpWindowRecorded = true;
    }
    portEXIT_CRITICAL(&correctionMux);

    ESP_LOGI(CLOCK_CORRECTION_TAG,
             "%s recorded clock jump %lldms at mono=%lld", source, jumpMs,
             now.monotonicMs);

    return true;
}

}  // namespace

int64_t currentWallClockMs() {
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    return ((int64_t)tv.tv_sec) * 1000 + ((int64_t)tv.tv_usec) / 1000;
}

int64_t currentMonotonicMs() {
    return esp_timer_get_time() / 1000;
}

ClockSnapshot captureClockSnapshot() {
    return {
        .wallMs = currentWallClockMs(),
        .monotonicMs = currentMonotonicMs(),
    };
}

bool isWallClockSet() {
    time_t now = lockboxTime(nullptr);
    struct tm timeinfo{};
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_year >= (2000 - 1900);
}

void beginNtpClockCorrectionWindow() {
    ClockSnapshot snapshot = captureClockSnapshot();

    portENTER_CRITICAL(&correctionMux);
    ntpSnapshot = snapshot;
    hasNtpSnapshot = true;
    ntpWindowRecorded = false;
    portEXIT_CRITICAL(&correctionMux);
}

bool recordNtpClockCorrection(const char* source) {
    ClockSnapshot snapshot{};

    portENTER_CRITICAL(&correctionMux);
    if (!hasNtpSnapshot || ntpWindowRecorded) {
        portEXIT_CRITICAL(&correctionMux);
        return false;
    }
    snapshot = ntpSnapshot;
    portEXIT_CRITICAL(&correctionMux);

    return storeCorrectionEvent(snapshot, source, true);
}

bool saveLocalLockTimerDuration(uint64_t durationMs) {
    uint32_t latestBeforeCreation = 0;
    int64_t previousCreatedMonotonicMs = 0;
    uint32_t previousAppliedThrough = 0;

    portENTER_CRITICAL(&correctionMux);
    latestBeforeCreation = latestCorrectionIdLocked();
    previousCreatedMonotonicMs = localTimerCreatedMonotonicMs;
    previousAppliedThrough = localTimerAppliedThrough;
    portEXIT_CRITICAL(&correctionMux);

    ClockSnapshot creationSnapshot = captureClockSnapshot();
    uint64_t lockEndDatetime = 0;
    if (!lockbox::security::localTimerDeadline(creationSnapshot.wallMs, durationMs,
                                              lockEndDatetime)) {
        ESP_LOGW(CLOCK_CORRECTION_TAG, "Could not save invalid local timer deadline");
        return false;
    }

    portENTER_CRITICAL(&correctionMux);
    localTimerSaveInProgress = true;
    localTimerCreatedMonotonicMs = creationSnapshot.monotonicMs;
    localTimerAppliedThrough = latestBeforeCreation;
    portEXIT_CRITICAL(&correctionMux);

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) !=
        pdTRUE) {
        portENTER_CRITICAL(&correctionMux);
        localTimerSaveInProgress = false;
        localTimerCreatedMonotonicMs = previousCreatedMonotonicMs;
        localTimerAppliedThrough = previousAppliedThrough;
        portEXIT_CRITICAL(&correctionMux);

        ESP_LOGW(CLOCK_CORRECTION_TAG,
                 "Could not save local timer: preferences mutex unavailable");
        return false;
    }

    preferences.begin(Keys::base, false);
    preferences.putBool(Keys::Base::isLocalPlay, true);
    preferences.putBool(Keys::Base::isLockTimer, true);
    preferences.end();

    preferences.begin(Keys::session, false);
    preferences.putULong64(Keys::Session::lockEnd, lockEndDatetime);
    preferences.end();

    portENTER_CRITICAL(&correctionMux);
    localTimerSaveInProgress = false;
    portEXIT_CRITICAL(&correctionMux);

    xSemaphoreGive(preferencesMutex);

    processPendingLocalTimerClockCorrections();

    return true;
}

bool processPendingLocalTimerClockCorrections() {
    ClockCorrectionEvent pending[kMaxCorrectionEvents]{};
    uint8_t pendingCount = 0;
    uint32_t latestSeen = 0;
    int64_t createdMonotonicMs = 0;
    uint32_t appliedThrough = 0;

    portENTER_CRITICAL(&correctionMux);
    if (localTimerSaveInProgress) {
        portEXIT_CRITICAL(&correctionMux);
        return false;
    }
    appliedThrough = localTimerAppliedThrough;
    portEXIT_CRITICAL(&correctionMux);

    copyPendingEvents(appliedThrough, pending, &pendingCount, &latestSeen);
    if (pendingCount == 0) {
        return false;
    }

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) !=
        pdTRUE) {
        ESP_LOGW(CLOCK_CORRECTION_TAG,
                 "Could not process clock correction: preferences mutex "
                 "unavailable");
        return false;
    }

    portENTER_CRITICAL(&correctionMux);
    if (localTimerSaveInProgress) {
        portEXIT_CRITICAL(&correctionMux);
        xSemaphoreGive(preferencesMutex);
        return false;
    }
    createdMonotonicMs = localTimerCreatedMonotonicMs;
    portEXIT_CRITICAL(&correctionMux);

    preferences.begin(Keys::base);
    bool isLocalPlay = preferences.getBool(Keys::Base::isLocalPlay, false);
    bool isLockTimer = preferences.getBool(Keys::Base::isLockTimer, false);
    preferences.end();

    if (!isLocalPlay || !isLockTimer) {
        xSemaphoreGive(preferencesMutex);
        return false;
    }

    preferences.begin(Keys::session, false);
    uint64_t oldLockEnd = preferences.getULong64(Keys::Session::lockEnd, 0);
    if (oldLockEnd == 0) {
        preferences.end();
        xSemaphoreGive(preferencesMutex);
        return false;
    }

    int64_t totalDeltaMs = 0;
    uint8_t appliedCount = 0;
    for (uint8_t i = 0; i < pendingCount; i++) {
        if (createdMonotonicMs == 0 ||
            createdMonotonicMs <= pending[i].eventMonotonicMs) {
            totalDeltaMs += pending[i].jumpMs;
            appliedCount++;
        }
    }

    uint64_t newLockEnd = oldLockEnd;
    if (totalDeltaMs != 0) {
        newLockEnd = addSignedDelta(oldLockEnd, totalDeltaMs);
        preferences.putULong64(Keys::Session::lockEnd, newLockEnd);
    }
    preferences.end();
    xSemaphoreGive(preferencesMutex);

    portENTER_CRITICAL(&correctionMux);
    if (latestSeen > localTimerAppliedThrough) {
        localTimerAppliedThrough = latestSeen;
    }
    if (totalDeltaMs != 0) {
        localTimerApplyVersion++;
    }
    portEXIT_CRITICAL(&correctionMux);

    if (totalDeltaMs != 0) {
        ESP_LOGI(CLOCK_CORRECTION_TAG,
                 "Adjusted local lockEnd by %lldms using %u correction(s): "
                 "%llu -> %llu",
                 totalDeltaMs, appliedCount, oldLockEnd, newLockEnd);
    }

    return totalDeltaMs != 0;
}

uint32_t localTimerCorrectionVersion() {
    uint32_t version = 0;
    portENTER_CRITICAL(&correctionMux);
    version = localTimerApplyVersion;
    portEXIT_CRITICAL(&correctionMux);
    return version;
}

void resetClockCorrectionForTest() {
    portENTER_CRITICAL(&correctionMux);
    hasNtpSnapshot = false;
    ntpWindowRecorded = false;
    nextEventSlot = 0;
    nextEventId = 1;
    latestEventId = 0;
    localTimerCreatedMonotonicMs = 0;
    localTimerAppliedThrough = 0;
    localTimerApplyVersion = 0;
    localTimerSaveInProgress = false;
    for (uint8_t i = 0; i < kMaxCorrectionEvents; i++) {
        events[i] = {};
    }
    portEXIT_CRITICAL(&correctionMux);
}
