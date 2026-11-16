#ifndef LOCKBOX_SYNCSTATE_H
#define LOCKBOX_SYNCSTATE_H

#include "utils/trustedClock.h"
#include <Arduino.h>
#include <WiFi.h>

#include "ArduinoJson.h"
#include "CloudInput.h"
#include "FirmwareProvenance.h"
#include "constants/LogTags.h"
#include "constants/PerferenceKeys.h"
#include "constants/Pins.h"
#include "constants/dashboard.h"
#include "constants/strings.h"
#include "services/deviceIdentity.h"
#include "services/magnetics.h"
#include "services/settings.h"
#include "services/wm.h"
#include "state/Guard.h"
#include "utils/breakTiming.h"
#include "utils/chasterAsk.h"
#include "utils/emergencyStore.h"
#include "utils/freezeState.h"
#include "utils/generateId.h"
#include "utils/lockReportReconcile.h"
#include "utils/offeredLock.h"
#include "utils/syncEffect.h"
#include "utils/getEfuseMac.h"

static String createStateMessage(const String& state,
                                 bool includeState = true,
                                 const String& sessionId = "") {
    // Get the current time in milliseconds
    uint64_t currentDatetimeMs;
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    currentDatetimeMs =
        ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;

    JsonDocument doc;

    // Fill the JSON document
    doc[Str::Json::ID] = generateRandomID();
    doc[Str::Json::STATE] = state;
    doc[Str::Json::CREATED_AT] = includeState ? currentDatetimeMs : 0;
    doc["firmwareProvenanceId"] =
        firmware::provenance::currentTokenId();

    // get preferences mutex
    if (includeState &&
        xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        doc[Str::Json::IS_LOCKED] = preferences.getBool(Keys::Base::isLocked, false);
        doc[Str::Json::IS_LOCK_TIMER] =
            preferences.getBool(Keys::Base::isLockTimer, false);
        doc[Str::Json::IS_E_BLOCKED] = preferences.getBool(Keys::Base::isEBlocked, false);
        doc[Str::Json::IS_LOCAL_PLAY] =
            preferences.getBool(Keys::Base::isLocalPlay, false);
        doc[Str::Json::EMERGENCY_END] =
            preferences.getULong64(Keys::Base::emergencyEnd, 0);
        preferences.end();

        preferences.begin(Keys::session);
        doc[Str::Json::SESSION_ID] = preferences.getString(Keys::Session::sessionId, "");
        doc[Str::Json::BREAK_LAST] = preferences.getULong64(Keys::Session::breakLast, 0);
        doc["breakEndAccepted"] = preferences.getBool(Keys::Session::breakEndAccepted, false);
        // The box witnessed the accept: carry its own dates in every report
        // so the server can date the session from any of them, not only from
        // the one accept report that may be lost.
        const uint64_t lockStart = preferences.getULong64(Keys::Session::lockStart, 0);
        const uint64_t lockEnd = preferences.getULong64(Keys::Session::lockEnd, 0);
        if (lockStart != 0) doc[Str::Json::LOCK_START] = lockStart;
        if (lockEnd != 0) doc[Str::Json::LOCK_END] = lockEnd;
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    if (!sessionId.isEmpty()) doc[Str::Json::SESSION_ID] = sessionId;

    // some other values

    // Serialize the JSON document to a string
    String output;
    serializeJson(doc, output);


    return output;
}

static String createActivityLogMessage(const String& state,
                                       const String& currentState = "") {
    // Get the current time in milliseconds
    uint64_t currentDatetimeMs;
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    currentDatetimeMs =
        ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;

    JsonDocument doc;

    // Fill the JSON document
    doc[Str::Json::ID] = generateRandomID();
    doc[Str::Json::IS_ACTIVITY_LOG] = true;
    doc[Str::Json::CREATED_AT] = currentDatetimeMs;

    doc[Str::Json::CURRENT_STATE] = currentState;
    doc["firmwareProvenanceId"] =
        firmware::provenance::currentTokenId();

    doc[Str::Json::TMR1] = digitalRead(Pins::Magnetics::tmr1) == LOW;
    doc[Str::Json::TMR2] = digitalRead(Pins::Magnetics::tmr2) == LOW;
    doc[Str::Json::TMR3] = digitalRead(Pins::Magnetics::tmr3) == LOW;
    doc[Str::Json::HALL1] = digitalRead(Pins::Magnetics::hall1) == LOW;
    doc[Str::Json::HALL2] = digitalRead(Pins::Magnetics::hall2) == LOW;

    doc[Str::Json::IS_CHARGING] = digitalRead(Pins::Power::usbDetect) == HIGH;

    // doc["busVoltage"] = ina.getBusVoltage_V(); // Instead of this, sending
    // composite battery percentage below Calculate battery percentage from bus
    // voltage (assuming 3.0V is empty and 4.2V is full) Should consider sharing
    // this (or modified) percentage calculation for use with battery meter This
    // will be inaccurate if the motor was recently under load
    int batteryPercent =
        static_cast<int>(((ina.getBusVoltage_V() - 3.0) / (4.2 - 3.0)) * 100);
    batteryPercent = std::max(0, std::min(100, batteryPercent));  // Clamp 0-100
    doc[Str::Json::BATTERY_PERCENT] = batteryPercent;

    // Paddle uses the debounced telemetry value to avoid reporting brief
    // magnetic transients (e.g. when the backplate is removed). Backplate stays
    // raw — its hall signal is clean and we want it instantaneous.
    doc[Str::Json::IS_PADDLE_LOCKED] = getStablePaddleLocked();
    doc[Str::Json::IS_BACKPLATE_CLOSED] = isBackplateClosed();

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        doc[Str::Json::SESSION_ID] = preferences.getString(Keys::Session::sessionId, "");
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    // Serialize the JSON document to a string
    String output;
    serializeJson(doc, output);


    return output;
}

// enum for pull results.
enum class SyncResult {
    FAILED = -1,
    UNLOCKED = 0,
    LOCKED = 1,
    BREAK = 2,
    NO_CHANGE = 3,
    PENDING = 4,
};

// type for                 //
// {"type":"sync","isBreak":false,"lockEnd":1727962467121,"breakMax":0,"breakReg":0,"breakLast":0,"isEUnlock":false,"lockStart":1727876067121,"lockState":"locked","sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","isTestLock":true,"isTimeDisp":true,"displayElapsedTime":false}
struct SyncMessage {
    bool isBreak;
    uint64_t lockEnd;
    uint64_t breakMax;
    uint64_t breakReg;
    uint64_t breakLast;
    uint64_t duration;
    bool isEUnlock;
    uint64_t lockStart;
    String lockState;
    String sessionId;
    String lockName;
    String requestedBy;
    bool isTestLock;
    bool isTimeDisp;
    bool displayElapsedTime;
    // Chaster freeze state. The has* flags record whether the payload carried
    // the field: a legacy payload (or a BLE session write) must never clear a
    // freeze already known for the same session.
    bool isFrozen;
    uint64_t frozenAt;
    String freezeReason;
    bool hasIsFrozen;
    bool hasFrozenAt;
    bool hasFreezeReason;
    // Only the keyholder may start a break. Missing in older payloads, which
    // keeps the wearer's break button working as before.
    bool breakKeyholderOnly;
    // Chaster controls the session: the box asks the dashboard before it
    // opens for a break or unlocks. hasChasterControlled records whether the
    // payload carried the field, as for the freeze fields above.
    bool chasterControlled;
    bool hasChasterControlled;
};

// Separate persisted metadata from physical transitions. Runtime callers use
// effect; boot uses SyncResult to restore the physical state through preflight.
struct SyncOutcome {
    bool freezeChanged = false;
    bool timingUnchanged = false;
    // The server never recorded the lock this box holds (still an offer there,
    // or locked with no end time): the accept report was lost. The caller
    // repeats the LOCKED report, which now carries the box's own dates.
    bool serverMissedLock = false;
    // What the caller must route: nothing, a timer repaint, a freeze change, or
    // the motor sequence. See classifySyncEffect.
    SyncEffect effect = SyncEffect::NONE;
};

inline FreezeSync freezeSyncFrom(const SyncMessage& message) {
    FreezeSync sync{};
    sync.hasIsFrozen = message.hasIsFrozen;
    sync.isFrozen = message.isFrozen;
    sync.hasFrozenAt = message.hasFrozenAt;
    sync.frozenAt = message.frozenAt;
    sync.hasReason = message.hasFreezeReason;
    sync.reason = message.freezeReason.c_str();
    return sync;
}

// Apply the shared freeze presence rules to a parsed payload. Templated so the
// HTTP (`data`) object and the MQTT sync document use one implementation.
template <typename TJsonObject>
inline void applyFreezeSync(const TJsonObject& document, SyncMessage& message) {
    const FreezeSync sync =
        freezeSyncFromJson(document, Str::Json::IS_FROZEN,
                           Str::Json::FROZEN_AT, Str::Json::FREEZE_REASON);
    message.hasIsFrozen = sync.hasIsFrozen;
    message.isFrozen = sync.isFrozen;
    message.hasFrozenAt = sync.hasFrozenAt;
    message.frozenAt = sync.frozenAt;
    message.hasFreezeReason = sync.hasReason;
    message.freezeReason =
        sync.reason != nullptr ? String(sync.reason) : String();
}

// Apply the Chaster-controlled flag of a parsed payload. Templated so the HTTP
// (`data`) object and the MQTT sync document use one implementation.
template <typename TJsonObject>
inline void applyChasterSync(const TJsonObject& document, SyncMessage& message) {
    bool controlled = false;
    bool permission = false;
    const bool hasControlled = sessionFlagFromJson(
        document, Str::Json::CHASTER_CONTROLLED, controlled);
    const bool hasPermission = sessionFlagFromJson(
        document, Str::Json::REQUIRES_PERMISSION, permission);
    const AskFlag flag =
        combineAskFlags(hasControlled, controlled, hasPermission, permission);
    message.hasChasterControlled = flag.present;
    message.chasterControlled = flag.asks;
}

// Read the persisted freeze state. The caller must already hold
// preferencesMutex with the session namespace open.
inline FreezeState readStoredFreezeLocked() {
    FreezeState stored{};
    stored.isFrozen = preferences.getBool(Keys::Session::isFrozen, false);
    stored.frozenAt = preferences.getULong64(Keys::Session::frozenAt, 0);
    copyBoundedString(stored.reason, sizeof(stored.reason),
                      preferences.getString(Keys::Session::freezeReason, "").c_str());
    return stored;
}

static SyncResult clearState() {
    // clear settings
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session, false);
        preferences.clear();
        preferences.end();

        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocked, false);
        preferences.putBool(Keys::Base::isLockTimer, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    ESP_LOGD(SYNC_STATE_TAG, "Cleared settings");
    return SyncResult::UNLOCKED;
}

static SyncResult saveState(SyncMessage lockSession,
                            SyncOutcome* outcome = nullptr) {
    if (outcome) *outcome = SyncOutcome{};

    // get the preferencesMutex
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) {
        ESP_LOGE(SYNC_STATE_TAG, "Failed to acquire preferencesMutex in saveState");
        return SyncResult::FAILED;
    }

    // A session this box ended by emergency unlock is never taken up again
    // (utils/emergencyRecord.h). The dashboard still names it when the
    // emergency report never arrived; stored as any other answer, it locked
    // the box again at the next boot, or when the lockout ran out. Nothing
    // is stored and nothing moves; the report is sent again.
    if (refusesEmergencyEndedSessionLocked(lockSession.sessionId)) {
        xSemaphoreGive(preferencesMutex);
        return SyncResult::NO_CHANGE;
    }

    preferences.begin(Keys::session, false);

    const String savedId = preferences.getString(Keys::Session::sessionId, "");
    const String savedState = preferences.getString(Keys::Session::lockState, "");
    const bool sameSession = !savedId.isEmpty() && savedId == lockSession.sessionId;
    // Read before the namespace is cleared for a new session below.
    const bool chasterControlled = mergeChasterControlled(
        savedId != lockSession.sessionId,
        preferences.getBool(Keys::Session::chasterControlled, false),
        lockSession.hasChasterControlled, lockSession.chasterControlled);

    // The box's own deadline for the session it holds. The server learns the
    // dates only from the box's reports; when the accept report was lost it
    // has none, and must neither erase the box's deadline nor stay unaware.
    const uint64_t savedLockEnd = preferences.getULong64(Keys::Session::lockEnd, 0);
    const uint64_t savedLockStart = preferences.getULong64(Keys::Session::lockStart, 0);
    if (outcome) {
        outcome->serverMissedLock = serverMissedLock(
            sameSession, savedState.c_str(),
            preferences.getBool(Keys::Session::isActiveBreak, false), savedLockEnd,
            lockSession.lockState.c_str(), lockSession.lockEnd);
    }
    if (keepLocalDeadline(sameSession, lockSession.lockState.c_str(),
                          lockSession.lockEnd, savedLockEnd)) {
        ESP_LOGW(SYNC_STATE_TAG,
                 "Server has no end time for the held session, keeping local "
                 "deadline %llu",
                 savedLockEnd);
        lockSession.lockEnd = savedLockEnd;
        lockSession.lockStart = savedLockStart;
    }

    // Freeze state is applied on every path, including the ones that ignore the
    // rest of the sync: a keyholder freeze must land even while a stale pending
    // echo is being held off, and a legacy payload must leave it untouched.
    const FreezeMerge freeze =
        mergeFreezeState(savedId != lockSession.sessionId,
                         readStoredFreezeLocked(), freezeSyncFrom(lockSession));
    // Empty sync strings mean "leave the stored value alone" (see the writes
    // below), so they compare as unchanged rather than as a pending edit.
    const bool lockNameUnchanged =
        lockSession.lockName.isEmpty() ||
        preferences.getString(Keys::Session::lockName, "") ==
            lockSession.lockName;
    const bool requestedByUnchanged =
        lockSession.requestedBy.isEmpty() ||
        preferences.getString(Keys::Session::requestedBy, "") ==
            lockSession.requestedBy;
    const bool timingUnchanged =
        sameSession && savedState == lockSession.lockState &&
        preferences.getULong64(Keys::Session::lockEnd, 0) ==
            lockSession.lockEnd &&
        preferences.getULong64(Keys::Session::lockStart, 0) ==
            lockSession.lockStart &&
        preferences.getULong64(Keys::Session::duration, 0) ==
            lockSession.duration &&
        preferences.getBool(Keys::Session::isBreak, false) ==
            lockSession.isBreak &&
        preferences.getULong64(Keys::Session::breakMax, 0) ==
            lockSession.breakMax &&
        preferences.getULong64(Keys::Session::breakReg, 0) ==
            lockSession.breakReg &&
        preferences.getULong64(Keys::Session::breakLast, 0) >=
            lockSession.breakLast &&
        preferences.getBool(Keys::Session::isEUnlock, false) ==
            lockSession.isEUnlock &&
        preferences.getBool(Keys::Session::isTimeDisp, true) ==
            lockSession.isTimeDisp &&
        preferences.getBool(Keys::Session::displayElapsedTime, false) ==
            lockSession.displayElapsedTime &&
        lockNameUnchanged && requestedByUnchanged;

    auto persistFreeze = [&]() {
        if (!freeze.write) return;
        preferences.putBool(Keys::Session::isFrozen, freeze.state.isFrozen);
        preferences.putULong64(Keys::Session::frozenAt, freeze.state.frozenAt);
        preferences.putString(Keys::Session::freezeReason,
                              String(freeze.state.reason));
    };
    auto publishOutcome = [&]() {
        if (!outcome) return;
        outcome->freezeChanged = freeze.changed;
        outcome->timingUnchanged = timingUnchanged;
    };
    auto finish = [&](SyncResult result) {
        preferences.end();
        xSemaphoreGive(preferencesMutex);
        publishOutcome();
        // Ignored offers, declines and stale break-end echoes never create a
        // new physical transition. A freeze update can still refresh a main
        // locked screen; menus and break/emergency screens ignore this event.
        if (outcome && freeze.changed) outcome->effect = SyncEffect::TIMER_META;
        return result;
    };

    // A repeated end request must not reopen a break the wearer accepted.
    // The keyholder-only flag can ride on that same edit, so keep it.
    if (sameSession && lockSession.lockState == Str::Protocol::BREAK_ENDING &&
        preferences.getBool(Keys::Session::breakEndAccepted, false) &&
        !preferences.getBool(Keys::Session::isActiveBreak, false)) {
        preferences.putBool(Keys::Session::breakKeyholderOnly,
                            lockSession.breakKeyholderOnly);
        persistFreeze();
        return finish(SyncResult::NO_CHANGE);
    }
    // A new session always needs consent, even if a server echo says locked.
    // Only the local Accept action may advance a persisted pending offer.
    const bool isPending = lockSession.lockState == Str::Protocol::PENDING ||
        (!lockSession.sessionId.isEmpty() && !sameSession) ||
        (sameSession && savedState == Str::Protocol::PENDING);
    // MQTT retries / HTTP reconciliation must not undo the wearer's decision.
    if ((sameSession && (savedState == Str::Protocol::ABANDONED ||
                        savedState == Str::Protocol::COMPLETED)) ||
        (isPending && preserveOfferedDecision(
            sameSession,
            savedState == Str::Protocol::LOCKED || savedState == Str::Protocol::BREAK ||
                savedState == Str::Protocol::BREAK_ENDING,
            savedState == Str::Protocol::ABANDONED))) {
        persistFreeze();
        return finish(SyncResult::NO_CHANGE);
    }
    if (savedId != lockSession.sessionId) preferences.clear();
    if (isPending) {
        lockSession.lockState = Str::Protocol::PENDING;
        lockSession.lockStart = 0;
        lockSession.lockEnd = 0;
    }

    // string values
    preferences.putString(Keys::Session::sessionId,
                          String(lockSession.sessionId));
    preferences.putString(Keys::Session::lockState,
                          String(lockSession.lockState));
    if (!lockSession.lockName.isEmpty())
        preferences.putString(Keys::Session::lockName, lockSession.lockName);
    if (!lockSession.requestedBy.isEmpty())
        preferences.putString(Keys::Session::requestedBy, lockSession.requestedBy);

    // boolean values
    preferences.putBool(Keys::Session::isBreak, lockSession.isBreak);
    preferences.putBool(Keys::Session::breakKeyholderOnly,
                        lockSession.breakKeyholderOnly);
    preferences.putBool(Keys::Session::chasterControlled, chasterControlled);
    preferences.putBool(Keys::Session::isEUnlock, lockSession.isEUnlock);
    preferences.putBool(Keys::Session::isTimeDisp, lockSession.isTimeDisp);
    preferences.putBool(Keys::Session::displayElapsedTime,
                        lockSession.displayElapsedTime);
    preferences.putBool(Keys::Session::isTestLock, lockSession.isTestLock);

    // date values in MS
    preferences.putULong64(Keys::Session::breakReg, lockSession.breakReg);
    preferences.putULong64(Keys::Session::breakMax, lockSession.breakMax);

    // The most recent breakLast is the correct one.
    uint64_t breakLast =
        max(preferences.getULong64(Keys::Session::breakLast, 0),
            lockSession.breakLast);
    preferences.putULong64(Keys::Session::breakLast, breakLast);
    // isBreak means "breaks are enabled", not "a break is active".
    // Active break state is managed locally by the device — do not touch
    // isActiveBreak or breakStart here. They are owned by startBreakTimer
    // (entry) and clearActiveBreak (exit).
    preferences.putULong64(Keys::Session::lockEnd, lockSession.lockEnd);
    preferences.putULong64(Keys::Session::lockStart, lockSession.lockStart);
    preferences.putULong64(Keys::Session::duration, lockSession.duration);

    // Freeze fields (isFrozen / frozenAt / freezeReason)
    persistFreeze();

    preferences.end();

    // Offers stay unlocked until the wearer accepts them.
    preferences.begin(Keys::base, false);
    preferences.putBool(Keys::Base::isLocalPlay, false);
    preferences.putBool(Keys::Base::isLocked, !isPending);
    preferences.putBool(Keys::Base::isLockTimer, !isPending);
    // A new session with a "locked" state but no deadline is a permanent lock:
    // keep isLockTimer clear so a reboot routes straight into "locked" instead
    // of the countdown page. A timed lock always keeps it set.
    if (!isPending && lockSession.lockState == Str::Protocol::LOCKED &&
        lockSession.lockEnd == 0) {
        preferences.putBool(Keys::Base::isLockTimer, false);
    }
    preferences.end();

    xSemaphoreGive(preferencesMutex);

    ESP_LOGI(SYNC_STATE_TAG, "Saved session state");

    // saveState is the single source of truth for the sync result. A break is
    // still a locked session on disk (isLocked/isLockTimer stay true above),
    // but callers must dispatch Break{} rather than LockTimer{}.
    publishOutcome();
    if (outcome) outcome->effect = classifySyncEffect(
        sameSession, savedState.c_str(), lockSession.lockState.c_str(),
        freeze.changed || !timingUnchanged);
    if (isPending) return SyncResult::PENDING;
    if (lockSession.lockState == Str::Protocol::BREAK ||
        lockSession.lockState == Str::Protocol::BREAK_ENDING) {
        return SyncResult::BREAK;
    }

    return SyncResult::LOCKED;
}

// Keep the declined session ID until the server clears it. MQTT's RAM outbox
// alone cannot preserve a decline through offline sleep or reboot.
static String declineOfferedLockSession() {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return "";
    preferences.begin(Keys::session, false);
    const String id = preferences.getString(Keys::Session::sessionId, "");
    const bool pending = preferences.getString(Keys::Session::lockState, "") == Str::Protocol::PENDING;
    if (pending) preferences.putString(Keys::Session::lockState, Str::Protocol::ABANDONED);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
    return pending ? id : String();
}

// Whether a SERVER-pushed break should be suppressed because the device finished
// a break too recently (the stale-"break" echo of one just ended). Only the
// fixed floor applies: the wearer's break interval is a rule for the wearer's
// own button, and a keyholder may open the lock inside it. Does NOT gate
// user-initiated breaks or boot-time resume — only the server-sync call sites.
static bool serverBreakOnCooldown() {
    uint64_t breakLast = 0;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        breakLast = preferences.getULong64(Keys::Session::breakLast, 0);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    uint64_t now =
        ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;

    return isServerBreakEcho(breakLast, now);
}

// Applies the dashboard's answer to a state request. Shared by the pull that
// waits for its answer (pullSession) and the one the delay task polls
// (startSessionPull, LKBX::serviceStateRead).
static SyncResult applySessionResponse(int httpResponseCode,
                                       const String& payload,
                                       SyncOutcome* outcome = nullptr) {
    if (httpResponseCode != 200) {
        ESP_LOGD(SYNC_STATE_TAG, "Failed to reach server. Response code: %d",
                 httpResponseCode);
        return SyncResult::FAILED;
    }

    ESP_LOGD(SYNC_STATE_TAG, "HTTP Response code: %d", httpResponseCode);

    JsonDocument data;
    if (deserializeJson(data, payload) || !data.is<JsonObject>() ||
        !data[Str::Json::OK].is<bool>() || !data[Str::Json::OK].as<bool>() ||
        (!data[Str::Json::CLEAR].isNull() && !data[Str::Json::CLEAR].is<bool>())) {
        return SyncResult::FAILED;
    }

    if (data[Str::Json::OK] == false || data[Str::Json::OK] == "false") {
        ESP_LOGD(SYNC_STATE_TAG, "Failed to get state from server");
        return SyncResult::FAILED;
    }

    auto lockSession = data[Str::Json::DATA];
    bool shouldClear = data[Str::Json::CLEAR].as<bool>();

    if (shouldClear) {
        // The dashboard has no active session for the box. That ends the
        // session the box holds, but not every emergency lockout
        // (utils/emergencyRecord.h): one that stays keeps its page, so no
        // unlock is sent for it. The paddle is open already.
        bool recordRead = false;
        const bool lockoutStays = emergencyLockoutOutlastsClear(recordRead);
        if (!recordRead) return SyncResult::FAILED;
        const SyncResult result = clearState();
        if (outcome) {
            outcome->effect =
                lockoutStays ? SyncEffect::NONE : SyncEffect::MOTOR_UNLOCK;
        }
        return result;
    }

    // A pending server offer is saved as an unlocked offer by saveState().
    if (lockbox::security::httpSessionKind(lockSession) ==
        lockbox::security::HttpSessionKind::invalid) {
        ESP_LOGW(SYNC_STATE_TAG, "Session response has invalid fields");
        return SyncResult::FAILED;
    }

    SyncMessage session = {
        .isBreak = lockSession[Str::Json::IS_BREAK].as<bool>(),
        .lockEnd = lockSession[Str::Json::LOCK_END].as<uint64_t>(),
        .breakMax = lockSession[Str::Json::BREAK_MAX].as<uint64_t>(),
        .breakReg = lockSession[Str::Json::BREAK_REG].as<uint64_t>(),
        .breakLast = lockSession[Str::Json::BREAK_LAST].as<uint64_t>(),
        .duration = lockSession[Str::Json::DURATION].as<uint64_t>(),
        .isEUnlock = lockSession[Str::Json::IS_E_UNLOCK].as<bool>(),
        .lockStart = lockSession[Str::Json::LOCK_START].as<uint64_t>(),
        .lockState = lockSession[Str::Json::LOCK_STATE].as<String>(),
        .sessionId = lockSession[Str::Json::SESSION_ID].as<String>(),
        .lockName = lockSession[Str::Json::LOCK_NAME] | "",
        .requestedBy = lockSession[Str::Json::REQUESTED_BY] | "",
        .isTestLock = lockSession[Str::Json::IS_TEST_LOCK].as<bool>(),
        .isTimeDisp = lockSession[Str::Json::IS_TIME_DISP].as<bool>(),
        .displayElapsedTime =
            lockSession[Str::Json::DISPLAY_ELAPSED_TIME].as<bool>(),
    };
    applyFreezeSync(lockSession, session);
    applyChasterSync(lockSession, session);
    session.breakKeyholderOnly =
        lockSession[Str::Json::BREAK_KEYHOLDER_ONLY] | false;

    // saveState() resolves the result from lockState (LOCKED / BREAK / FAILED).
    return saveState(session, outcome);
}

static SyncResult pullSession(SyncOutcome* outcome = nullptr) {
    if (WiFiClass::status() != WL_CONNECTED) {
        ESP_LOGD(SYNC_STATE_TAG, "Not connected to WiFi");
        return SyncResult::FAILED;
    }

    String payload;
    int httpResponseCode = -1;
    const String token = signedInWithV2() ? deviceTokenForRequest() : String();
    if (token.length() > 0) {
        httpResponseCode = getWithDeviceToken(
            String(RAD_SERVER) + Str::Api::LOCKBOX_V2_STATE, token, &payload);
    } else {
        httpResponseCode = getWithDeviceSecret(
            String(RAD_SERVER) + Str::Api::LOCKBOX_STATE + getMacAddresss(),
            deviceSecretForRequest(), &payload);
    }

    return applySessionResponse(httpResponseCode, payload, outcome);
}

// The same request, handed to the HTTP worker for a caller that polls for the
// answer (takeHttpResult) and gives it to applySessionResponse. Null when the
// worker could not take it.
inline HttpPending* startSessionPull(int timeoutMs) {
    const String token = signedInWithV2() ? deviceTokenForRequest() : String();
    if (token.length() > 0) {
        return getDetachedWithDeviceToken(
            String(RAD_SERVER) + Str::Api::LOCKBOX_V2_STATE, token, timeoutMs);
    }
    return getDetachedWithDeviceSecret(
        String(RAD_SERVER) + Str::Api::LOCKBOX_STATE + getMacAddresss(),
        deviceSecretForRequest(), timeoutMs);
}

// return true if we pulled the state.
inline SyncResult syncState(SyncOutcome* outcome = nullptr) {
    // get the state from the server.

    auto isLocalPlay = false;

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        isLocalPlay = preferences.getBool(Keys::Base::isLocalPlay, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    if (isLocalPlay) {
        return SyncResult::FAILED;
    } else {
        return pullSession(outcome);
    }
}

#endif  // LOCKBOX_SYNCSTATE_H
