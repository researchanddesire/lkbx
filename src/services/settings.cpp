
#include "settings.h"

#include "constants/PerferenceKeys.h"

SemaphoreHandle_t preferencesMutex = xSemaphoreCreateMutex();

LockboxPreferences preferences;

void LockboxPreferences::invalidatePollingCache() {
    lockedStateCached = false;
    sessionTimingCached = false;
}

bool LockboxPreferences::getLockedState() {
    if (!lockedStateCached) {
        if (!begin(Keys::base)) return false;
        cachedLocked = getBool(Keys::Base::isLocked, false);
        end();
        lockedStateCached = true;
    }
    return cachedLocked;
}

bool LockboxPreferences::getSessionTiming(SessionTiming& timing) {
    if (!sessionTimingCached) {
        if (!begin(Keys::session)) return false;
        cachedTiming.lockStart = getULong64(Keys::Session::lockStart, 0);
        cachedTiming.lockEnd = getULong64(Keys::Session::lockEnd, 0);
        cachedTiming.breakStart = getULong64(Keys::Session::breakStart, 0);
        cachedTiming.breakMax = getULong64(Keys::Session::breakMax, 0);
        cachedTiming.breakActive = getBool(Keys::Session::isActiveBreak, false);
        cachedTiming.frozen = getBool(Keys::Session::isFrozen, false);
        cachedTiming.frozenAt = getULong64(Keys::Session::frozenAt, 0);
        cachedTiming.freezeReason = getString(Keys::Session::freezeReason, "");
        end();
        sessionTimingCached = true;
    }
    timing = cachedTiming;
    return true;
}

size_t LockboxPreferences::putBool(const char* key, bool value) {
    invalidatePollingCache();
    return Preferences::putBool(key, value);
}

size_t LockboxPreferences::putULong64(const char* key, uint64_t value) {
    invalidatePollingCache();
    return Preferences::putULong64(key, value);
}

bool LockboxPreferences::getSessionIdentity(String& id, String& state) {
    // Activity polling must not reopen/read NVS until a write invalidates it.
    if (!sessionIdentityCached) {
        if (!begin(Keys::session)) return false;
        cachedSessionId = isKey(Keys::Session::sessionId)
            ? getString(Keys::Session::sessionId, "") : String();
        cachedLockState = isKey(Keys::Session::lockState)
            ? getString(Keys::Session::lockState, "") : String();
        end();
        sessionIdentityCached = true;
    }
    id = cachedSessionId;
    state = cachedLockState;
    return true;
}

// Invalidate centrally so every session writer (sync, accept, decline, unlock,
// and clear) is covered, including callers added after this cache.
size_t LockboxPreferences::putString(const char* key, const char* value) {
    invalidatePollingCache();
    sessionIdentityCached = false;
    return Preferences::putString(key, value);
}

size_t LockboxPreferences::putString(const char* key, String value) {
    return putString(key, value.c_str());
}

bool LockboxPreferences::clear() {
    invalidatePollingCache();
    sessionIdentityCached = false;
    return Preferences::clear();
}

bool LockboxPreferences::remove(const char* key) {
    invalidatePollingCache();
    sessionIdentityCached = false;
    return Preferences::remove(key);
}
