#ifndef LOCKBOX_SETTINGS_H
#define LOCKBOX_SETTINGS_H

#include <Preferences.h>

#include <mutex>

extern SemaphoreHandle_t preferencesMutex;

struct SessionTiming {
    uint64_t lockStart = 0;
    uint64_t lockEnd = 0;
    uint64_t breakStart = 0;
    uint64_t breakMax = 0;
    bool breakActive = false;
    bool frozen = false;
    uint64_t frozenAt = 0;
    String freezeReason;
};

// All access, including the session cache, is protected by preferencesMutex.
class LockboxPreferences : public Preferences {
   public:
    bool getSessionIdentity(String& id, String& state);
    bool getLockedState();
    bool getSessionTiming(SessionTiming& timing);
    size_t putBool(const char* key, bool value);
    size_t putULong64(const char* key, uint64_t value);
    size_t putString(const char* key, const char* value);
    size_t putString(const char* key, String value);
    bool clear();
    bool remove(const char* key);

   private:
    bool sessionIdentityCached = false;
    String cachedSessionId;
    String cachedLockState;
    bool lockedStateCached = false;
    bool cachedLocked = false;
    bool sessionTimingCached = false;
    SessionTiming cachedTiming;
    void invalidatePollingCache();
};

extern LockboxPreferences preferences;

#endif  // LOCKBOX_SETTINGS_H
