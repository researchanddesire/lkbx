#ifndef TEST_INTEGRATION_UTILS_H
#define TEST_INTEGRATION_UTILS_H

#include <Arduino.h>
#include <unity.h>

#include <cmath>

#include "constants/Pins.h"
#include "motor/Motor.h"
#include "services/machine.h"
#include "services/settings.h"
#include "state/LKBX.h"
#include "test_helpers.h"

// Shared mutable state — defined in main.cpp, available to all test files.
extern Motor *motor;
extern bool testWifiUp;
extern AuthResponse cachedAuth;

// ── SM query helpers ────────────────────────────────────────────────────

static inline String currentState() {
    String name;
    if (lkbx && lkbx->sm) {
        lkbx->sm->visit_current_states(
            [&](auto state) { name = state.c_str(); });
    }
    return name;
}

static inline bool smAtHome() { return currentState().startsWith("home"); }

static inline bool smInErrorState() {
    return currentState().startsWith("locking.error");
}

static inline bool smIsLockedOk() {
    String s = currentState();
    if (s.startsWith("locked")) return true;
    if (s.startsWith("locking") && !s.startsWith("locking.error")) return true;
    return false;
}

static inline bool smInLockedState() {
    return currentState().startsWith("locked");
}

// ── NVS helpers ─────────────────────────────────────────────────────────

static inline void resetPrefsToUnlocked() {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocked, false);
        preferences.putBool(Keys::Base::isLockTimer, false);
        preferences.putBool(Keys::Base::isEBlocked, false);
        preferences.putBool(Keys::Base::isLocalPlay, false);
        preferences.end();

        preferences.begin(Keys::session, false);
        preferences.putBool(Keys::Session::isActiveBreak, false);
        preferences.putULong64(Keys::Session::breakStart, 0);
        preferences.end();

        xSemaphoreGive(preferencesMutex);
    }
}

// ── SM recovery helpers ─────────────────────────────────────────────────

static inline bool ensureHome() {
    if (smAtHome()) return true;

    waitFor(
        []() {
            String s = currentState();
            return s.startsWith("home") || s.startsWith("locked") ||
                   s.startsWith("locking.error") || s == "locking.preflight";
        },
        10000, 250);

    if (smAtHome()) return true;

    resetPrefsToUnlocked();

    if (smInErrorState()) {
        lkbx->sm->process_event(ButtonPress{});
        waitFor(smAtHome, 15000, 250);
        if (smAtHome()) return true;

        if (smInErrorState()) {
            resetPrefsToUnlocked();
            lkbx->sm->process_event(ButtonPress{});
            waitFor(smAtHome, 15000, 250);
            if (smAtHome()) return true;
        }
    }

    lkbx->sm->process_event(Unlock{});
    waitFor(smAtHome, 15000, 250);
    return smAtHome();
}

static inline bool ensureLocked() {
    if (smInLockedState()) return true;
    if (!ensureHome()) return false;
    lkbx->sm->process_event(Lock{});
    bool ok = waitFor(smInLockedState, 15000, 250);
    if (!ok && smInErrorState()) {
        char msg[128];
        snprintf(msg, sizeof(msg), "ensureLocked: ended in error state %s",
                 currentState().c_str());
        TEST_MESSAGE(msg);
    }
    return ok;
}

// ── Progress-logging wait ───────────────────────────────────────────────

static inline bool waitWithProgress(bool (*targetCheck)(), uint32_t timeoutMs,
                                    uint32_t logIntervalMs = 15000) {
    uint32_t start = millis();
    uint32_t lastLog = 0;
    char msg[128];

    while ((millis() - start) < timeoutMs) {
        if (targetCheck()) return true;
        if (smInErrorState()) return false;

        uint32_t elapsed = millis() - start;
        if (elapsed - lastLog >= logIntervalMs) {
            lastLog = elapsed;
            snprintf(msg, sizeof(msg), "  [%us] SM at: %s",
                     (unsigned)elapsed / 1000, currentState().c_str());
            TEST_MESSAGE(msg);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    return targetCheck();
}

#endif  // TEST_INTEGRATION_UTILS_H
