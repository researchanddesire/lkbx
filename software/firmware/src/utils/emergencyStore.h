#ifndef LOCKBOX_EMERGENCY_STORE_H
#define LOCKBOX_EMERGENCY_STORE_H

#include <Arduino.h>
#include <esp_log.h>

#include "constants/LogTags.h"
#include "constants/PerferenceKeys.h"
#include "services/settings.h"
#include "utils/emergencyRecord.h"
#include "utils/emergencyReport.h"

// The emergency unlock's record in flash: reading it and applying the rules
// of utils/emergencyRecord.h to it. Every function ending in "Locked" expects
// the caller to hold preferencesMutex with no namespace open.

// A string that may not be there yet. Reading a missing string logs an
// error, so it is looked for first. The base namespace must be open.
inline String emergencyStringLocked(const char *key) {
    return preferences.isKey(key) ? preferences.getString(key, "") : String();
}

// The dashboard is seen without the emergency-ended session as its active
// one: it reports no session, or another one. Applies rule 2 and returns what
// it did.
inline EmergencySettle settleEmergencyLocked() {
    preferences.begin(Keys::base, false);
    const String ended = emergencyStringLocked(Keys::Base::emergencySession);
    // Owed until the broker has acknowledged it. The acknowledgement is
    // noted on the MQTT task and written here a moment later, so the note
    // counts as well.
    const bool owed =
        preferences.getBool(Keys::Base::emergencyOwed, false) &&
        !emergencyReport::owed.isAcknowledged();
    const EmergencySettle settle = settleEmergencySession(ended.c_str(), owed);
    if (settle != EmergencySettle::NONE) {
        preferences.putString(Keys::Base::emergencySession, "");
        // Nothing can make the report owed again once the session's name is
        // forgotten, so its text is not needed any more.
        if (preferences.isKey(Keys::Base::emergencyReport)) {
            preferences.remove(Keys::Base::emergencyReport);
        }
    }
    if (settle == EmergencySettle::LIFT_LOCKOUT) {
        preferences.putBool(Keys::Base::isEBlocked, false);
        preferences.putBool(Keys::Base::emergencyOwed, false);
    }
    preferences.end();

    if (settle == EmergencySettle::LIFT_LOCKOUT) {
        emergencyReport::owed.drop();
        ESP_LOGW(EMERGENCY_TAG,
                 "The dashboard ended the session before it heard of the "
                 "emergency unlock: lockout lifted, report dropped");
    } else if (settle == EmergencySettle::KEEP_LOCKOUT) {
        ESP_LOGI(EMERGENCY_TAG,
                 "The dashboard ended the session the emergency unlock "
                 "reported; the lockout runs on");
    }
    return settle;
}

// A dashboard clear arrived: the dashboard has no active session for the box,
// or has ended the one the box holds. True when an emergency lockout is in
// effect and stays, so the caller must not send the unlock that normally
// follows a clear. `read` is false when the record could not be read; the
// caller then treats the clear as not applied.
inline bool emergencyLockoutOutlastsClear(bool &read) {
    read = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) {
        return false;
    }
    preferences.begin(Keys::base);
    const bool lockout = preferences.getBool(Keys::Base::isEBlocked, false);
    preferences.end();
    const EmergencySettle settle = settleEmergencyLocked();
    xSemaphoreGive(preferencesMutex);
    read = true;
    return !clearEndsLockout(lockout, settle);
}

// The dashboard names `incomingSessionId` as the box's session. True when
// that is the session this box ended by emergency unlock (rule 1): the caller
// must not store it or act on it. The dashboard has not applied the report,
// so the report is owed again. Any other session means the dashboard has
// ended that one (rule 2).
inline bool refusesEmergencyEndedSessionLocked(const String &incomingSessionId) {
    // A payload that names no session says nothing about any.
    if (incomingSessionId.isEmpty()) return false;
    preferences.begin(Keys::base, false);
    const String ended = emergencyStringLocked(Keys::Base::emergencySession);
    if (ended.isEmpty()) {
        preferences.end();
        return false;
    }
    if (!isEmergencyEndedSession(ended.c_str(), incomingSessionId.c_str())) {
        preferences.end();
        settleEmergencyLocked();
        return false;
    }
    if (!preferences.getBool(Keys::Base::emergencyOwed, false)) {
        preferences.putBool(Keys::Base::emergencyOwed, true);
    }
    const String report = emergencyStringLocked(Keys::Base::emergencyReport);
    preferences.end();

    if (!report.isEmpty()) emergencyReport::owed.owe(report.c_str());
    ESP_LOGW(EMERGENCY_TAG,
             "The dashboard still holds the session an emergency unlock "
             "ended: not locking, and the emergency report is sent again");
    return true;
}

#endif  // LOCKBOX_EMERGENCY_STORE_H
