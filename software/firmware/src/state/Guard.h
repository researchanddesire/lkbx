#ifndef SOFTWARE_GUARD_H
#define SOFTWARE_GUARD_H

#include "utils/trustedClock.h"
#include <time.h>

#include "../constants/LogTags.h"
#include "Events.h"
#include "WiFi.h"
#include "services/serverOnline.h"
#include "services/settings.h"
#include "services/wm.h"
#include "utils/chasterAsk.h"
#include "utils/freezeState.h"
#include "utils/heldLock.h"

static auto isOnline = []() { return WiFiClass::status() == WL_CONNECTED; };
static auto isOffline = []() { return !isOnline(); };

static auto hasSavedWifiCredentials = []() { return wm.getWiFiIsSaved(); };

static auto hasFinishedOnboarding = []() {
    bool result = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        result = preferences.getBool(Keys::Base::hasBeenOnboarded, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    ESP_LOGD(GUARD_TAG, "hasFinishedOnboarding: %d", result);
    return result;
};

static auto isLockOffered = []() {
    bool offered = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        offered = preferences.getString(Keys::Session::lockState, "") == Str::Protocol::PENDING;
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    return offered;
};

static auto isUnlocked = []() {
    bool lockState = false;

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        lockState = preferences.getBool(Keys::Base::isLocked, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(GUARD_TAG, "isUnlocked: %d", !lockState);
    // the reward pin should be high and the punishment pin low
    return !lockState;
};

static auto isPaddleLocked = []() {
    // read all three of the TMR pins, if all three are high, then the TMRs are
    // unlocked

    bool tmr1 = digitalRead(Pins::Magnetics::tmr1) == LOW;
    bool tmr2 = digitalRead(Pins::Magnetics::tmr2) == LOW;
    bool tmr3 = digitalRead(Pins::Magnetics::tmr3) == LOW;

    ESP_LOGV(GUARD_TAG, "isPaddleLocked --> tmr1: %d, tmr2: %d, tmr3: %d", tmr1,
             tmr2, tmr3);
    return (tmr1 && tmr2 && tmr3) || (tmr1 && tmr2) || (tmr1 && tmr3) || (tmr2 && tmr3);
};

static auto isBackplateClosed = []() {
    // read all three of the TMR pins, if all three are high, then the TMRs are
    // unlocked

    bool hall1 = digitalRead(Pins::Magnetics::hall1) == LOW;
    bool hall2 = digitalRead(Pins::Magnetics::hall2) == LOW;

    ESP_LOGV(GUARD_TAG, "isBackplateClosed: %d", hall1 || hall2);
    return hall1 || hall2;
};

static auto isPaddleUnlocked = []() { return !isPaddleLocked(); };

static auto isLocked = []() { return !isUnlocked(); };

// True while the box holds a lock whose check has passed and whose paddle has
// not been told to open since (utils/heldLock.h). When the mark cannot be
// read the lock counts as held: a failed check then keeps the paddle closed.
static auto isLockHeld = []() {
    bool held = true;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        held = preferences.getBool(Keys::Base::lockHeld, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(GUARD_TAG, "isLockHeld: %d", held);
    return held;
};

static auto isError = [](ErrorT error) {
    return [error](const auto& e) { return e.error == error; };
};

static auto isMqttConnectionError = [](const auto& e) {
    return e.error == ErrorT::MQTT_AUTH_FAILED ||
           e.error == ErrorT::MQTT_TIMEOUT ||
           e.error == ErrorT::AUTH_RATE_LIMITED ||
           e.error == ErrorT::DASHBOARD_SIGN_IN_FAILED;
};

static auto always = []() { return true; };

// True once the wall clock has been set to a believable time (year >= 2020).
// Used to decide whether the date needs syncing before continuing boot.
static auto isDateTimeValid = []() {
    time_t now = lockboxTime(nullptr);
    struct tm timeinfo{};
    localtime_r(&now, &timeinfo);
    bool valid = (timeinfo.tm_year + 1900) >= 2020;
    ESP_LOGD(GUARD_TAG, "isDateTimeValid: %d (year %d)", valid,
             timeinfo.tm_year + 1900);
    return valid;
};

static auto isEmergencyBlocked = []() {
    auto lockState = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        lockState = preferences.getBool(Keys::Base::isEBlocked, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(GUARD_TAG, "isEmergencyBlocked: %d", lockState);

    return lockState;
};

static auto isLockedTimer = []() {
    auto lockState = false;
    auto lockTimer = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        lockState = preferences.getBool(Keys::Base::isLocked, false);
        lockTimer = preferences.getBool(Keys::Base::isLockTimer, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(GUARD_TAG, "isLockedTimer: %d", lockState && lockTimer);
    return lockState && lockTimer;
};

// True while the box shows a held session on its own page. Chaster holds a
// session either with a freeze or, through the same fields, with an unlock
// hold ("Chaster would refuse this unlock"). The freeze lives in the session
// namespace, so it is only meaningful for a locked session. Used by the state
// table to route a locked session into the explicit "locked.frozen" state
// instead of "locked.timer"/"locked.ready".
//
// With a hidden timer an unlock hold is not shown before the box's own
// deadline, because it would say the end is near (holdShownOnScreen). Whether
// a session may open never depends on this guard: isReadyToUnlock and
// acceptExpiredTimer read the stored flag, so a held countdown never expires
// and never reports ready-to-unlock, even after a reboot with a passed
// deadline.
static auto isFrozen = []() {
    bool frozen = false;
    bool isTimeDisp = true;
    uint64_t lockEnd = 0;
    String reason;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        frozen = preferences.getBool(Keys::Session::isFrozen, false);
        if (frozen) {
            isTimeDisp = preferences.getBool(Keys::Session::isTimeDisp, true);
            lockEnd = preferences.getULong64(Keys::Session::lockEnd, 0);
            reason = preferences.getString(Keys::Session::freezeReason, "");
        }
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(GUARD_TAG, "isFrozen: %d", frozen);
    if (!freezeHoldsCountdown(frozen)) return false;

    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    const uint64_t now = uint64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    return holdShownOnScreen(true, isUnlockHoldReason(reason.c_str()),
                             isTimeDisp, isTimeSynced, lockEnd, now);
};

// True while Chaster controls the session. Such a session asks the dashboard
// before it opens for a break or unlocks at the end of its timer.
static auto isChasterSession = []() {
    bool chaster = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        chaster = preferences.getBool(Keys::Session::chasterControlled, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(GUARD_TAG, "isChasterSession: %d", chaster);
    return chaster;
};

// False while the session hides its timer from the wearer.
static auto isTimerDisplayed = []() {
    bool displayed = true;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        displayed = preferences.getBool(Keys::Session::isTimeDisp, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    return displayed;
};

static auto isLocalPlay = []() {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return false;
    preferences.begin(Keys::base);
    const bool local = preferences.getBool(Keys::Base::isLocalPlay, false);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    return local;
};

static auto isReadyToUnlock = []() {
    // Local deadlines are created from this clock and corrected with it when
    // NTP arrives. Their elapsed duration does not require a network time sync.
    if ((!isTimeSynced && !isLocalPlay()) || !isLockedTimer() || isEmergencyBlocked()) return false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return false;
    preferences.begin(Keys::session);
    const bool isFrozen = preferences.getBool(Keys::Session::isFrozen, false);
    const uint64_t end = preferences.getULong64(Keys::Session::lockEnd, 0);
    const String state = preferences.getString(Keys::Session::lockState, "");
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    // A frozen session never becomes ready: the countdown is held at frozenAt,
    // so a deadline in the past (reboot, missed unfreeze, late freeze) must not
    // unlock and must not auto-unlock. Emergency unlock and physical controls
    // keep their own paths.
    if (freezeHoldsCountdown(isFrozen)) {
        ESP_LOGD(GUARD_TAG, "isReadyToUnlock: held by a Chaster freeze");
        return false;
    }
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    const uint64_t now = uint64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    return end != 0 && end <= now && state != Str::Protocol::PENDING;
};

static auto isCalibrated = []() {
    auto motorCalibrated = false;
    auto buttonCalibrated = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        motorCalibrated =
            preferences.getBool(Keys::Base::isMotorCalibrated, false);
        buttonCalibrated =
            preferences.getBool(Keys::Base::isButtonCalibrated, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    return motorCalibrated && buttonCalibrated;
};

static auto isBreakEndRequested = []() {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return true;
    preferences.begin(Keys::session);
    const bool requested = preferences.getString(Keys::Session::lockState, "") == Str::Protocol::BREAK_ENDING;
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    return requested;
};

/**
 * Check if the device is currently in an active break session (possibly late).
 * This is used to restore break state after device wake-up from sleep.
 * Also verifies the device is actually locked to prevent going to break
 * when the device should be unlocked.
 */
static auto isOnActiveBreak = []() {
    bool isActiveBreak = false;
    bool deviceIsLocked = false;
    bool lockHeld = false;

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        isActiveBreak =
            preferences.getBool(Keys::Session::isActiveBreak, false);
        preferences.end();

        preferences.begin(Keys::base);
        deviceIsLocked = preferences.getBool(Keys::Base::isLocked, false);
        lockHeld = preferences.getBool(Keys::Base::lockHeld, false);
        preferences.end();

        xSemaphoreGive(preferencesMutex);
    }

    // A break record beside a held lock is left over from a break that has
    // ended; resuming it would open a locked box (utils/heldLock.h).
    bool stillOnBreak =
        breakRecordStands(isActiveBreak, deviceIsLocked, lockHeld);
    if (isActiveBreak && deviceIsLocked && lockHeld) {
        ESP_LOGW(GUARD_TAG,
                 "Ignoring a break record left beside a held lock");
    }

    ESP_LOGD(GUARD_TAG, "isOnActiveBreak: %d", stillOnBreak);

    return stillOnBreak;
};

#endif  // SOFTWARE_GUARD_H
