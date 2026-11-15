#ifndef LOCKBOX_SAVED_TIMER_SLEEP_H
#define LOCKBOX_SAVED_TIMER_SLEEP_H

#include <sys/time.h>

#include "utils/trustedClock.h"

#include "constants/PerferenceKeys.h"
#include "services/lastInteraction.h"
#include "services/settings.h"
#include "utils/timerSleep.h"

// The countdown task may stop when the state machine enters sleep. Read its
// persisted deadline instead of the task's mutable sleepDuration cache.
inline uint64_t savedTimerSleepDelayMs() {
    if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE)
        return 1;

    preferences.begin(Keys::base);
    const bool emergency = preferences.getBool(Keys::Base::isEBlocked, false);
    const bool active = emergency ||
        (preferences.getBool(Keys::Base::isLocked, false) &&
         preferences.getBool(Keys::Base::isLockTimer, false));
    uint64_t deadline = preferences.getULong64(Keys::Base::emergencyEnd, 0);
    preferences.end();
    // The emergency countdown is always shown.
    bool timerDisplayed = true;
    if (!emergency) {
        preferences.begin(Keys::session);
        deadline = preferences.getULong64(Keys::Session::lockEnd, 0);
        timerDisplayed = preferences.getBool(Keys::Session::isTimeDisp, true);
        preferences.end();
    }
    xSemaphoreGive(preferencesMutex);

    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    const uint64_t now = static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
                         static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
    return timerSleepDelayMs(
        active, deadline, now,
        timerWakeMarginMs(timerDisplayed, PSEUDO_SLEEP_TIMEOUT));
}

#endif
