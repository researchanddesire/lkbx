#ifndef SOFTWARE_ACTIONS_H
#define SOFTWARE_ACTIONS_H

// include the ESP
#include "Adafruit_GFX.h"
#include "Esp.h"
#include "constants/LogTags.h"
#include "WiFi.h"
#include "constants/Colors.h"
#include "constants/PerferenceKeys.h"
#include "constants/Sizes.h"
#include "motor/Motor.h"
#include "services/display.h"
#include "services/mqtt.h"
#include "services/rad_ble.h"
#include "services/settings.h"
#include "services/wm.h"
#include "tasks/TaskParams.h"
#include "tasks/TaskStart.h"
#include "utils/syncState.h"

auto setLocalPlay = []() {
    ESP_LOGD(ACTIONS_TAG, "Setting local play");
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocalPlay, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

auto clearLocalPlay = []() {
    ESP_LOGD(ACTIONS_TAG, "Clearing local play");
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocalPlay, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

// The box holds a lock whose check has passed (utils/heldLock.h). Written
// once per lock: the paddle being told to open is what clears it.
auto markLockHeld = []() {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        if (!preferences.getBool(Keys::Base::lockHeld, false)) {
            preferences.putBool(Keys::Base::lockHeld, true);
            ESP_LOGI(ACTIONS_TAG, "Lock confirmed: it is held from here on");
        }
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

auto clearCalibration = []() {
    ESP_LOGD(ACTIONS_TAG, "Clearing calibration");
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isMotorCalibrated, false);
        preferences.putBool(Keys::Base::isButtonCalibrated, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

auto switchToTimer = []() {
    ESP_LOGD(ACTIONS_TAG, "Switching to timer");
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocked, true);
        preferences.putBool(Keys::Base::isLockTimer, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

auto switchToLock = []() {
    ESP_LOGD(ACTIONS_TAG, "Switching to lock");
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocked, true);
        preferences.putBool(Keys::Base::isLockTimer, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

// The guard commits acceptance before any motor action. An invalid clock or
// missing duration must never produce a zero-length or overflowing timer.
auto acceptOfferedLockSession = []() {
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    const uint64_t now = uint64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return false;
    preferences.begin(Keys::session, false);
    const bool pending = preferences.getString(Keys::Session::lockState, "") == Str::Protocol::PENDING;
    const uint64_t duration = preferences.getULong64(Keys::Session::duration, 0);
    const uint64_t end = acceptedOfferDeadline(pending, now, duration);
    if (end) {
        preferences.putULong64(Keys::Session::lockStart, now);
        preferences.putULong64(Keys::Session::lockEnd, end);
        preferences.putString(Keys::Session::lockState, Str::Protocol::LOCKED);
    }
    preferences.end();
    if (end) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocalPlay, false);
        preferences.putBool(Keys::Base::isLocked, true);
        preferences.putBool(Keys::Base::isLockTimer, true);
        preferences.end();
    }
    xSemaphoreGive(preferencesMutex);
    return end != 0;
};

auto acceptBreakEnd = []() {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return false;
    preferences.begin(Keys::session, false);
    preferences.putBool(Keys::Session::breakEndAccepted, true);
    preferences.putBool(Keys::Session::isActiveBreak, false);
    preferences.putULong64(Keys::Session::breakStart, 0);
    preferences.putString(Keys::Session::lockState, Str::Protocol::LOCKED);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    return true;
};

inline bool acceptExpiredTimer(bool localOnly) {
    // A keyholder freeze can land between the transition guard and this action,
    // so the frozen check is repeated while the session namespace is held. The
    // guard stays because it keeps the transition (and the unlock) from firing
    // at all; this check closes the window between the two.
    if (!isReadyToUnlock()) return false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return false;
    // Recheck ownership under the same mutex as the deadline. A replacement
    // managed session must never inherit a local timer's automatic release.
    preferences.begin(Keys::base);
    const bool local = preferences.getBool(Keys::Base::isLocalPlay, false);
    preferences.end();
    if (localOnly && !local) {
        xSemaphoreGive(preferencesMutex);
        return false;
    }
    preferences.begin(Keys::session, false);
    const uint64_t end = preferences.getULong64(Keys::Session::lockEnd, 0);
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    const uint64_t now = uint64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    // An extension may have arrived since the Ready page was drawn.
    const bool ready = end != 0 && end <= now &&
        preferences.getString(Keys::Session::lockState, "") != Str::Protocol::PENDING &&
        !preferences.getBool(Keys::Session::isFrozen, false);
    if (ready) {
        preferences.putString(Keys::Session::lockState, Str::Protocol::COMPLETED);
        preferences.putBool(Keys::Session::isActiveBreak, false);
        preferences.putULong64(Keys::Session::breakStart, 0);
    }
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    return ready;
}

auto acceptNaturalUnlock = []() { return acceptExpiredTimer(false); };
auto acceptLocalTimerUnlock = []() { return acceptExpiredTimer(true); };

auto clearEBlock = []() {
    ESP_LOGD(ACTIONS_TAG, "Clearing emergency block");
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isEBlocked, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

/**
 * Clear the active break flag when a break ends (either by user ending it,
 * or by transitioning to lock state after wake-up).
 */
auto clearActiveBreak = []() {
    ESP_LOGD(ACTIONS_TAG, "Clearing active break");
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session, false);
        preferences.putBool(Keys::Session::isActiveBreak, false);
        preferences.putULong64(Keys::Session::breakStart, 0);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

#endif  // SOFTWARE_ACTIONS_H
