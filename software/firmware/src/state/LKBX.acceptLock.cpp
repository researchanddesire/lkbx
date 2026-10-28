#include "LKBX.h"

// Recovered acceptance flow from the saved July 23 staging work. Poll from the
// activity task so leaving/replacing the screen cannot race a lingering task.
void LKBX::checkOfferedLock() {
    String id;
    String savedState;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return;
    const bool loaded = preferences.getSessionIdentity(id, savedState);
    xSemaphoreGive(preferencesMutex);
    if (!loaded) return;

    const uint32_t now = millis();
    // An offer received during boot/calibration can miss its first event.
    // Retry when the state machine reaches a screen that handles offers.
    if (!id.isEmpty() && savedState == Str::Protocol::PENDING && !sm->is("acceptLock"_s)) {
        sm->process_event(LockOffered{});
    }
    if (sm->is("acceptLock"_s) && savedState == Str::Protocol::PENDING) {
        if (offerSessionId != id) {
            offerSessionId = id;
            offerStartedMs = now;
            decisionPublishedMs = now - 10000;
        }
        if (offeredLockTimedOut(offerStartedMs, now)) sm->process_event(Decline{});
    } else {
        offerSessionId = "";
    }

    // Retry decisions only while connected. Their persisted session ID lets
    // the server reject stale acknowledgements after a replacement session.
    if (!id.isEmpty() && mqttConnected && uint32_t(now - decisionPublishedMs) >= 10000) {
        if (savedState == Str::Protocol::PENDING && isLockOffered()) {
            sendMessage(Str::Protocol::PENDING, id);
            decisionPublishedMs = now;
        } else if (savedState == Str::Protocol::ABANDONED) {
            sendMessage(Str::Protocol::ABANDONED, id);
            decisionPublishedMs = now;
        } else if (savedState == Str::Protocol::COMPLETED &&
                   sm->is("home.idle"_s) && isPaddleUnlocked()) {
            sendMessage(Str::Protocol::UNLOCKED, id);
            decisionPublishedMs = now;
        }
    }
}
