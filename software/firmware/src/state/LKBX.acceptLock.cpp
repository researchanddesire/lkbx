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

    // An offer on screen is reported every ten seconds, each time as a new
    // report: the dashboard reads a later one as "still offered". It ends
    // with the offer, five minutes at most.
    if (!id.isEmpty() && mqttConnected && savedState == Str::Protocol::PENDING &&
        isLockOffered() && uint32_t(now - decisionPublishedMs) >= 10000) {
        sendMessage(Str::Protocol::PENDING, id);
        decisionPublishedMs = now;
    }

    // A decline, and the unlock at the end of the timer, are reported until
    // the dashboard has taken them: it clears the session or sends another,
    // and then the box holds no decision. The same report is sent each time,
    // with a longer wait after every attempt (utils/decisionReport.h).
    const char *decision = nullptr;
    if (!id.isEmpty()) {
        if (savedState == Str::Protocol::ABANDONED) {
            decision = Str::Protocol::ABANDONED;
        } else if (savedState == Str::Protocol::COMPLETED &&
                   sm->is("home.idle"_s) && isPaddleUnlocked()) {
            decision = Str::Protocol::UNLOCKED;
        }
    }
    if (decision == nullptr) {
        // An open box that is not on its home page yet keeps its report for
        // when it gets there; anything else has nothing left to repeat.
        if (id.isEmpty() || savedState != Str::Protocol::COMPLETED) {
            decisionReport::tracker.clear();
        }
        return;
    }
    repeatDecisionReport(decision, id);
}

// Repeats the report of a decision the box holds, when it is due. The page
// that made the decision has reported it once already. The first repeat
// builds the report; every later one sends those bytes again, so the
// dashboard sees one repeated report whatever the number of attempts.
void LKBX::repeatDecisionReport(const char *state, const String &sessionId) {
    const uint32_t now = millis();
    if (!decisionReport::tracker.due(sessionId.c_str(), state, now)) return;
    // Attempts are only spent while connected, with a clock: a report that
    // is due goes out as soon as the box has both.
    if (!mqttConnected || !client || !isTimeSynced) return;

    std::string payload = decisionReport::tracker.payload();
    const bool first = payload.empty();
    if (first) {
        // The box has moved on from whatever it reported before, as with any
        // other state report (sendMessage).
        lockedReport::tracker.clear();
        unsentReport::holder.clear();
        payload = createStateMessage(state, true, sessionId).c_str();
    }

    const String topic = cloudTopic();
    // Each attempt is sealed afresh; the report inside stays the same.
    const String sealed = sealCloudReport(
        topic.c_str(), lockbox::security::ReportKind::state,
        String(payload.c_str()));
    int result = -1;
    if (!sealed.isEmpty()) {
        result = esp_mqtt_client_enqueue(client, topic.c_str(), sealed.c_str(),
                                         0, 2, false, true);
    }
    decisionReport::tracker.onAttempt(result, now, payload);
    ESP_LOGW(MQTT_TAG,
             "%s report not answered yet, sent again (attempt %u), id %d",
             state,
             static_cast<unsigned>(decisionReport::tracker.attemptCount()),
             result);
}
