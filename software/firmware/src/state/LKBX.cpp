#include "utils/trustedClock.h"
#include "LKBX.h"

#include <Arduino.h>

#include "../constants/Images.h"
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/FreeSansBold12pt7b.h"
#include "constants/Colors.h"
#include "constants/fonts/Workbench_20pt7b.h"
#include "constants/strings.h"
#include "services/display.h"
#include "services/hardwareHealth.h"
#include "services/ntp.h"
#include "structs/Version.h"
#include "utils/clearDisplay.h"
#include "utils/clockCorrection.h"
#include "utils/stringFormat.h"

namespace sml = boost::sml;
using namespace sml;

long delayTimeout = 0;

void LKBX::immediateTransition() { sm->process_event(Done{}); }

// Now we can define the OSSM constructor since OSSMStateMachine::operator() is
// fully defined
LKBX::LKBX(Adafruit_ST7789 &display, AiEsp32RotaryEncoder &encoder,
           Motor &motor, esp_mqtt_client_handle_t &client)
    : display(display),
      encoder(encoder),
      motor(motor),
      client(client),
      sm(std::make_unique<
          sml::sm<InnerStateMachine, sml::thread_safe<ESP32RecursiveMutex>,
                  sml::logger<StateLogger>>>(logger, *this)) {}

void LKBX::startDeferredServices() {
    ESP_LOGI(BOOT_TAG, "Update check passed, starting deferred services");
    initNTP();
}

// Boot-time clock sync. Always runs during preflight so isTimeSynced is set
// before the lock timer UI and MQTT timestamps rely on it. When online, sync
// from verified HTTPS even if preferences restored a stale clock. Offline with a
// cached clock is enough to show time remaining.
void LKBX::syncClockOnBootTask(void *pvParameters) {
    LKBX *plkbx = static_cast<LKBX *>(pvParameters);

    if (!isOffline()) {
        if (!isTimeSynced) {
            updateTime();  // authenticated time or preserve the previous clock
        }
    } else if (isWallClockSet()) {
        isTimeSynced = true;
    }

    TaskHandles::syncClock = nullptr;
    plkbx->sm->process_event(Done{});
    vTaskDelete(nullptr);
}

// Clock retry while a LOCKED report is held for an unverified clock. Unlike
// the boot sync it drives no state transition.
void LKBX::retryClockSyncTask(void *) {
    if (!isOffline() && !isTimeSynced) {
        updateTime();  // authenticated time or preserve the previous clock
    }
    TaskHandles::syncClock = nullptr;
    vTaskDelete(nullptr);
}

/**
 * This task displays the RD logo centered on screen
 * with version text below, then proceeds to the next state.
 * @param pvParameters
 */
void LKBX::drawHelloTask(void *pvParameters) {
    LKBX *plkbx = (LKBX *)pvParameters;

    constexpr int logoWidth = 64;
    constexpr int logoHeight = 50;
    constexpr int textPadding = 16;

    // Check if alpha version
    bool isAlpha = false;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        isAlpha = preferences.getBool(Keys::Base::isAlpha, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    // Build version string
    String versionText = isAlpha ? Str::Screen::HELLO_ALPHA_VERSION_PREFIX
                                 : Str::Screen::HELLO_VERSION_PREFIX;
    versionText += currentVersionString;

    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        // Fill only below status bar to preserve header
        plkbx->display.fillRect(0, Display::StatusbarHeight, Display::WIDTH,
                                Display::HEIGHT - Display::StatusbarHeight,
                                Colors::black);

        // Draw logo centered in content area, offset up to make room for text
        int contentHeight = Display::HEIGHT - Display::StatusbarHeight;
        int logoX = (Display::WIDTH - logoWidth) / 2;
        int logoY =
            Display::StatusbarHeight + (contentHeight - logoHeight) / 2 - 30;

        plkbx->display.drawXBitmap(logoX, logoY, Images::RDLogo, logoWidth,
                                   logoHeight, Colors::white);

        // Set up text rendering
        plkbx->display.setFont(&FreeSans9pt7b);
        plkbx->display.setTextColor(Colors::white);

        int16_t x1, y1;
        uint16_t w, h;

        // Draw "Chastity Lockbox" centered below logo
        String titleText = Str::Screen::HELLO_TITLE_TEXT;
        plkbx->display.getTextBounds(titleText, 0, 0, &x1, &y1, &w, &h);
        int titleX = (Display::WIDTH - w) / 2;
        int titleY = logoY + logoHeight + textPadding + h;
        plkbx->display.setCursor(titleX, titleY);
        plkbx->display.print(titleText);

        // Draw version string centered below title (smaller built-in font)
        plkbx->display.setFont(NULL);  // Use built-in 5x7 font
        plkbx->display.getTextBounds(versionText, 0, 0, &x1, &y1, &w, &h);
        int versionX = (Display::WIDTH - w) / 2;
        int versionY = titleY + textPadding + h;
        plkbx->display.setCursor(versionX, versionY);
        plkbx->display.print(versionText);

        xSemaphoreGive(displayMutex);
    }

    vTaskDelay(2500 / portTICK_PERIOD_MS);

    plkbx->sm->process_event(Done{});
    TaskHandles::drawHello = nullptr;
    vTaskDelete(nullptr);
}

void LKBX::drawHello() {
    if (TaskHandles::drawHello != nullptr) {
        ESP_LOGW(TASKDEBUG_TAG, "drawHello task already running, skipping");
        return;
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating draw hello task");
    if (!startTaskChecked(drawHelloTask, Tasks::drawHello, this,
                          &TaskHandles::drawHello)) {
        // Boot carries on without the logo: the delay task sends the Done
        // this task would have sent.
        delayTimeout = 2500;
    }
}

void LKBX::delayTask(void *pvParameters) {
    LKBX *plkbx = (LKBX *)pvParameters;
    long lastLoopTime = millis();
    while (true) {
        long now = millis();
        hardwareHealthHeartbeat(plkbx->sm->is("home.idle"_s));
        long elapsed = now - lastLoopTime;
        lastLoopTime = now;

        if (delayTimeout > 0) {
            delayTimeout -= elapsed;
            if (delayTimeout <= 0) {
                delayTimeout = 0;
                plkbx->sm->process_event(Done{});
            }
        }

        // A Chaster session's question runs here, one step per pass, so
        // asking never depends on memory for a task of its own.
        plkbx->serviceChasterAsk();

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void LKBX::initDelayTask() {
    if (startTaskChecked(delayTask, Tasks::delayDispatch, this,
                         &TaskHandles::delayDispatch)) {
        return;
    }
    // Every timed step (the lock animation, preflight, the boot fallbacks)
    // and the Chaster question run on this task: without it a locked box
    // would stop on its first page. This runs in setup(), before TLS and the
    // page tasks take memory, so starting again is the way out.
    ESP_LOGE(TASKDEBUG_TAG, "Could not start the delay task; restarting");
    delay(1000);
    esp_restart();
}

void LKBX::drawPreflightTask(void *pvParameters) {
    LKBX *plkbx = (LKBX *)pvParameters;

    // The loading animation owns the screen now — just give it a brief moment
    // on screen before branching to the first preflight step.
    vTaskDelay(500 / portTICK_PERIOD_MS);

    plkbx->sm->process_event(Done{});
    TaskHandles::preflightScreen = nullptr;
    vTaskDelete(nullptr);
}

void LKBX::startPreflightScreen() {
    if (TaskHandles::preflightScreen != nullptr) {
        ESP_LOGW(TASKDEBUG_TAG, "preflightScreen task already running, skipping");
        return;
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating preflight screen task");
    if (!startTaskChecked(drawPreflightTask, Tasks::preflightScreen, this,
                          &TaskHandles::preflightScreen)) {
        // The delay task sends the Done this task would have sent.
        delayTimeout = 500;
    }
}

void LKBX::startPreflight() const {
    bool paddleLocked = isPaddleLocked();
#ifdef BYPASS_BACKPLATE_CHECK
    bool backplateClosed = true;
#else
    bool backplateClosed = isBackplateClosed();
#endif
    bool locked = isLocked();

    ESP_LOGI(PREFLIGHT_TAG,
             "isLocked: %d, isPaddleLocked: %d, isBackplateClosed: %d", locked,
             paddleLocked, backplateClosed);

    if (locked) {
        if (paddleLocked && backplateClosed) {
            sm->process_event(Done{});
        } else if (!paddleLocked) {
            sm->process_event(Error{.error = ErrorT::CANNOT_LOCK});
        } else if (!backplateClosed) {
            sm->process_event(Error{.error = ErrorT::NO_BACKPLATE});
        }
    } else {
        if (!paddleLocked) {
            sm->process_event(Done{});
        } else {
            sm->process_event(Error{.error = ErrorT::CANNOT_UNLOCK});
        }
    }
}

void LKBX::startUnlockTimerTask(void *pvParameters) {
    // parse ossm from the parameters
    LKBX *plkbx = (LKBX *)pvParameters;

    auto isInCorrectState = [](LKBX *ossm) {
        return ossm->sm->is("locked.timer"_s) || ossm->sm->is("lockMenu"_s) ||
               ossm->sm->is("locked.ready"_s) ||
               ossm->sm->is("locked.ready.addTime"_s) ||
               ossm->sm->is("lockMenu.sleep"_s) ||
               ossm->sm->is("emergency.blocked"_s) ||
               ossm->sm->is("locked.frozen"_s);
    };

    auto shouldDrawCountdown = [](LKBX *ossm) {
        return ossm->sm->is("locked.timer"_s) ||
               ossm->sm->is("locked.frozen"_s) ||
               ossm->sm->is("emergency.blocked"_s);
    };

    uint64_t currentDatetimeMs;
    uint64_t lockEndDatetimeMs = 0;
    uint64_t lockStartDatetimeMs = 0;
    bool isEmergencyBlocked = false;
    bool isTimeDisp = true;
    bool displayElapsedTime = false;
    // Chaster freeze: "Frozen by Chaster" holds the countdown at frozenAt. The
    // anchor is a server epoch, so the paused value is identical after a reboot;
    // without one the banner is shown on its own rather than a time that would
    // silently change.
    bool isFrozen = false;
    uint64_t frozenAtMs = 0;
    char freezeReason[kFreezeReasonDisplayBytes] = "";
    // The dashboard also holds a session Chaster would refuse to unlock, with
    // a reason that says so. That page reads "waiting", and its reason line
    // shows only the detail.
    bool isUnlockHold = false;
    auto readHeldReason = [&]() {
        const String stored =
            preferences.getString(Keys::Session::freezeReason, "");
        isUnlockHold = isUnlockHoldReason(stored.c_str());
        boundedFreezeReason(heldReasonLine(stored.c_str()), freezeReason,
                            sizeof(freezeReason));
    };
    uint32_t seenCorrectionVersion = UINT32_MAX;
    uint32_t seenFreezeVersion = freezeDisplayVersion();
    uint32_t refreshedAt = 0;
    // Snapshot of the last frame actually blitted. While frozen the countdown is
    // static, so a repaint is a no-op unless one of these changed. This keeps a
    // periodic poll (or a duplicate freeze sync) from redrawing the frozen page
    // every second.
    struct FrozenDisplay {
        bool valid = false;
        bool isEmergencyBlocked = false;
        bool freezeHolds = false;
        bool isTimeDisp = true;
        bool displayElapsedTime = false;
        uint64_t remainingTimeMs = 0;
        // Only meaningful while frozen. Zero whenever the live countdown is
        // drawn, so leaving the freeze always forces one repaint.
        uint32_t counter = 0;
        char reason[kFreezeReasonDisplayBytes] = "";
    };
    FrozenDisplay lastFrozenDisplay;

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        isEmergencyBlocked = preferences.getBool(Keys::Base::isEBlocked, false);
        if (isEmergencyBlocked) {
            lockEndDatetimeMs =
                preferences.getULong64(Keys::Base::emergencyEnd);
        }
        preferences.end();

        preferences.begin(Keys::session);
        lockStartDatetimeMs = preferences.getULong64(Keys::Session::lockStart, 0);
        if (!isEmergencyBlocked || lockEndDatetimeMs == 0) {
            lockEndDatetimeMs =
                preferences.getULong64(Keys::Session::lockEnd, lockEndDatetimeMs);
        }
        isTimeDisp = preferences.getBool(Keys::Session::isTimeDisp, true);
        displayElapsedTime =
            preferences.getBool(Keys::Session::displayElapsedTime, false);
        isFrozen = preferences.getBool(Keys::Session::isFrozen, false);
        frozenAtMs = preferences.getULong64(Keys::Session::frozenAt, 0);
        readHeldReason();
        preferences.end();

        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(UNLOCK_TIMER_TAG, "lockEndDatetimeMs: %llu", lockEndDatetimeMs);

    // Define the height for the time canvas
    const int TIME_CANVAS_HEIGHT = 48;
    GFXcanvas1 timeRemainingCanvas(Display::WIDTH, TIME_CANVAS_HEIGHT);

    while (isInCorrectState(plkbx)) {
        if (!isEmergencyBlocked) {
            processPendingLocalTimerClockCorrections();
            uint32_t correctionVersion = localTimerCorrectionVersion();
            const uint32_t freezeVersion = freezeDisplayVersion();
            if (correctionVersion != seenCorrectionVersion ||
                freezeVersion != seenFreezeVersion ||
                uint32_t(millis() - refreshedAt) >= 1000) {
                if (xSemaphoreTake(preferencesMutex,
                                   portTICK_PERIOD_MS * 1000) == pdTRUE) {
                    preferences.begin(Keys::session);
                    lockEndDatetimeMs = preferences.getULong64(
                        Keys::Session::lockEnd, lockEndDatetimeMs);
                    lockStartDatetimeMs = preferences.getULong64(
                        Keys::Session::lockStart, lockStartDatetimeMs);
                    isFrozen =
                        preferences.getBool(Keys::Session::isFrozen, false);
                    frozenAtMs =
                        preferences.getULong64(Keys::Session::frozenAt, 0);
                    // Re-read the visibility settings too: a freeze sync can
                    // arrive with a hidden-timer or elapsed-time change, and the
                    // banner must honour whichever one is now current.
                    isTimeDisp =
                        preferences.getBool(Keys::Session::isTimeDisp, true);
                    displayElapsedTime = preferences.getBool(
                        Keys::Session::displayElapsedTime, false);
                    readHeldReason();
                    preferences.end();
                    xSemaphoreGive(preferencesMutex);
                    seenCorrectionVersion = correctionVersion;
                    seenFreezeVersion = freezeVersion;
                    refreshedAt = millis();

                    ESP_LOGD(UNLOCK_TIMER_TAG,
                             "Reloaded lockEndDatetimeMs: %llu, frozen: %d",
                             lockEndDatetimeMs, isFrozen);
                }
            }
        }

        struct timeval tv{};
        lockboxGetTimeOfDay(&tv, nullptr);
        currentDatetimeMs =
            ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;

        // A Chaster freeze holds the countdown. The session stays locked, the
        // remaining time is anchored at frozenAt, and the timer never expires.
        // Emergency-blocked timing keeps its own unfrozen path so emergency
        // unlock is unaffected.
        const bool freezeHolds =
            freezeHoldsCountdown(isFrozen) && !isEmergencyBlocked;
        // Whether the hold has its own page. With a hidden timer an unlock
        // hold stays off the screen until the deadline has passed: shown
        // earlier, it would say the end is near. It holds all the same.
        const bool freezeShown =
            freezeHolds &&
            holdShownOnScreen(true, isUnlockHold, isTimeDisp, isTimeSynced,
                              lockEndDatetimeMs, currentDatetimeMs);

        // Only the main timer pages reconcile automatically. Menus, break
        // acceptance and emergency screens keep their focus until user input.
        if (!isEmergencyBlocked &&
            ((freezeShown && (plkbx->sm->is("locked.timer"_s) ||
                              plkbx->sm->is("locked.ready"_s) ||
                              plkbx->sm->is("locked.ready.addTime"_s))) ||
             (!freezeShown && plkbx->sm->is("locked.frozen"_s)))) {
            plkbx->sm->process_event(FreezeChange{});
        }
        // A freeze can turn into an unlock hold, or back, without leaving the
        // page: repaint its heading. The repaint also redraws the band below.
        if (freezeShown && plkbx->sm->is("locked.frozen"_s) &&
            plkbx->heldPageShowsWaiting != isUnlockHold) {
            plkbx->drawLockedActions();
        }
        if (!shouldDrawCountdown(plkbx)) lastFrozenDisplay.valid = false;

        // Only a visible timer with a server-supplied anchor may show a paused
        // value: the anchor is the same before and after a reboot, and a hidden
        // timer is never revealed.
        const bool freezeShowsTime =
            freezeShown && freezeShowsPausedTime(isTimeDisp, frozenAtMs);

        // ms remaining, held while a freeze is shown
        const uint64_t remainingTimeMs =
            freezeShown ? frozenRemainingAtAnchor(frozenAtMs, lockEndDatetimeMs)
                        : (lockEndDatetimeMs > currentDatetimeMs
                               ? lockEndDatetimeMs - currentDatetimeMs
                               : 0);

        // Light the screen for the last 30 seconds of a visible countdown. A
        // hidden timer stays dark: waking by itself would say the end is near.
        if (wakesScreenNearExpiry(isTimeDisp, freezeHolds, remainingTimeMs)) {
            setNotIdle(Str::Input::UNLOCK_TIMER);
        }

        if (!freezeHolds && lockEndDatetimeMs <= currentDatetimeMs) {
            if (isEmergencyBlocked) {
                plkbx->sm->process_event(Unlock{});
                break;
            }
            // Also reconcile a local timer restored to Ready after reboot.
            // Managed Ready pages reject this event and keep waiting for input.
            plkbx->sm->process_event(TimerExpired{});
            // Stay active to pick up an extension without restarting a task.
            sleepDuration = 0;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        if (plkbx->sm->is("locked.ready"_s) ||
            plkbx->sm->is("locked.ready.addTime"_s)) {
            plkbx->sm->process_event(LockTimer{});
        }

        int16_t x1, y1;
        uint16_t w, h;

        timeRemainingCanvas.fillScreen(0);
        timeRemainingCanvas.setTextColor(ST77XX_WHITE);

        int x = 0;
        int y = Display::StatusbarHeight + 24;
        if (isEmergencyBlocked) {
            y = Display::HEIGHT - 24;
        }

        if (freezeShown) {
            // Keep the paused value and reason in their own 48-pixel band,
            // below the dedicated frozen heading and above the snowflake.
            y = 96;
            // Draw one centered line on the timer canvas.
            auto drawCenteredLine = [&](const char *text, const GFXfont *font,
                                        int baseline, uint16_t *outWidth) {
                timeRemainingCanvas.setFont(font);
                int16_t tx, ty;
                uint16_t tw, th;
                timeRemainingCanvas.getTextBounds(text, 0, 0, &tx, &ty, &tw,
                                                  &th);
                timeRemainingCanvas.setCursor((Display::WIDTH - tw) / 2 - tx,
                                              baseline);
                timeRemainingCanvas.println(text);
                if (outWidth != nullptr) *outWidth = tw;
            };

            // The wearer keeps watching the timer they normally watch -- the
            // countdown, or the elapsed time when that is selected -- stopped at
            // frozenAt. A hidden timer stays hidden: the banner alone reveals
            // nothing.
            const uint64_t pausedDisplayMs =
                freezeShowsTime
                    ? frozenPausedDisplayMs(frozenAtMs, lockEndDatetimeMs,
                                            lockStartDatetimeMs,
                                            displayElapsedTime)
                    : 0;
            // Under a second there is nothing worth showing (the formatter
            // would render "Now"), so the banner stands on its own.
            const char *pausedTime = pausedDisplayMs >= 1000
                                         ? formatTimeString(pausedDisplayMs)
                                         : nullptr;

            if (pausedTime != nullptr) {
                // Use a plain, legible face. The scan font's 32-pixel digits
                // previously overlapped the 17-pixel freeze label.
                uint16_t width = 0;
                drawCenteredLine(pausedTime, &FreeSansBold12pt7b, 21, &width);
                if (width > Display::WIDTH - 2 * Display::Padding::P2) {
                    timeRemainingCanvas.fillScreen(0);
                    drawCenteredLine(pausedTime, &FreeSans9pt7b, 21, nullptr);
                }
            } else {
                drawCenteredLine(isTimeDisp ? "Time paused" : Str::Screen::TIME_HIDDEN,
                                 &FreeSans9pt7b, 21, nullptr);
            }
            if (freezeReason[0] != '\0')
                drawCenteredLine(freezeReason, &FreeSans9pt7b, 44, nullptr);
            else if (pausedTime != nullptr)
                drawCenteredLine("Time paused", &FreeSans9pt7b, 44, nullptr);
        } else {
            const uint64_t elapsedTimeMs =
                currentDatetimeMs > lockStartDatetimeMs
                    ? currentDatetimeMs - lockStartDatetimeMs
                    : 0;

            const uint64_t displayTimeMs =
                displayElapsedTime ? elapsedTimeMs : remainingTimeMs;

            // convert to pretty time string
            const char *timeStr;
            if (isTimeDisp) {
                timeStr = formatTimeString(displayTimeMs);
            } else {
                timeStr = Str::Screen::TIME_HIDDEN;
            }

            // The smaller face is for a long visible countdown only: a hidden
            // timer's page must look the same however much time is left.
            if (isTimeDisp && remainingTimeMs > 365 * 24 * 60 * 60 * 1000ULL) {
                timeRemainingCanvas.setFont(
                    &Workbench_Regular_VariableFont_BLED_SCAN16pt7b);
            } else {
                timeRemainingCanvas.setFont(
                    &Workbench_Regular_VariableFont_BLED_SCAN20pt7b);
            }
            timeRemainingCanvas.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);

            if (!isTimeSynced) {
                timeStr = Str::Screen::WIFI_FOR_TIME;
                timeRemainingCanvas.setFont(&FreeSans9pt7b);
                timeRemainingCanvas.getTextBounds(timeStr, 0, 0, &x1, &y1, &w,
                                                  &h);
            }

            x = (Display::WIDTH - w) / 2;
            timeRemainingCanvas.setCursor(x, 14 + 20);
            timeRemainingCanvas.println(timeStr);
        }

        // A held countdown is static, so compare what would be drawn with the
        // last frame. A duplicate freeze sync (or the once-a-second poll) must
        // not keep redrawing the same frozen page.
        const FrozenDisplay nextDisplay = [&]() {
            FrozenDisplay snapshot;
            snapshot.valid = true;
            snapshot.isEmergencyBlocked = isEmergencyBlocked;
            snapshot.freezeHolds = freezeShown;
            snapshot.isTimeDisp = isTimeDisp;
            snapshot.displayElapsedTime = displayElapsedTime;
            snapshot.remainingTimeMs = freezeShown
                ? frozenPausedDisplayMs(frozenAtMs, lockEndDatetimeMs,
                                        lockStartDatetimeMs, displayElapsedTime)
                : remainingTimeMs;
            snapshot.counter = freezeDisplayVersion();
            if (freezeShown) {
                strncpy(snapshot.reason, freezeReason,
                        sizeof(snapshot.reason) - 1);
            }
            return snapshot;
        }();
        const bool frozenFrameUnchanged =
            lastFrozenDisplay.valid &&
            lastFrozenDisplay.isEmergencyBlocked == nextDisplay.isEmergencyBlocked &&
            lastFrozenDisplay.freezeHolds == nextDisplay.freezeHolds &&
            lastFrozenDisplay.isTimeDisp == nextDisplay.isTimeDisp &&
            lastFrozenDisplay.displayElapsedTime ==
                nextDisplay.displayElapsedTime &&
            lastFrozenDisplay.remainingTimeMs == nextDisplay.remainingTimeMs &&
            lastFrozenDisplay.counter == nextDisplay.counter &&
            strcmp(lastFrozenDisplay.reason, nextDisplay.reason) == 0;

        if (shouldDrawCountdown(plkbx) && !frozenFrameUnchanged &&
            xSemaphoreTake(displayMutex, 100) == pdTRUE) {
            plkbx->display.drawBitmap(0, y, timeRemainingCanvas.getBuffer(),
                                      Display::WIDTH, TIME_CANVAS_HEIGHT,
                                      Colors::white, Colors::black);
            xSemaphoreGive(displayMutex);
            lastFrozenDisplay = nextDisplay;
        }

        ESP_LOGV(UNLOCK_TIMER_TAG, "Staying locked for %llu ms",
                 remainingTimeMs);

        // Wake before expiry; unsigned subtraction must not wrap for short locks.
        sleepDuration = remainingTimeMs > PSEUDO_SLEEP_TIMEOUT
                            ? remainingTimeMs - PSEUDO_SLEEP_TIMEOUT
                            : 1;
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    sleepDuration = 0;
    // set the handle to null
    TaskHandles::unlockTimer = nullptr;

    // delete the task
    vTaskDelete(nullptr);
}

void LKBX::startUnlockTimer() {
    // A page being entered and restoreUnlockTimer can both get here.
    std::lock_guard<std::mutex> lock(unlockTimerStart);
    if (TaskHandles::unlockTimer != nullptr) {
        ESP_LOGW(UNLOCK_TIMER_TAG, "UnlockTimer already running");
        return;
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating unlock timer task");
    startTaskWithRetry(startUnlockTimerTask, Tasks::unlockTimer, this,
                       &TaskHandles::unlockTimer);
}

// A locked timer needs its countdown task: it draws the time left, and at the
// end it releases a local timer, moves a session to Ready to unlock or lifts
// the emergency block. If the task could not be created when its page was
// entered (no free block of internal RAM for its stack), or ended just as the
// page was entered again, the box would stay on that page with nothing
// counting. So the task is started again here for as long as it is missing.
// Called from the activity log task.
void LKBX::restoreUnlockTimer() {
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - unlockTimerCheckedMs) < 2000) return;
    unlockTimerCheckedMs = now;

    const bool missing =
        TaskHandles::unlockTimer == nullptr &&
        (sm->is("locked.timer"_s) || sm->is("locked.ready"_s) ||
         sm->is("locked.ready.addTime"_s) || sm->is("locked.frozen"_s) ||
         sm->is("emergency.blocked"_s));
    // Two looks in a row, so a page that is just being entered starts its own
    // task first.
    if (!missing) {
        unlockTimerMissing = false;
        return;
    }
    if (!unlockTimerMissing) {
        unlockTimerMissing = true;
        return;
    }

    ESP_LOGW(UNLOCK_TIMER_TAG, "The countdown task is not running; starting it");
    startUnlockTimer();
}
void LKBX::calibrate() {
    if (TaskHandles::calibrate != nullptr) {
        ESP_LOGW(CALIBRATE_TAG, "Calibration task already running");
        return;
    }

    if (xTaskCreatePinnedToCore(
            calibrateTask, Tasks::calibrate.name, Tasks::calibrate.stackSize,
            this, Tasks::calibrate.priority, &TaskHandles::calibrate,
            Tasks::calibrate.core) != pdPASS) {
        ESP_LOGE(CALIBRATE_TAG, "Could not start calibration task");
    }
}

void LKBX::calibrateTask(void *pvParameters) {
    auto *plkbx = static_cast<LKBX *>(pvParameters);
    // Pin detection and motor travel must not hold the state-machine mutex:
    // BLE inputs need to finish even while calibration waits for the operator.
    plkbx->calibrateButton();
    if (plkbx->sm->process_event(ButtonCalibrated{})) {
        plkbx->motor.calibrate();
    }
    TaskHandles::calibrate = nullptr;
    vTaskDelete(nullptr);
}

void LKBX::calibrateButton() {
    bool pressed = false;
    bool switchPins = false;

    // make Motor::A and Buttons::buttonEnter inputs
    pinMode(Pins::MOTOR::A, INPUT);
    pinMode(Pins::Buttons::buttonEnter, INPUT);

    int buttonInitialState = digitalRead(Pins::Buttons::buttonEnter);
    int motorInitialState = digitalRead(Pins::MOTOR::A);

    int buttonState;
    int motorState;
    // counting presses avoids false positives.
    int presses = 0;

    while (!pressed && presses < 10) {
        buttonState = digitalRead(Pins::Buttons::buttonEnter);
        motorState = digitalRead(Pins::MOTOR::A);

        if (buttonState != buttonInitialState) {
            // when asked to press the button, the button pin changed!
            // therefore, do not change.
            pressed = true;
            presses++;
            switchPins = false;
            ESP_LOGD(CALIBRATE_TAG, "Button Pin pressed, not switching pins");
        } else if (motorState != motorInitialState) {
            // When asked to press the button, the motor pin changed!
            // therefore, change the pins.
            pressed = true;
            presses++;
            switchPins = true;
            ESP_LOGD(CALIBRATE_TAG, "Motor Pin pressed, switching pins");
        }

        ESP_LOGD(CALIBRATE_TAG, "buttonState: %d, motorState: %d", buttonState,
                 motorState);

        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    // get preferences and save this
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isButtonInverted, switchPins);
        preferences.putBool(Keys::Base::isButtonCalibrated, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    if (switchPins) {
        Pins::MOTOR::A = 12;
        Pins::Buttons::buttonEnter = 46;
    }

    pinMode(Pins::MOTOR::A, OUTPUT);
    pinMode(Pins::Buttons::buttonEnter, INPUT);
}
