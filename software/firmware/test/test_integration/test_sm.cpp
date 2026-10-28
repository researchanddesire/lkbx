#include <WiFi.h>
#include <sys/time.h>

#include "constants/Pins.h"
#include "services/settings.h"
#include "state/LKBX.h"
#include "utils/clockCorrection.h"

#include "test_utils.h"

// ── SM boot ─────────────────────────────────────────────────────────────

void test_sm_boot_to_home() {
    TEST_ASSERT_TRUE_MESSAGE(testWifiUp, "WiFi must be connected");

    resetPrefsToUnlocked();

    String state;
    uint32_t start = millis();
    uint32_t lastLog = 0;
    bool reached = false;

    while ((millis() - start) < 60000) {
        state = currentState();
        if (state.startsWith("home")) {
            reached = true;
            break;
        }

        // Dead-end states that require physical button presses will
        // never self-resolve during an automated test run.  Fail
        // immediately instead of burning the full 60 s timeout.
        if (state.startsWith("init.preflight.wifiError")) {
            logHeap("wifiError");
            const char* hint = "Wi-Fi preflight failed.";
            if (state == "init.preflight.wifiErrorServer") {
                hint = "Server health check failed (likely TLS memory "
                       "allocation failure). Ensure RAD_SERVER is reachable "
                       "and there is enough contiguous heap for a TLS "
                       "handshake.";
            } else if (state == "init.preflight.wifiErrorNetwork") {
                hint = "Saved Wi-Fi network was not found or is out of range.";
            } else if (state == "init.preflight.wifiErrorAuth") {
                hint = "Saved Wi-Fi password was rejected.";
            } else if (state == "init.preflight.wifiErrorNoIp") {
                hint = "Wi-Fi connected but DHCP did not assign an IP.";
            }
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "SM stuck at %s — %s", state.c_str(), hint);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        if (state == "init.preflight.wifiSetup") {
            TEST_FAIL_MESSAGE(
                "SM stuck at init.preflight.wifiSetup — no saved WiFi "
                "credentials on device. Provision WiFi first.");
            return;
        }
        if (state == "init.preflight.mqttError") {
            logHeap("mqttError");
            TEST_FAIL_MESSAGE(
                "SM stuck at init.preflight.mqttError — MQTT connection "
                "or auth failed. Check broker address and heap for TLS.");
            return;
        }
        if (state.startsWith("calibrate")) {
            TEST_FAIL_MESSAGE(
                "SM entered calibrate — motor calibration flag not set "
                "in NVS. Ensure isMotorCalibrated=true before boot.");
            return;
        }

        uint32_t elapsed = millis() - start;
        if (elapsed - lastLog >= 5000) {
            lastLog = elapsed;
            char prog[128];
            snprintf(prog, sizeof(prog), "  [%us] SM at: %s",
                     (unsigned)elapsed / 1000, state.c_str());
            TEST_MESSAGE(prog);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    state = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "SM settled at: %s (reached_home=%d)",
             state.c_str(), reached);
    TEST_MESSAGE(msg);

    TEST_ASSERT_TRUE_MESSAGE(reached,
                             "SM should reach home within 60s. "
                             "Check server, MQTT broker, WiFi.");
}

// ── Helpers ──────────────────────────────────────────────────────────────

static void resetMotorCounters() {
    lkbx->motor.lockCount = 0;
    lkbx->motor.unlockCount = 0;
}

static void assertMotorCounts(uint32_t expectedLocks, uint32_t expectedUnlocks,
                              const char *context) {
    char msg[256];
    snprintf(msg, sizeof(msg),
             "%s: locks=%u (expected %u), unlocks=%u (expected %u)",
             context, lkbx->motor.lockCount, expectedLocks,
             lkbx->motor.unlockCount, expectedUnlocks);
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedLocks, lkbx->motor.lockCount, msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedUnlocks, lkbx->motor.unlockCount,
                                     msg);
}

static void setupTimerSessionPrefs(uint64_t lockEnd, uint64_t breakMaxMs) {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session, false);
        preferences.putULong64(Keys::Session::lockEnd, lockEnd);
        preferences.putBool(Keys::Session::isBreak, true);
        preferences.putULong64(Keys::Session::breakMax, breakMaxMs);
        preferences.putULong64(Keys::Session::breakReg, 0);
        preferences.putULong64(Keys::Session::breakLast, 0);
        preferences.putBool(Keys::Session::isActiveBreak, false);
        preferences.putULong64(Keys::Session::breakStart, 0);
        preferences.putBool(Keys::Session::isTimeDisp, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
}

static void setupEmergencyBlockPrefs(uint64_t emergencyEnd) {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isEBlocked, true);
        preferences.putULong64(Keys::Base::emergencyEnd, emergencyEnd);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
}

static bool isEmergencyBlockedPref() {
    bool isBlocked = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        isBlocked = preferences.getBool(Keys::Base::isEBlocked, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    return isBlocked;
}

static void setWallClockMsForTest(int64_t wallMs) {
    struct timeval tv{};
    tv.tv_sec = wallMs / 1000;
    tv.tv_usec = (wallMs % 1000) * 1000;
    settimeofday(&tv, nullptr);
}

static void restoreWallClockForTest(const ClockSnapshot &snapshot) {
    int64_t elapsedMs = currentMonotonicMs() - snapshot.monotonicMs;
    setWallClockMsForTest(snapshot.wallMs + elapsedMs);
}

static uint64_t getStoredLockEnd() {
    uint64_t lockEnd = 0;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        lockEnd = preferences.getULong64(Keys::Session::lockEnd, 0);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    return lockEnd;
}

static void setupDashboardTimedLockPrefs(uint64_t lockEnd) {
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isLocalPlay, false);
        preferences.putBool(Keys::Base::isLockTimer, true);
        preferences.end();

        preferences.begin(Keys::session, false);
        preferences.putULong64(Keys::Session::lockEnd, lockEnd);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
}

static void simulateNtpJumpFromCurrentWindow(int64_t jumpMs) {
    setWallClockMsForTest(currentWallClockMs() + jumpMs);
    recordNtpClockCorrection("test");
}

// ── Clock correction ────────────────────────────────────────────────────

void test_clock_correction_preserves_local_timer_on_backward_jump() {
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();
    ClockSnapshot restoreSnapshot = captureClockSnapshot();

    constexpr uint64_t DURATION_MS = 60000;
    constexpr int64_t CLOCK_JUMP_MS = -50 * 60 * 1000;

    TEST_ASSERT_TRUE_MESSAGE(saveLocalLockTimerDuration(DURATION_MS),
                             "Local timer should save");

    beginNtpClockCorrectionWindow();
    simulateNtpJumpFromCurrentWindow(CLOCK_JUMP_MS);
    TEST_ASSERT_TRUE_MESSAGE(processPendingLocalTimerClockCorrections(),
                             "Local timer should be adjusted");

    uint64_t lockEnd = getStoredLockEnd();
    int64_t remainingMs = (int64_t)lockEnd - currentWallClockMs();

    restoreWallClockForTest(restoreSnapshot);
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();

    TEST_ASSERT_TRUE_MESSAGE(remainingMs >= 55000 && remainingMs <= 65000,
                             "Remaining time should stay near original 60s");
}

void test_clock_correction_preserves_local_timer_on_forward_jump() {
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();
    ClockSnapshot restoreSnapshot = captureClockSnapshot();

    constexpr uint64_t DURATION_MS = 60000;
    constexpr int64_t CLOCK_JUMP_MS = 50 * 60 * 1000;

    TEST_ASSERT_TRUE_MESSAGE(saveLocalLockTimerDuration(DURATION_MS),
                             "Local timer should save");

    beginNtpClockCorrectionWindow();
    simulateNtpJumpFromCurrentWindow(CLOCK_JUMP_MS);
    TEST_ASSERT_TRUE_MESSAGE(processPendingLocalTimerClockCorrections(),
                             "Local timer should be adjusted");

    uint64_t lockEnd = getStoredLockEnd();
    int64_t remainingMs = (int64_t)lockEnd - currentWallClockMs();

    restoreWallClockForTest(restoreSnapshot);
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();

    TEST_ASSERT_TRUE_MESSAGE(remainingMs >= 55000 && remainingMs <= 65000,
                             "Remaining time should stay near original 60s");
}

void test_clock_correction_ignores_dashboard_timer() {
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();
    ClockSnapshot restoreSnapshot = captureClockSnapshot();

    constexpr uint64_t DURATION_MS = 60000;
    constexpr int64_t CLOCK_JUMP_MS = -50 * 60 * 1000;

    uint64_t originalLockEnd = nowMs() + DURATION_MS;
    setupDashboardTimedLockPrefs(originalLockEnd);

    beginNtpClockCorrectionWindow();
    simulateNtpJumpFromCurrentWindow(CLOCK_JUMP_MS);
    bool adjusted = processPendingLocalTimerClockCorrections();
    uint64_t lockEnd = getStoredLockEnd();

    restoreWallClockForTest(restoreSnapshot);
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();

    TEST_ASSERT_FALSE_MESSAGE(adjusted,
                              "Dashboard timer should not be adjusted");
    TEST_ASSERT_TRUE_MESSAGE(lockEnd == originalLockEnd,
                             "Dashboard lockEnd should remain absolute");
}

void test_clock_correction_ignores_timer_created_after_jump() {
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();
    ClockSnapshot restoreSnapshot = captureClockSnapshot();

    constexpr uint64_t DURATION_MS = 60000;
    constexpr int64_t CLOCK_JUMP_MS = -50 * 60 * 1000;

    beginNtpClockCorrectionWindow();
    simulateNtpJumpFromCurrentWindow(CLOCK_JUMP_MS);
    TEST_ASSERT_TRUE_MESSAGE(saveLocalLockTimerDuration(DURATION_MS),
                             "Local timer should save after correction");

    uint64_t lockEnd = getStoredLockEnd();
    int64_t remainingMs = (int64_t)lockEnd - currentWallClockMs();

    restoreWallClockForTest(restoreSnapshot);
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();

    TEST_ASSERT_TRUE_MESSAGE(remainingMs >= 55000 && remainingMs <= 65000,
                             "Timer created after jump should not be shifted");
}

void test_clock_correction_retries_after_mutex_contention() {
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();
    ClockSnapshot restoreSnapshot = captureClockSnapshot();

    constexpr uint64_t DURATION_MS = 60000;
    constexpr int64_t CLOCK_JUMP_MS = -50 * 60 * 1000;

    TEST_ASSERT_TRUE_MESSAGE(saveLocalLockTimerDuration(DURATION_MS),
                             "Local timer should save");

    beginNtpClockCorrectionWindow();
    simulateNtpJumpFromCurrentWindow(CLOCK_JUMP_MS);

    TEST_ASSERT_EQUAL_MESSAGE(
        pdTRUE, xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000),
        "Test should acquire preferences mutex");
    bool adjustedWhileBusy = processPendingLocalTimerClockCorrections();
    xSemaphoreGive(preferencesMutex);

    bool adjustedAfterRelease = processPendingLocalTimerClockCorrections();
    uint64_t lockEnd = getStoredLockEnd();
    int64_t remainingMs = (int64_t)lockEnd - currentWallClockMs();

    restoreWallClockForTest(restoreSnapshot);
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();

    TEST_ASSERT_FALSE_MESSAGE(adjustedWhileBusy,
                              "Correction should wait if mutex is busy");
    TEST_ASSERT_TRUE_MESSAGE(adjustedAfterRelease,
                             "Correction should apply after mutex release");
    TEST_ASSERT_TRUE_MESSAGE(remainingMs >= 55000 && remainingMs <= 65000,
                             "Remaining time should stay near original 60s");
}

void test_clock_correction_version_changes_on_adjustment() {
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();
    ClockSnapshot restoreSnapshot = captureClockSnapshot();

    constexpr uint64_t DURATION_MS = 60000;
    constexpr int64_t CLOCK_JUMP_MS = -50 * 60 * 1000;

    TEST_ASSERT_TRUE_MESSAGE(saveLocalLockTimerDuration(DURATION_MS),
                             "Local timer should save");
    uint32_t beforeVersion = localTimerCorrectionVersion();

    beginNtpClockCorrectionWindow();
    simulateNtpJumpFromCurrentWindow(CLOCK_JUMP_MS);
    processPendingLocalTimerClockCorrections();
    uint32_t afterVersion = localTimerCorrectionVersion();

    restoreWallClockForTest(restoreSnapshot);
    resetPrefsToUnlocked();
    resetClockCorrectionForTest();

    TEST_ASSERT_TRUE_MESSAGE(afterVersion > beforeVersion,
                             "Timer task should see correction version change");
}

// ── Motor count: single lock ─────────────────────────────────────────────

void test_motor_count_single_lock() {
    if (!lkbx || !lkbx->sm || !smAtHome()) {
        TEST_FAIL_MESSAGE("Skipped: SM not at home");
        return;
    }

    resetMotorCounters();

    lkbx->sm->process_event(Lock{});
    bool reached = waitFor(smInLockedState, 15000, 250);

    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(), "Should not error during lock");
    TEST_ASSERT_TRUE_MESSAGE(reached, "Should reach locked state");

    // Lock{} → lockAction → motor.lock() = 1 lock, 0 unlocks.
    // Goes through locking → locking.preflight → locked.
    // Preflight confirms paddle+backplate, no extra motor calls.
    assertMotorCounts(1, 0, "single Lock{}");

    ensureHome();
}

// ── Motor count: single unlock from locked ───────────────────────────────

void test_motor_count_single_unlock() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    // Lock first (don't count this)
    lkbx->sm->process_event(Lock{});
    bool locked = waitFor(smInLockedState, 15000, 250);
    if (!locked || smInErrorState()) {
        ensureHome();
        TEST_FAIL_MESSAGE("Skipped: could not lock (sensor issue)");
        return;
    }

    resetMotorCounters();

    lkbx->sm->process_event(Unlock{});
    bool reached = waitFor(smAtHome, 15000, 250);

    if (!reached && smInErrorState()) {
        reached = ensureHome();
    }

    TEST_ASSERT_TRUE_MESSAGE(reached, "Should reach home after unlock");

    // Unlock{} from locked → unlockAction → motor.unlock() = 1 unlock.
    // Goes through locking → locking.preflight.
    // Preflight: isLocked()=false, !isPaddleLocked()=true → Done{}.
    // Catch-all: sendMessage("unlocked") → home. NO motor.unlock() call.
    assertMotorCounts(0, 1, "single Unlock{} from locked");
}

// ── Motor count: lock timer → break → re-lock ───────────────────────────

void test_motor_count_break_roundtrip() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    resetMotorCounters();

    uint64_t lockEnd = nowMs() + 3 * 60 * 1000;
    setupTimerSessionPrefs(lockEnd, 30000);
    lkbx->sm->process_event(LockTimer{.lockEnd = lockEnd});

    bool locked = waitFor(
        []() { return currentState() == "locked.timer"; }, 15000, 250);
    TEST_ASSERT_TRUE_MESSAGE(locked, "Should reach locked.timer");
    assertMotorCounts(1, 0, "after initial LockTimer");

    // Enter break
    lkbx->sm->process_event(Break{});
    bool onBreak = waitFor(
        []() { return currentState() == "locked.break"; }, 10000, 250);
    TEST_ASSERT_TRUE_MESSAGE(onBreak, "Should enter locked.break");

    // locked.break on_entry → movePaddleToUnlock → motor.unlock(false)
    assertMotorCounts(1, 1, "after entering break");

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    // End break
    lkbx->sm->process_event(ButtonPress{});
    bool reLocked = waitFor(
        []() { return currentState() == "locked.timer"; }, 15000, 250);
    TEST_ASSERT_TRUE_MESSAGE(reLocked, "Should return to locked.timer");

    // locked.break on_exit → movePaddleToLock → motor.lock(false) = +1 lock.
    // Through locking → locking.preflight → locked.timer. No extra motor calls.
    assertMotorCounts(2, 1, "after break roundtrip");

    ensureHome();
}

// ── Motor count: timer expiry unlock ─────────────────────────────────────

void test_motor_count_timer_expiry() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    resetMotorCounters();

    constexpr uint64_t SHORT_LOCK_MS = 10 * 1000;
    uint64_t lockEnd = nowMs() + SHORT_LOCK_MS;
    setupTimerSessionPrefs(lockEnd, 30000);
    lkbx->sm->process_event(LockTimer{.lockEnd = lockEnd});

    bool locked = waitFor(
        []() { return currentState() == "locked.timer"; }, 15000, 250);
    TEST_ASSERT_TRUE_MESSAGE(locked, "Should reach locked.timer");
    assertMotorCounts(1, 0, "after initial LockTimer");

    // Wait for timer to expire
    TEST_MESSAGE("  Waiting for 10s lock to expire...");
    bool unlocked = waitFor(smAtHome, SHORT_LOCK_MS + 15000, 500);

    String state = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "After timer expiry: %s", state.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(unlocked, "Should reach home after timer expiry");

    // Timer fires Unlock{} → unlockAction → motor.unlock() = 1 unlock.
    // locking.preflight catch-all: no motor call, just sendMessage → home.
    // Total: 1 lock, 1 unlock. NOT 1 lock, 2 unlocks.
    assertMotorCounts(1, 1, "after timer expiry");
}

void test_emergency_block_uses_emergency_end_not_session_lock_end() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    resetMotorCounters();

    constexpr uint64_t OLD_LOCK_REMAINING_MS = 5 * 1000;
    constexpr uint64_t EMERGENCY_BLOCK_MS = 60 * 1000;
    uint64_t oldLockEnd = nowMs() + OLD_LOCK_REMAINING_MS;
    uint64_t emergencyEnd = nowMs() + EMERGENCY_BLOCK_MS;

    setupTimerSessionPrefs(oldLockEnd, 30000);
    setupEmergencyBlockPrefs(emergencyEnd);

    lkbx->sm->process_event(LockTimer{.lockEnd = oldLockEnd});

    bool blocked = waitFor(
        []() { return currentState() == "emergency.blocked"; }, 15000, 250);
    TEST_ASSERT_TRUE_MESSAGE(blocked, "Should enter emergency.blocked");
    TEST_ASSERT_TRUE_MESSAGE(isEmergencyBlockedPref(),
                             "Emergency block pref should be set");

    uint64_t now = nowMs();
    uint64_t waitPastOldLockEnd =
        oldLockEnd > now ? oldLockEnd - now + 2000 : 2000;
    TEST_MESSAGE("  Waiting past original session.lockEnd...");
    vTaskDelay(waitPastOldLockEnd / portTICK_PERIOD_MS);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "emergency.blocked", currentState().c_str(),
        "Emergency block should ignore expired session.lockEnd");
    TEST_ASSERT_TRUE_MESSAGE(isEmergencyBlockedPref(),
                             "Emergency block should remain set");

    lkbx->sm->process_event(Unlock{});
    bool returnedHome = waitFor(smAtHome, 15000, 250);
    TEST_ASSERT_TRUE_MESSAGE(returnedHome,
                             "Remote unlock override should clear block");
}

// ── Full lifecycle (5 min lock, 2 breaks, timer expiry) ──────────────────

void test_sm_lock_timer_full_lifecycle() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }

    ensureHome();
    if (!smAtHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    resetMotorCounters();

    constexpr uint64_t LOCK_DURATION_MS = 5 * 60 * 1000;
    constexpr uint64_t BREAK_MAX_MS     = 60 * 1000;

    uint64_t lockEnd = nowMs() + LOCK_DURATION_MS;
    setupTimerSessionPrefs(lockEnd, BREAK_MAX_MS);
    lkbx->sm->process_event(LockTimer{.lockEnd = lockEnd});

    bool locked = waitFor(
        []() { return currentState() == "locked.timer"; }, 15000, 250);
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(),
                              "SM should not error after LockTimer");
    TEST_ASSERT_TRUE_MESSAGE(locked, "SM should reach locked.timer");
    assertMotorCounts(1, 0, "after initial lock");

    // ── Break 1: on-time ──

    TEST_MESSAGE("=== Break 1: on-time break ===");

    lkbx->sm->process_event(Break{});
    bool onBreak = waitFor(
        []() { return currentState() == "locked.break"; }, 10000, 250);
    TEST_ASSERT_TRUE_MESSAGE(onBreak, "Should enter locked.break");
    assertMotorCounts(1, 1, "break 1 entered");

    vTaskDelay(10000 / portTICK_PERIOD_MS);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("locked.break", currentState().c_str(),
                                     "Should still be on break after 10s");

    lkbx->sm->process_event(ButtonPress{});
    bool reLocked1 = waitFor(
        []() { return currentState() == "locked.timer"; }, 15000, 250);
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(), "No error after break 1");
    TEST_ASSERT_TRUE_MESSAGE(reLocked1, "Should re-lock after break 1");
    assertMotorCounts(2, 1, "break 1 ended");

    // ── Break 2: overstay ──

    TEST_MESSAGE("=== Break 2: missed break ===");

    lkbx->sm->process_event(Break{});
    onBreak = waitFor(
        []() { return currentState() == "locked.break"; }, 10000, 250);
    TEST_ASSERT_TRUE_MESSAGE(onBreak, "Should enter locked.break for break 2");
    assertMotorCounts(2, 2, "break 2 entered");

    TEST_MESSAGE("  Waiting 65s for break to expire...");
    vTaskDelay(65000 / portTICK_PERIOD_MS);

    bool leftBreakAfterExpiry = waitFor(
        []() { return currentState() != "locked.break"; }, 15000, 250);
    TEST_ASSERT_TRUE_MESSAGE(leftBreakAfterExpiry,
                             "Should leave locked.break when break expires");

    if (currentState() == "locked.break") {
        lkbx->sm->process_event(ButtonPress{});
    }
    bool reLocked2 = waitFor(
        []() { return currentState() == "locked.timer"; }, 15000, 250);
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(), "No error after break 2");
    TEST_ASSERT_TRUE_MESSAGE(reLocked2, "Should re-lock after break 2");
    assertMotorCounts(3, 2, "break 2 ended");

    // ── Timer expiry ──

    TEST_MESSAGE("=== Waiting for 5m lock timer to expire ===");
    bool unlocked = waitWithProgress(smAtHome, LOCK_DURATION_MS + 30000);

    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(), "No error after timer expiry");
    TEST_ASSERT_TRUE_MESSAGE(unlocked, "Should auto-unlock after timer");

    // Timer fires Unlock{}: +1 unlock. Preflight catch-all: no motor call.
    assertMotorCounts(3, 3, "FINAL after timer expiry");

    ensureHome();
}

// ── Ticket Scenario Test ─────────────────────────────────────────────────
//
// Tests the EXACT scenario from the integration test ticket:
// - 5-minute lock (shortened to 90s for CI)
// - Break at ~halfway point
// - 30-second break duration
// - End break, return to locked.timer
// - Wait remaining time, auto-unlock to home
//
// Uses shortened timings for CI. For full 5-minute test, adjust constants.

void test_sm_ticket_scenario_lock_break_unlock() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }
    if (!ensureHome()) {
        TEST_FAIL_MESSAGE("Skipped: could not reach home");
        return;
    }

    // Ticket specifies: 5-minute lock, break at 3-min mark, 30s break, 1:30 remaining
    // For CI, we use shortened timings but same ratios:
    // - 90s total lock (instead of 5 min)
    // - Break at 45s (halfway, instead of 3 min)
    // - 30s break duration (as specified)
    // - 45s remaining after break (instead of 1:30)
    constexpr uint64_t LOCK_DURATION_MS = 90 * 1000;   // 90s for CI (ticket: 5 min)
    constexpr uint64_t BREAK_MAX_MS = 30 * 1000;       // 30s (as per ticket)
    constexpr uint64_t WAIT_BEFORE_BREAK_MS = 45 * 1000;  // halfway point

    TEST_MESSAGE("=== Ticket Scenario: Lock → Break → Unlock ===");

    resetMotorCounters();

    // Step 1: Setup and start timed lock
    uint64_t lockEnd = nowMs() + LOCK_DURATION_MS;
    setupTimerSessionPrefs(lockEnd, BREAK_MAX_MS);

    lkbx->sm->process_event(LockTimer{.lockEnd = lockEnd});
    bool locked = waitFor(smInLockedState, 15000, 250);
    TEST_ASSERT_TRUE_MESSAGE(locked, "Step 1: Should lock successfully");
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(), "Step 1: No error during lock");
    assertMotorCounts(1, 0, "Step 1: after lock");

    // Step 2: Wait until "3-minute mark" (scaled: 45s for 90s lock)
    TEST_MESSAGE("Step 2: Waiting for break point...");
    vTaskDelay(WAIT_BEFORE_BREAK_MS / portTICK_PERIOD_MS);

    // Step 3: Take a break
    lkbx->sm->process_event(Break{});
    bool onBreak = waitFor(
        []() { return currentState() == "locked.break"; }, 10000, 250);
    TEST_ASSERT_TRUE_MESSAGE(onBreak, "Step 3: Should enter locked.break");

    // Verify NVS isActiveBreak flag is set
    bool isActiveBreak = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        isActiveBreak = preferences.getBool(Keys::Session::isActiveBreak, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    TEST_ASSERT_TRUE_MESSAGE(isActiveBreak, "Step 3: isActiveBreak should be true");

    // Step 4: Wait during break (ticket: 30 seconds, we'll wait 10s)
    TEST_MESSAGE("Step 4: On break, waiting 10s...");
    vTaskDelay(10000 / portTICK_PERIOD_MS);

    // Step 5: End break successfully
    lkbx->sm->process_event(ButtonPress{});
    bool reLocked = waitFor(
        []() { return currentState() == "locked.timer"; }, 15000, 250);
    TEST_ASSERT_TRUE_MESSAGE(reLocked, "Step 5: Should return to locked.timer");
    assertMotorCounts(2, 1, "Step 5: after break roundtrip");

    // Verify NVS isActiveBreak flag is cleared
    bool isActiveAfter = true;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        isActiveAfter = preferences.getBool(Keys::Session::isActiveBreak, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    TEST_ASSERT_FALSE_MESSAGE(isActiveAfter, "Step 5: isActiveBreak should be false");

    // Step 6: Wait for remaining time and auto-unlock
    // Remaining time is lockEnd - nowMs(), plus buffer
    uint64_t remaining = lockEnd > nowMs() ? (lockEnd - nowMs()) : 0;
    char waitMsg[128];
    snprintf(waitMsg, sizeof(waitMsg),
             "Step 6: Waiting %llus for auto-unlock...", remaining / 1000);
    TEST_MESSAGE(waitMsg);

    bool unlocked = waitWithProgress(smAtHome, remaining + 30000);
    TEST_ASSERT_TRUE_MESSAGE(unlocked, "Step 6: Should auto-unlock after timer");
    TEST_ASSERT_FALSE_MESSAGE(smInErrorState(), "Step 6: No error after unlock");
    assertMotorCounts(2, 2, "Step 6: after auto-unlock");

    String finalState = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "Final state: %s", finalState.c_str());
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(finalState.startsWith("home"),
                             "Should be at home after full lifecycle");

    ensureHome();
}

void test_sm_final_unlock() {
    if (!lkbx || !lkbx->sm) {
        TEST_FAIL_MESSAGE("Skipped: SM not initialized");
        return;
    }

    bool ok = ensureHome();

    String state = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "Final state: %s (home=%d)", state.c_str(), ok);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(ok, "SM should be at home before MQTT dispatch tests");
}

// ── Registration ────────────────────────────────────────────────────────

void register_sm_boot_tests() {
    RUN_TEST(test_sm_boot_to_home);
}

void register_sm_traversal_tests() {
    RUN_TEST(test_clock_correction_preserves_local_timer_on_backward_jump);
    RUN_TEST(test_clock_correction_preserves_local_timer_on_forward_jump);
    RUN_TEST(test_clock_correction_ignores_dashboard_timer);
    RUN_TEST(test_clock_correction_ignores_timer_created_after_jump);
    RUN_TEST(test_clock_correction_retries_after_mutex_contention);
    RUN_TEST(test_clock_correction_version_changes_on_adjustment);
    RUN_TEST(test_motor_count_single_lock);
    RUN_TEST(test_motor_count_single_unlock);
    RUN_TEST(test_motor_count_break_roundtrip);
    RUN_TEST(test_motor_count_timer_expiry);
    RUN_TEST(test_emergency_block_uses_emergency_end_not_session_lock_end);
    RUN_TEST(test_sm_lock_timer_full_lifecycle);
    RUN_TEST(test_sm_ticket_scenario_lock_break_unlock);
    RUN_TEST(test_sm_final_unlock);
}
