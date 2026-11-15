#include "utils/trustedClock.h"
#include "LKBX.h"

#include <esp_heap_caps.h>

#include "Fonts/FreeSans9pt7b.h"
#include "services/serverOnline.h"
#include "ui/Animation.h"
#include "utils/breakTiming.h"
#include "utils/chasterAsk.h"
#include "utils/psram.h"

static constexpr int kBreakCanvasWidth = 170;  // Display::WIDTH - 150
static constexpr int kBreakCanvasHeight = 72;
static uint16_t *breakCanvasBuffer = nullptr;
static GFXcanvas16 *breakCanvas = nullptr;

static bool ensureBreakCanvasAllocated() {
    if (breakCanvas != nullptr) return true;

    const size_t breakCanvasBytes = static_cast<size_t>(kBreakCanvasWidth) *
                                    kBreakCanvasHeight * sizeof(uint16_t);
    breakCanvasBuffer =
        static_cast<uint16_t *>(lockboxAllocPreferPsram(breakCanvasBytes));
    if (breakCanvasBuffer) {
        breakCanvas = new (std::nothrow) ui::ExternalCanvas16(
            kBreakCanvasWidth, kBreakCanvasHeight, breakCanvasBuffer);
    }
    return breakCanvas != nullptr && breakCanvas->getBuffer() != nullptr;
}

void startBreakTimerTask(void *pvParameters) {
    LKBX *plkbx = (LKBX *)pvParameters;
    auto isInCorrectState = [](LKBX *lkbx) {
        // Add any states that you want to support here.
        return lkbx->sm->is("locked.break"_s);
    };

    // On Entry set the breakLast to now.

    uint64_t currentDatetimeMs;
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    currentDatetimeMs =
        ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;
    uint64_t breakStart = 0;
    uint64_t breakMax = 0;
    uint64_t lockEnd = 0;
    int64_t timeRemaining = 0;
    // On a Chaster session the box's own deadline ends nothing: Chaster, the
    // keyholder or the wearer ends the break.
    bool chasterControlled = false;

    // Check if we're resuming from a previous break (after wake-up from sleep)
    // or starting a new break.
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session, false);
        bool wasActiveBreak =
            preferences.getBool(Keys::Session::isActiveBreak, false);
        uint64_t savedBreakStart =
            preferences.getULong64(Keys::Session::breakStart, 0);
        breakMax = preferences.getULong64(Keys::Session::breakMax, 0);
        lockEnd = preferences.getULong64(Keys::Session::lockEnd, 0);
        chasterControlled =
            preferences.getBool(Keys::Session::chasterControlled, false);

        // Resume a previously-started break regardless of whether its window has
        // already elapsed. If it's late, timeRemaining goes negative below and the
        // "you are over your break" screen renders. (Previously this also required
        // `now < breakStart + breakMax`, which reset a slept-through break to the
        // full time -- the same stale check removed from isOnActiveBreak in
        // 3649fb9.)
        if (shouldResumeBreak(wasActiveBreak, savedBreakStart)) {
            breakStart = savedBreakStart;
            ESP_LOGD(BREAK_TIMER_TAG, "Resuming break from %llu", breakStart);
        } else {
            // Start new break - persist the start time and set active flag
            breakStart = currentDatetimeMs;
            preferences.putULong64(Keys::Session::breakStart, breakStart);
            preferences.putBool(Keys::Session::isActiveBreak, true);
            ESP_LOGD(BREAK_TIMER_TAG, "Starting new break at %llu", breakStart);
        }
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    if (breakStart == 0) {
        breakStart = currentDatetimeMs;
        ESP_LOGW(BREAK_TIMER_TAG, "breakStart was 0 after init, using %llu",
                 breakStart);
        if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) ==
            pdTRUE) {
            preferences.begin(Keys::session, false);
            preferences.putULong64(Keys::Session::breakStart, breakStart);
            preferences.putBool(Keys::Session::isActiveBreak, true);
            preferences.end();
            xSemaphoreGive(preferencesMutex);
        }
    }

    // Expiry ends a normal break by re-locking into Ready to unlock. A pending
    // dashboard break-end request still waits for the wearer's acceptance.
    if (breakEndsAtLockWindow(chasterControlled, isTimeSynced, lockEnd,
                              currentDatetimeMs) &&
        plkbx->sm->process_event(TimerExpired{})) {
        ESP_LOGW(BREAK_TIMER_TAG,
                 "Lock window ended (lockEnd=%llu) -- ready to unlock",
                 lockEnd);
        TaskHandles::breakTimer = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    bool canvasAvailable = ensureBreakCanvasAllocated();
    bool breakMissed = false;
    bool breakEnded = false;
    bool pageDrawn = false;
    bool endRequested = false;
    String displayedRequester;

    while (isInCorrectState(plkbx)) {
        // update the current time.
        lockboxGetTimeOfDay(&tv, nullptr);
        currentDatetimeMs =
            ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;

        // each cycle, update the breakLast time in preferences and pull the
        // break max (this could change!)
        if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) ==
            pdTRUE) {
            preferences.begin(Keys::session, false);
            preferences.putULong64(Keys::Session::breakLast, currentDatetimeMs);
            breakMax = preferences.getULong64(Keys::Session::breakMax, 0);
            //            breakMax = 1000 * 30;  // 30 seconds.
            lockEnd = preferences.getULong64(Keys::Session::lockEnd, 0);
            chasterControlled =
                preferences.getBool(Keys::Session::chasterControlled, false);
            const bool requested = preferences.getString(Keys::Session::lockState, "") == Str::Protocol::BREAK_ENDING;
            String requestedBy = preferences.getString(Keys::Session::requestedBy, "");
            if (requestedBy.isEmpty()) requestedBy = "Your dashboard";
            preferences.end();
            xSemaphoreGive(preferencesMutex);
            if (!pageDrawn || requested != endRequested ||
                (requested && requestedBy != displayedRequester)) {
                if (requested) {
                    plkbx->drawBreakEndRequest(requestedBy);
                } else {
                    plkbx->drawQRCode(Str::Screen::ON_BREAK_TITLE, Str::Screen::EMPTY,
                                      Str::Url::DOCS_TAKE_A_BREAK, Str::Label::INFO,
                                      {Str::Action::END_BREAK, true}, Action{.visible = false});
                }
                pageDrawn = true;
                displayedRequester = requestedBy;
            }
            endRequested = requested;
        }

        if (breakEndsAtLockWindow(chasterControlled, isTimeSynced, lockEnd,
                                  currentDatetimeMs) &&
            plkbx->sm->process_event(TimerExpired{})) {
            ESP_LOGW(BREAK_TIMER_TAG,
                     "Lock window ended mid-break (lockEnd=%llu) -- ready to unlock",
                     lockEnd);
            break;
        }

        timeRemaining =
            breakTimeRemaining(breakStart, breakMax, currentDatetimeMs);

        // Break window ended — notify server, show late UI briefly, then
        // re-lock.
        if (timeRemaining <= 0) {
            if (!breakMissed) {
                plkbx->sendMessage(Str::Protocol::BREAK_MISSED);
                breakMissed = true;
            }
            uint64_t lateMs = 0;
            if (timeRemaining < 0) {
                lateMs = (uint64_t)(-timeRemaining);
                // Corrupt/missing breakStart produced absurd values — clamp for
                // display.
                if (lateMs > currentDatetimeMs) {
                    lateMs = currentDatetimeMs;
                }
            }

            if (canvasAvailable) {
                breakCanvas->fillScreen(ST77XX_BLACK);
                breakCanvas->setCursor(0, 12);
                breakCanvas->setTextColor(Colors::red);
                breakCanvas->setFont(&FreeSans9pt7b);
                char breakMsg[64];
                snprintf(breakMsg, sizeof(breakMsg), "%s%s",
                         Str::Screen::BREAK_LATE_PREFIX,
                         formatTimeString(lateMs));
                wrapText(*breakCanvas, breakMsg);
            }

            if (!breakEnded && !endRequested) {
                ESP_LOGI(BREAK_TIMER_TAG,
                         "Break expired (late by %llu ms), ending break",
                         lateMs);
                breakEnded = plkbx->sm->process_event(Done{});
            }
        } else {
            if (canvasAvailable) {
                breakCanvas->fillScreen(ST77XX_BLACK);
                breakCanvas->setCursor(0, 12);
                breakCanvas->setTextColor(ST77XX_WHITE);
                breakCanvas->setFont(&FreeSans9pt7b);
                char breakMsg[64];
                snprintf(breakMsg, sizeof(breakMsg), "%s%s",
                         Str::Screen::BREAK_ENDS_PREFIX,
                         formatTimeString((uint64_t)timeRemaining));
                wrapText(*breakCanvas, breakMsg);
            }
        }

        if (canvasAvailable && isInCorrectState(plkbx) && xSemaphoreTake(displayMutex, 100) == pdTRUE) {
            ui::fastBlit(plkbx->display, Display::Padding::P2, endRequested ? 160 : 92,
                         breakCanvas->getBuffer(), kBreakCanvasWidth,
                         endRequested ? 48 : kBreakCanvasHeight);
            xSemaphoreGive(displayMutex);
        }

        if (!breakEnded) {
            ESP_LOGD(BREAK_TIMER_TAG, "Time remaining: %lld ms", timeRemaining);
        }

        if (breakEnded) {
            break;
        }

        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    TaskHandles::breakTimer = nullptr;
    vTaskDelete(nullptr);
}

void LKBX::startBreakTimer() {
    if (TaskHandles::breakTimer != nullptr) {
        ESP_LOGW(TASKDEBUG_TAG, "Break timer task already running, skipping");
        return;
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating break timer task");

    // Write break flag synchronously BEFORE spawning task to avoid race
    // condition. If device sleeps/crashes before task runs, flag must already
    // be persisted.
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session, false);
        bool wasActiveBreak =
            preferences.getBool(Keys::Session::isActiveBreak, false);
        if (!wasActiveBreak) {
            preferences.putBool(Keys::Session::breakEndAccepted, false);
            uint64_t now = 0;
            struct timeval tv{};
            lockboxGetTimeOfDay(&tv, nullptr);
            now = ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;

            preferences.putULong64(Keys::Session::breakStart, now);
            preferences.putBool(Keys::Session::isActiveBreak, true);
            ESP_LOGD(TASKDEBUG_TAG, "Break flag set synchronously at %llu",
                     now);
        }
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    xTaskCreatePinnedToCore(startBreakTimerTask, Tasks::breakTimer.name,
                            Tasks::breakTimer.stackSize, this,
                            Tasks::breakTimer.priority,
                            &TaskHandles::breakTimer, Tasks::breakTimer.core);
}
void LKBX::checkBreakCooldown() {
    if (sm->is("locked.breakCooldown"_s) &&
        uint32_t(millis() - breakCooldownUpdatedMs) >= 1000) {
        // Dispatch through the state machine so an exited page cannot redraw
        // over the next screen while another task handles a button or offer.
        sm->process_event(Error{.error = ErrorT::TOO_SOON_FOR_BREAK});
    }
}

void LKBX::drawBreakCooldown(bool initial) {
    breakCooldownUpdatedMs = millis();
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) != pdTRUE) return;
    preferences.begin(Keys::session);
    const uint64_t breakReg = preferences.getULong64(Keys::Session::breakReg, 0);
    const uint64_t breakLast = preferences.getULong64(Keys::Session::breakLast, 0);
    const uint64_t lockStart = preferences.getULong64(Keys::Session::lockStart, 0);
    preferences.end();
    xSemaphoreGive(preferencesMutex);

    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    const uint64_t now = uint64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    const uint64_t remaining = breakCooldownRemaining(breakLast, lockStart, breakReg, now);
    if (remaining == 0) {
        sm->process_event(Done{});
        return;
    }

    // Round up so the countdown never says zero before a break is available.
    const String timeToWait = formatTimeString(((remaining + 999) / 1000) * 1000);
    drawQRCode(Str::Screen::BREAK_TOO_SOON_TITLE,
               String(Str::Screen::BREAK_TOO_SOON_DESC_PRE) + timeToWait +
                   Str::Screen::BREAK_TOO_SOON_DESC_POST,
               Str::Url::DOCS_TAKE_A_BREAK, Str::Label::INFO,
               continueAction, backAction, !initial);
}

void LKBX::startBreakPreflight() const {
    // Are breaks enabled? May the wearer start one? When did the last one end?

    bool isBreak = false;
    bool keyholderOnly = false;
    uint64_t breakLast = 0;
    uint64_t lockStart = 0;
    uint64_t breakReg = 0;
    uint64_t currentDatetimeMs;
    struct timeval tv{};
    lockboxGetTimeOfDay(&tv, nullptr);
    currentDatetimeMs =
        ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;

    // get prefs
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        isBreak = preferences.getBool(Keys::Session::isBreak, true);
        keyholderOnly =
            preferences.getBool(Keys::Session::breakKeyholderOnly, false);
        breakLast = preferences.getULong64(Keys::Session::breakLast, 0);
        lockStart = preferences.getULong64(Keys::Session::lockStart, 0);
        breakReg = preferences.getULong64(Keys::Session::breakReg, 0);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    switch (evaluateBreakPreflight(isBreak, keyholderOnly, breakLast, lockStart,
                                   breakReg, currentDatetimeMs)) {
        case BreakPreflight::NOT_ENABLED:
            sm->process_event(Error{.error = ErrorT::BREAK_NOT_ENABLED});
            return;
        case BreakPreflight::KEYHOLDER_ONLY:
            sm->process_event(Error{.error = ErrorT::BREAK_KEYHOLDER_ONLY});
            return;
        case BreakPreflight::TOO_SOON:
            sm->process_event(Error{.error = ErrorT::TOO_SOON_FOR_BREAK});
            return;
        case BreakPreflight::ALLOWED:
            break;
    }

    // Chaster decides a break on a session it controls. The box's own rules
    // passed, so ask the dashboard; it opens only on a granted answer.
    if (isChasterSession()) {
        sm->process_event(AskChaster{});
        return;
    }

    sm->process_event(Break{});
}
