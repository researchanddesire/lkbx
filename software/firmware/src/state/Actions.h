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
#include "utils/naturalUnlock.h"
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
    // The unlock at the end of a timer stores nothing until its paddle has
    // opened (utils/naturalUnlock.h): this action runs in front of that
    // unlock, and a local timer whose paddle then stays closed must still be
    // the wearer's own. finishNaturalUnlock clears the flag instead.
    if (naturalUnlock::tracker.isAccepted(millis())) return;
    ESP_LOGD(ACTIONS_TAG, "Clearing local play");
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocalPlay, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
};

// The box holds a lock whose check has passed (utils/heldLock.h). Written
// once per lock: the paddle being told to open is what clears it. A box that
// is closed and confirmed is not on a break, so a break record still standing
// here was left behind and is removed: it would otherwise end the next break
// at once, as one that ran out long ago.
auto markLockHeld = []() {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        if (!preferences.getBool(Keys::Base::lockHeld, false)) {
            preferences.putBool(Keys::Base::lockHeld, true);
            ESP_LOGI(ACTIONS_TAG, "Lock confirmed: it is held from here on");
        }
        preferences.end();
        preferences.begin(Keys::session, false);
        if (preferences.getBool(Keys::Session::isActiveBreak, false)) {
            preferences.putBool(Keys::Session::isActiveBreak, false);
            preferences.putULong64(Keys::Session::breakStart, 0);
            ESP_LOGW(ACTIONS_TAG,
                     "Removed a break record left beside a confirmed lock");
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

// Accepts the unlock at the end of a session's timer. Nothing is stored here:
// the session is stored as finished only once the check after the motor
// movement has seen the paddle open (finishNaturalUnlock, below). Stored at
// this point, as it used to be, a session whose paddle then stayed closed was
// finished on the box while the wearer was still locked in, and ignored the
// dashboard from then on (utils/naturalUnlock.h).
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
    preferences.begin(Keys::session);
    const uint64_t end = preferences.getULong64(Keys::Session::lockEnd, 0);
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    const uint64_t now = uint64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    // An extension may have arrived since the Ready page was drawn.
    const bool ready = end != 0 && end <= now &&
        preferences.getString(Keys::Session::lockState, "") != Str::Protocol::PENDING &&
        !preferences.getBool(Keys::Session::isFrozen, false);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    // The unlock action that follows opens the paddle without ending the
    // stored lock.
    if (ready) naturalUnlock::tracker.accept(millis());
    return ready;
}

// The check after the motor movement saw the paddle open at the end of the
// session's timer: the session is finished. Stores what the accept and the
// unlock used to store before the paddle had moved. Waits for the settings as
// Motor::unlock does, because the paddle is open and that must be on record.
inline void finishNaturalUnlock() {
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) != pdTRUE) return;
    preferences.begin(Keys::base, false);
    const bool locked = preferences.getBool(Keys::Base::isLocked, false);
    if (locked) preferences.putBool(Keys::Base::isLocked, false);
    if (preferences.getBool(Keys::Base::isLocalPlay, false)) {
        preferences.putBool(Keys::Base::isLocalPlay, false);
    }
    preferences.end();
    // A session that replaced this one since the paddle was told to open is
    // an offer, and is stored unlocked: it is not the one that ran out.
    if (locked) {
        preferences.begin(Keys::session, false);
        if (preferences.getString(Keys::Session::lockState, "") != Str::Protocol::PENDING) {
            preferences.putString(Keys::Session::lockState, Str::Protocol::COMPLETED);
            preferences.putBool(Keys::Session::isActiveBreak, false);
            preferences.putULong64(Keys::Session::breakStart, 0);
        }
        preferences.end();
    }
    xSemaphoreGive(preferencesMutex);
}

// The check after the motor movement saw the paddle still closed at the end
// of the session's timer. The session and the stored lock are as they were;
// only the held mark was cleared, when the paddle was told to open. A lock
// that was held is held again: the paddle has not left its place, so a failed
// check after the next restart must still keep it closed (utils/heldLock.h).
// A lock that was not held (the box was on a break) is left as it was.
inline void keepLockAfterFailedUnlock(bool wasHeld) {
    if (!wasHeld) return;
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) != pdTRUE) return;
    preferences.begin(Keys::base, false);
    if (preferences.getBool(Keys::Base::isLocked, false) &&
        !preferences.getBool(Keys::Base::lockHeld, false)) {
        preferences.putBool(Keys::Base::lockHeld, true);
    }
    preferences.end();
    xSemaphoreGive(preferencesMutex);
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
