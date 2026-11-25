#ifndef LOCKBOX_LKBX_H
#define LOCKBOX_LKBX_H

#include "utils/trustedClock.h"
#include <pages/TextPages.h>

#include <boost/sml.hpp>

#include <atomic>
#include <mutex>

#include "../constants/LogTags.h"
#include "../constants/Pins.h"
#include "../services/ina.h"
#include "../services/mqtt.h"
#include "../services/sleep.h"
#include "../services/serverOnline.h"
#include "../services/tasks.h"
#include "../services/wm.h"
#include "services/deviceIdentity.h"
#include "../utils/RecusiveMutex.h"
#include "../utils/StateLogger.h"
#include "../utils/clockCorrection.h"
#include "../utils/update.h"
#include "Actions.h"
#include "Adafruit_ST7789.h"
#include "AiEsp32RotaryEncoder.h"
#include "Events.h"
#include "Guard.h"
#include "motor/Motor.h"
#include "structs/Action.h"
#include "structs/MenuItem.h"
#include "tasks/TaskStart.h"
#include "utils/breakTiming.h"
#include "utils/chasterAsk.h"
#include "utils/countdownWatch.h"
#include "utils/emergencyReport.h"
#include "utils/emergencyStore.h"
#include "utils/getPairingCode.h"
#include "utils/heldLock.h"
#include "utils/lockMenuCountdown.h"
#include "utils/lockedReport.h"
#include "utils/logBacklog.h"
#include "utils/missingTask.h"
#include "utils/naturalUnlock.h"
#include "utils/normalizeOption.h"
#include "utils/psram.h"
#include "utils/savedTimerSleep.h"
#include "utils/stateRead.h"
#include "utils/stringFormat.h"
#include "utils/unsentReport.h"

namespace sml = boost::sml;

extern long delayTimeout;

// static default actions
// continue
static Action continueAction = {Str::Action::CONTINUE, false};
static Action backAction = {Str::Action::BACK, false};

class LKBX {
  private:
    static void delayTask(void *pvParameters);
    void immediateTransition();
    void startPreflightScreen();
    static void drawPreflightTask(void *pvParameters);
    void startPreflight() const;
    void startBreakPreflight() const;
    void drawLoadingScreen(const String &title, const String &description);
    // Preflight loading animation (looping spinner + dynamic caption).
    void drawLoadingAnimation();
    void pauseLoadingAnimation();
    void playLoadingAnimation();
    void setLoadingText(const char *text);
    void stopLoadingAnimation();
    void drawPage(const TextPage *page);
    void drawPairingCode(bool onboarding);
    void startUnlockTimer();
    void startBreakTimer();
    void drawBreakCooldown(bool initial);
    void checkBreakCooldown();
    uint32_t breakCooldownUpdatedMs = 0;
    // A Chaster session asks the dashboard before a break or an unlock. The
    // question runs on the delay task, one step per pass (LKBX.chaster.cpp).
    void startChasterAsk();
    void serviceChasterAsk();
    void answerChasterAsk(const AskAnswer &answer);
    // A plain page: a heading, a few lines and its buttons.
    void drawNotice(const char *title, const String &body, const Action &right,
                    const Action &left);
    void drawAskAnswer();
    // The newest question: its number and what it asks (nextAskOrder). Only
    // the newest question may answer; an older one's result is dropped.
    std::atomic<uint32_t> askOrder{kNoAskOrder};
    // What the box is asking, set by the transition into "locked.asking".
    AskAction askAction = AskAction::UNLOCK;
    // The answer and why Chaster refused, as the dashboard worded it. Written
    // by the asking task just before it reports the answer, read by the page
    // it leads to.
    AskOutcome askOutcome = AskOutcome::UNANSWERED;
    char askReason[kAskReasonBytes] = "";
    // Which heading the held-session page was last drawn with, so a freeze
    // that turns into an unlock hold (or back) repaints it.
    volatile bool heldPageShowsWaiting = false;
    // The state read from the dashboard runs on the delay task too, one step
    // per pass (LKBX.wifi.cpp, utils/stateRead.h). confirmServerClear asks
    // for it.
    void serviceStateRead();
    StateRead stateReadPlan;
    bool onLockMenuPage() const;
    // Whether the lock menu must run the countdown (utils/lockMenuCountdown.h).
    bool lockMenuNeedsCountdown() const;
    // A locked timer must have its countdown task (LKBX.cpp).
    void restoreUnlockTimer();
    std::mutex unlockTimerStart;
    uint32_t unlockTimerCheckedMs = 0;
    bool unlockTimerMissing = false;
    // While that task cannot be created, the delay task watches the deadline
    // in its place (utils/countdownWatch.h). Set by restoreUnlockTimer on the
    // activity log task, read by the delay task.
    void serviceCountdownWatch();
    std::atomic<bool> countdownNeedsWatch{false};
    uint32_t countdownWatchedMs = 0;
    // A menu page must have its task too (LKBX.menu.cpp,
    // utils/missingTask.h).
    void restoreMenu();
    std::mutex menuTaskStart;
    MissingTask menuWatch;
    void checkOfferedLock();
    uint32_t offerStartedMs = 0;
    String offerSessionId;
    uint32_t decisionPublishedMs = 0;
    void startUpdate();
    static void updateTask(void *pvParameters);
    void startDeferredServices();
    void startWifiConnection();
    void startMqttConnection();
    static void startMqttConnectionTask(void *pvParameters);
    static void runMqttConnection(LKBX *plkbx);
    void calibrate();
    void calibrateButton();
    static void calibrateTask(void *pvParameters);
    /**
     * ///////////////////////////////////////////
     * ////
     * ////  OSSM State Machine
     * ////
     * ///////////////////////////////////////////
     *
     * This is the state machine that controls the OSSM.
     * It's based on the Boost SML library, and you don't need to be an expert
     *to use it, just follow the examples in this table:
     *
     * https://boost-ext.github.io/sml/tutorial.html#:~:text=3.%20Create%20a%20transition%20table
     *
     * Here are the basic rules:
     *  1. Each row in the table is a transition.
     *  2. Each transition has a source state, an event, a guard, an action, and
     *      a target state.
     *  3. The source state is the state that the machine must be in for the
     *      transition to be valid.
     *  4. (optional) The event is the event that triggers the transition.
     *  5. (optional) The guard is a function that returns true or false. If it
     *      returns true, then the transition is valid.
     *  6. (optional) The action is a function that is called when the
     *transition is triggered, it can't block the main thread and cannot return
     *a value.
     *  7. The target state is the state that the machine will be in after the
     *      transition is complete.
     *
     * Have fun!
     */
    struct InnerStateMachine {
        auto operator()() const {
            // Action definitions to make the table easier to read.
            auto drawHello = [](LKBX &o) { o.drawHello(); };

            auto returnToLockScreen = [](LKBX &o) { o.immediateTransition(); };
            auto startDelay = [](LKBX &) { delayTimeout = 2000; };
            auto startPreflight = [](LKBX &o) { o.startPreflight(); };
            auto startUnlockTimer = [](LKBX &o) { o.startUnlockTimer(); };
            // The lock menu keeps the countdown of the page it was opened
            // from. Coming back from a page that ends it (the break pages,
            // the emergency page, an answer from Chaster) it is started
            // again, so the menu still moves on at the deadline.
            auto resumeUnlockTimer = [](LKBX &o) {
                if (o.lockMenuNeedsCountdown()) o.startUnlockTimer();
            };
            auto startUpdate = [](LKBX &o) { o.startUpdate(); };
            auto startDeferredServices = [](LKBX &o) {
                o.startDeferredServices();
            };
            auto startWifiConnection = [](LKBX &o) { o.startWifiConnection(); };
            auto startMqttConnection = [](LKBX &o) { o.startMqttConnection(); };
            auto startBreakPreflight = [](LKBX &o) { o.startBreakPreflight(); };
            auto startBreakTimer = [](LKBX &o) { o.startBreakTimer(); };
            // Most paths into a break send no message (server or reconnect
            // driven), so an unconfirmed LOCKED report must be dropped here or
            // its retry would end the break.
            auto clearLockedReport = [](LKBX &) { lockedReport::tracker.clear(); };

            auto drawMenu = [](LKBX &o) {
                o.activeMenu = mainMenu;
                o.menuSize = numMainMenu;
                o.drawMenu();
            };
            auto drawSettingsMenu = [](LKBX &o) {
                o.activeMenu = settingsMenu;
                o.menuSize = numSettingsMenu;
                o.drawMenu();
            };
            auto drawEmergencyMenu = [](LKBX &o) { o.drawEmergencyMenu(); };
            auto drawTimerConfig = [](LKBX &o) { o.drawTimerConfig(); };
            auto drawLockAnimation = [](LKBX &o) { o.drawLockAnimation(); };
            auto drawLockedActions = [](LKBX &o) { o.drawLockedActions(); };
            auto drawReadyToUnlock = [](LKBX &o) { o.drawReadyToUnlock(); };
            auto drawAddTimeQR = [](LKBX &o) {
                o.drawQRCode("Add time", "Scan to add time to this session on your dashboard.",
                             String(RAD_SERVER) + "/app", "Dashboard",
                             Action{.visible = false}, backAction);
            };

            auto sendMessage = [](const char *message) {
                return [message](LKBX &o) { o.sendMessage(message); };
            };

            // A local timer locks only once its end is saved. The guard saves
            // it, and refuses the press when the page never showed a duration
            // or the end could not be stored (LocalTimerTransitions.inc).
            auto saveLocalTimer = [](LKBX &o) {
                return saveLocalLockTimerDuration(o.duration);
            };
            auto showTimerUnavailable = [](LKBX &o) {
                o.drawTimerUnavailable();
            };

            auto drawLoadingScreen = [](const char *title,
                                        const char *description) {
                return [title, description](LKBX &o) {
                    o.drawLoadingScreen(title, description);
                };
            };

            auto drawPage = [](const TextPage *page) {
                return [page](LKBX &o) { o.drawPage(page); };
            };

            auto isOption = [](MenuItemE value) {
                return [value](LKBX &o) {
                    auto currentOption =
                        normalizeOption(o.encoder.readEncoder(), o.menuSize);
                    auto indexOfValue = -1;

                    for (int i = 0; i < o.menuSize; i++) {
                        if (o.activeMenu[i].id == value) {
                            indexOfValue = i;
                            break;
                        }
                    }

                    return currentOption == indexOfValue;
                };
            };

            // Draws the Wi-Fi setup page and asks for its portal. The portal
            // is opened by the activity log task, which first stops the MQTT
            // client so the portal's task never has to fit in memory beside
            // it (services/wm.cpp). If the portal cannot be opened, that task
            // replaces the QR code with a notice.
            auto openWifiSetup = [](LKBX &o) {
                o.drawQRCode(Str::Screen::WIFI_SETUP_TITLE,
                             Str::Screen::WIFI_SETUP_DESC,
                             Str::Screen::WIFI_SETUP_QR, Str::Label::WIFI);
                requestWifiPortal();
            };

            auto drawWifiErrorNetworkQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::WIFI_ERROR_NETWORK_TITLE,
                             Str::Screen::WIFI_ERROR_NETWORK_DESC,
                             Str::Url::DOCS_WIFI_ERROR, Str::Label::HELP,
                             {Str::Action::SKIP}, {Str::Action::RETRY});
            };

            auto drawWifiErrorAuthQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::WIFI_ERROR_AUTH_TITLE,
                             Str::Screen::WIFI_ERROR_AUTH_DESC,
                             Str::Url::DOCS_WIFI_ERROR, Str::Label::HELP,
                             {Str::Action::SKIP}, {Str::Action::RETRY});
            };

            auto drawWifiErrorNoIpQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::WIFI_ERROR_NO_IP_TITLE,
                             Str::Screen::WIFI_ERROR_NO_IP_DESC,
                             Str::Url::DOCS_WIFI_ERROR, Str::Label::HELP,
                             {Str::Action::SKIP}, {Str::Action::RETRY});
            };

            auto drawWifiErrorServerQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::WIFI_ERROR_SERVER_TITLE,
                             Str::Screen::WIFI_ERROR_SERVER_DESC,
                             Str::Url::DOCS_SERVER_HEALTH_ERROR,
                             Str::Label::HELP, {Str::Action::SKIP},
                             {Str::Action::RETRY});
            };

            auto drawMqttErrorQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::MQTT_ERROR_TITLE,
                             Str::Screen::MQTT_ERROR_DESC,
                             Str::Url::DOCS_MQTT_ERROR, Str::Label::HELP,
                             {Str::Action::SKIP}, {Str::Action::RETRY});
            };

            auto drawAuthRateLimitedQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::AUTH_RATE_LIMITED_TITLE,
                             Str::Screen::AUTH_RATE_LIMITED_DESC,
                             Str::Url::DOCS_RATE_LIMITED_ERROR,
                             Str::Label::HELP, {Str::Action::SKIP},
                             {Str::Action::RETRY});
            };

            auto drawSignInFailedQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::SIGN_IN_FAILED_TITLE,
                             Str::Screen::SIGN_IN_FAILED_DESC,
                             Str::Url::DOCS_SIGN_IN_FAILED_ERROR,
                             Str::Label::HELP, {Str::Action::SKIP},
                             {Str::Action::RETRY});
            };

            auto drawDeviceKeyFailedQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::DEVICE_KEY_FAILED_TITLE,
                             Str::Screen::DEVICE_KEY_FAILED_DESC,
                             Str::Url::DOCS_DEVICE_KEY_ERROR,
                             Str::Label::HELP, {Str::Action::BACK},
                             {Str::Action::CONTINUE});
            };

            auto drawWelcomeQR = [](LKBX &o) {
                o.drawQRCode(
                    Str::Screen::WELCOME_TITLE, Str::Screen::WELCOME_DESC,
                    Str::Url::DOCS_QUICK_START, Str::Label::DOCS,
                    {Str::Action::NEXT}, {Str::Action::DONT_SHOW_AGAIN});
            };

            auto setOnboardingComplete = []() {
                if (xSemaphoreTake(preferencesMutex,
                                   portTICK_PERIOD_MS * 1000) == pdTRUE) {
                    preferences.begin(Keys::base, false);
                    preferences.putBool(Keys::Base::hasBeenOnboarded, true);
                    preferences.end();
                    xSemaphoreGive(preferencesMutex);
                }
                ESP_LOGD(ONBOARDING_TAG, "Set hasBeenOnboarded = true");
            };

            auto saveWifiSync = [](LKBX &o, const WiFiSync &e) {
                o.pendingWifiSsid = e.ssid;
                o.pendingWifiPassword = e.password;
                ESP_LOGD(BLE_TAG, "Stored WiFi credentials for SSID: %s",
                         e.ssid.c_str());
            };

            auto drawVoteQR = [](LKBX &o) { o.drawVoteQR(); };

            auto drawSleepQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::SLEEP_TITLE, Str::Screen::SLEEP_DESC,
                             Str::Url::DOCS_SLEEP_MODE, Str::Label::INFO,
                             {Str::Action::SLEEP});
            };

            auto drawErrorLockQR = [](LKBX &o) {
                o.drawQRCode(Str::Error::LOCK_TITLE, Str::Error::LOCK_DESC,
                             Str::Url::DOCS_ERROR_LOCK, Str::Label::INFO,
                             {Str::Action::RETRY}, {Str::Action::CALIBRATE});
            };

            auto drawErrorUnlock = [](LKBX &o) {
                o.drawQRCode(Str::Error::UNLOCK_TITLE, Str::Error::UNLOCK_DESC,
                             Str::Url::DOCS_ERROR_UNLOCK, Str::Label::INFO,
                             {Str::Action::RETRY}, {Str::Action::CALIBRATE});
            };

            auto drawErrorNoBackplateQR = [](LKBX &o) {
                o.drawQRCode(Str::Error::BACKPLATE_TITLE,
                             Str::Error::BACKPLATE_DESC,
                             Str::Url::DOCS_ERROR_BACKPLATE, Str::Label::INFO,
                             {Str::Action::RESTART}, {Str::Action::CALIBRATE});
            };

            auto drawBreakQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::BREAK_QR_TITLE,
                             Str::Screen::BREAK_QR_DESC,
                             Str::Url::DOCS_TAKE_A_BREAK, Str::Label::INFO,
                             {Str::Action::BREAK});
            };

            auto drawBreakNotEnabledQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::BREAK_NOT_ENABLED_TITLE,
                             Str::Screen::BREAK_NOT_ENABLED_DESC,
                             Str::Url::DOCS_TAKE_A_BREAK);
            };

            auto drawBreakKeyholderOnlyQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::BREAK_KEYHOLDER_ONLY_TITLE,
                             Str::Screen::BREAK_KEYHOLDER_ONLY_DESC,
                             Str::Url::DOCS_TAKE_A_BREAK);
            };

            auto drawBreakTooSoonQR = [](LKBX &o) {
                o.drawBreakCooldown(true);
            };

            auto drawAcceptLockQR = [](LKBX &o) {
                String name;
                String requestedBy;
                if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
                    preferences.begin(Keys::session);
                    name = preferences.getString(Keys::Session::lockName, "");
                    requestedBy = preferences.getString(Keys::Session::requestedBy, "");
                    preferences.end();
                    xSemaphoreGive(preferencesMutex);
                }
                if (name.isEmpty()) name = "Lock session";
                if (requestedBy.isEmpty()) requestedBy = "Your keyholder";
                const String description = requestedBy +
                    " wants to start lock session \"" + name +
                    "\". No response in 5 min cancels.";
                o.drawQRCode(Str::Screen::ACCEPT_LOCK_TITLE,
                             description,
                             Str::Url::DOCS_QUICK_START, Str::Label::INFO,
                             {Str::Action::ACCEPT}, {Str::Action::DECLINE});
            };
            auto declineLockAction = [](LKBX &o) {
                const String id = declineOfferedLockSession();
                if (!id.isEmpty()) o.sendMessage(Str::Protocol::ABANDONED, id);
            };

            auto drawPermalockQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::PERMALOCK_TITLE,
                             Str::Screen::PERMALOCK_DESC,
                             Str::Url::DOCS_PERMALOCK, Str::Label::INFO,
                             {Str::Action::LOCK});
            };

            auto drawEUnlockQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::EUNLOCK_TITLE,
                             Str::Screen::EUNLOCK_DESC,
                             Str::Url::DOCS_EMERGENCY_UNLOCK, Str::Label::INFO,
                             {Str::Action::UNLOCK});
            };

            auto drawEBlockedQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::EBLOCKED_TITLE,
                             Str::Screen::EBLOCKED_DESC,
                             Str::Url::DOCS_EBLOCK_ERROR, Str::Label::INFO,
                             {Str::Action::WIFI_SETUP});
            };

            auto drawCouldNotConnectQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::COULD_NOT_CONNECT_TITLE,
                             Str::Screen::COULD_NOT_CONNECT_DESC,
                             Str::Url::DOCS_WIFI_ERROR, Str::Label::INFO);
            };

            auto drawCalibrationQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::CALIBRATION_TITLE,
                             Str::Screen::CALIBRATION_DESC,
                             Str::Url::DOCS_CALIBRATION, Str::Label::INFO,
                             {Str::Action::CALIBRATE},
                             Action{.visible = false});
            };

            auto drawNoUpdateQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::NO_UPDATE_TITLE,
                             Str::Screen::NO_UPDATE_DESC,
                             Str::Url::DOCS_UPDATES);
            };

            auto drawPairingQR = [](LKBX &o) {
                o.drawPairingCode(false);
            };

            // Onboarding pairing QR — differs from the settings drawPairingQR
            // by offering a "Skip" button and different copy.
            auto drawInitPairingQR = [](LKBX &o) {
                o.drawPairingCode(true);
            };

            auto drawEnrolInAlphaQR = [](LKBX &o) {
                o.drawQRCode(Str::Screen::ENROL_ALPHA_TITLE,
                             Str::Screen::ENROL_ALPHA_DESC,
                             Str::Url::DOCS_ENROL_ALPHA, Str::Label::INFO,
                             {Str::Action::ENROL});
            };

            auto lockAction = [](LKBX &o) {
                o.motor.lock();
                switchToLock();
            };
            auto lockTimerAction = [](LKBX &o) {
                o.motor.lock();
                switchToTimer();
            };
            // An unlock the box accepted at the end of the session's timer
            // (acceptExpiredTimer) tells the paddle to open and stores
            // nothing: the session is finished only once the check has seen
            // the paddle open (utils/naturalUnlock.h). Any other unlock ends
            // the stored lock here, as it always has.
            auto unlockAction = [](LKBX &o) {
                if (naturalUnlock::tracker.begin(millis())) {
                    const bool wasHeld = isLockHeld();
                    o.motor.unlock(false);
                    naturalUnlock::tracker.opening(o.motor.lockId, wasHeld);
                    return;
                }
                o.motor.unlock();
            };
            auto movePaddleToLock = [](LKBX &o) { o.motor.lock(false); };
            auto movePaddleToUnlock = [](LKBX &o) { o.motor.unlock(false); };
            auto emergencyUnlockAction = [](LKBX &o) {
                // Whose session this ends, read before the local flag goes.
                const bool wasLocal = isLocalPlay();
                clearLocalPlay();
                o.motor.unlock();

                if (xSemaphoreTake(preferencesMutex,
                                   portTICK_PERIOD_MS * 1000) == pdTRUE) {
                    // The dashboard's session this ends, kept so that it is
                    // never taken up again (utils/emergencyRecord.h). A local
                    // session is not the dashboard's: none is recorded.
                    String endedSession;
                    if (!wasLocal) {
                        preferences.begin(Keys::session);
                        if (preferences.isKey(Keys::Session::sessionId)) {
                            endedSession = preferences.getString(
                                Keys::Session::sessionId, "");
                        }
                        preferences.end();
                    }

                    struct timeval tv{};
                    lockboxGetTimeOfDay(&tv, nullptr);
                    uint64_t currentDatetimeMs = ((uint64_t)tv.tv_sec) * 1000 +
                                                 ((uint64_t)tv.tv_usec) / 1000;

#ifdef SHORT_E_BLOCK
                    // if short timeouts are enabled, then the emergency unlock
                    // will brick the device for 1 minute
                    uint64_t emergencyEndDatetimeMs = currentDatetimeMs + 60000;
#else
                    // else, 24 hours.
                    uint64_t emergencyEndDatetimeMs =
                        currentDatetimeMs + 86400000;
#endif

                    preferences.begin(Keys::base, false);
                    preferences.putBool(Keys::Base::isEBlocked, true);
                    preferences.putULong64(Keys::Base::emergencyEnd,
                                           emergencyEndDatetimeMs);
                    preferences.putString(Keys::Base::emergencySession,
                                          endedSession);
                    preferences.end();
                    xSemaphoreGive(preferencesMutex);

                    ESP_LOGD(EMERGENCY_TAG, "emergencyEndDatetimeMs: %llu",
                             emergencyEndDatetimeMs);
                }
            };
            // The EMERGENCY report is kept in flash and repeated until the
            // broker has acknowledged it (utils/emergencyReport.h). Runs
            // after emergencyUnlockAction, so the report carries the lockout.
            auto reportEmergencyUnlock = [](LKBX &o) {
                o.reportEmergencyUnlock();
            };
            auto calibrateButton = [](LKBX &o) { o.calibrate(); };

            auto toggleAlpha = [](LKBX &o) {
                if (xSemaphoreTake(preferencesMutex,
                                   portTICK_PERIOD_MS * 1000) == pdTRUE) {
                    preferences.begin(Keys::base, false);
                    bool isAlpha =
                        preferences.getBool(Keys::Base::isAlpha, false);
                    preferences.putBool(Keys::Base::isAlpha, !isAlpha);
                    preferences.end();
                    xSemaphoreGive(preferencesMutex);

                    ESP_LOGD(TOGGLE_ALPHA_TAG, "isAlpha: %d", isAlpha);
                }

                // restart the device
                esp_restart();
            };

            auto restart = [](LKBX &o) {
                // since ESP_RESTART does not reset peripherals, we need to do
                // it ourselves.

                ledcWrite(0, 0);

                // Enter deep sleep.
                Motor::clear();
                o.motor.ina->powerSave(
                    true);  // Put INA219 into power-down mode
                o.display.writeCommand(ST77XX_SLPIN);

                pinMode(Pins::Display::TFT_RESET, OUTPUT);
                digitalWrite(Pins::Display::TFT_RESET, LOW);

                esp_restart();
            };

            // A boot step whose task cannot be created must not hold the box
            // on the loading page: a locked box that restarts has to reach its
            // locked page. The delay task reports such a step as done instead,
            // which is how each of these steps ends when the box is offline.
            auto checkWifi = [](LKBX &o) {
                ESP_LOGD(TASKDEBUG_TAG, "Creating check wifi task");
                if (!startTaskWithRetry(o.checkWifiTask, Tasks::checkWifi, &o,
                                        &TaskHandles::checkWifi)) {
                    delayTimeout = 100;
                }
            };

            // Async update check for onboarding flow.
            // Skips if offline, otherwise checks for major/minor version bumps.
            auto checkForUpdates = [](LKBX &o) {
                ESP_LOGD(TASKDEBUG_TAG, "Creating check for updates task");
                if (!startTaskWithRetry(o.checkForUpdatesTask,
                                        Tasks::checkUpdates, &o,
                                        &TaskHandles::checkUpdates)) {
                    delayTimeout = 100;
                }
            };

            // One-shot pairing check. Reuses MQTT auth status when available.
            auto startPairingCheck = [](LKBX &o) {
                ESP_LOGD(TASKDEBUG_TAG, "Creating pairing check task");
                if (!startTaskWithRetry(o.startPairingCheckTask,
                                        Tasks::pairingCheck, &o,
                                        &TaskHandles::pairingCheck)) {
                    delayTimeout = 100;
                }
            };

            // Pull current lock state during onboarding and route to
            // locking/home based on server response.
            auto syncLockStateOnBoot = [](LKBX &o) {
                ESP_LOGD(TASKDEBUG_TAG,
                         "Creating sync lock state on boot task");
                if (!startTaskWithRetry(o.syncLockStateOnBootTask,
                                        Tasks::syncLockState, &o,
                                        &TaskHandles::syncLockState)) {
                    // No memory for the task: carry on as after a failed pull,
                    // through the delay task, instead of waiting here forever.
                    bootHttpDone = true;
                    delayTimeout = 1000;
                }
            };

            // Synchronously sync the clock during boot when it's unset and
            // we're online, so downstream lock-timer comparisons use a valid
            // clock.
            auto syncClockOnBoot = [](LKBX &o) {
                ESP_LOGD(TASKDEBUG_TAG, "Creating sync clock on boot task");
                if (!startTaskWithRetry(o.syncClockOnBootTask, Tasks::syncClock,
                                        &o, &TaskHandles::syncClock)) {
                    // A clock restored offline still counts, as it does in
                    // the task. Online the clock stays unverified; a held
                    // LOCKED report retries the sync (startClockRetry).
                    if (isOffline() && isWallClockSet()) isTimeSynced = true;
                    delayTimeout = 100;
                }
            };

            // With a hidden timer the Sleep item must behave the same at any
            // time, or it would say the end is near.
            auto sleepReturnsToCountdown = []() {
                return sleepReturnsToTimer(isTimerDisplayed(), isFrozen(),
                                           savedTimerSleepDelayMs() == 1);
            };

            // A Chaster session asks the dashboard before it opens.
            auto setAskUnlock = [](LKBX &o) { o.askAction = AskAction::UNLOCK; };
            auto setAskBreak = [](LKBX &o) { o.askAction = AskAction::BREAK; };
            auto drawAskingChaster = [](LKBX &o) {
                o.drawNotice(Str::Screen::ASKING_CHASTER_TITLE,
                             Str::Screen::ASKING_CHASTER_DESC,
                             Action{.visible = false},
                             Action{.visible = false});
            };
            auto startChasterAsk = [](LKBX &o) { o.startChasterAsk(); };
            auto drawAskAnswer = [](LKBX &o) { o.drawAskAnswer(); };
            auto deepSleep = [](LKBX &o) {
                enterSleep(savedTimerSleepDelayMs(), [&o]() {
                    o.sendMessage(Str::Protocol::DISCONNECTED);
                });
            };

            // Preflight loading animation helpers.
            auto setLoadingText = [](const char *text) {
                return [text](LKBX &o) { o.setLoadingText(text); };
            };
            auto pauseLoadingAnimation = [](LKBX &o) {
                o.pauseLoadingAnimation();
            };
            auto stopLoadingAnimation = [](LKBX &o) {
                o.stopLoadingAnimation();
            };
            auto startLoadingAnimation = [](LKBX &o) {
                o.drawLoadingAnimation();
                o.setLoadingText(Str::Screen::LOADING_STARTING);
                o.startPreflightScreen();
            };

            // Phase-2 pairing poll: only runs while the pairing QR is shown.
            auto startPairingPoll = [](LKBX &o) {
                if (TaskHandles::pairingPoll != nullptr) return;
                ESP_LOGD(TASKDEBUG_TAG, "Creating pairing poll task");
                startTaskWithRetry(o.pollPairingTask, Tasks::pairingPoll, &o,
                                   &TaskHandles::pairingPoll);
            };

            auto needsPairing = [](LKBX &o) { return o.initNeedsPairing; };

            return make_transition_table(
                // clang-format off
                *"init"_s + on_entry<_> / (drawHello),
                "init"_s + done[isCalibrated] = "init.preflight"_s,
                "init"_s + done = "calibrate"_s,

                "calibrate"_s + on_entry<_> / (drawCalibrationQR, calibrateButton),
                "calibrate"_s + event<ButtonCalibrated> = "calibrate.motor"_s,
                "calibrate.motor"_s + on_entry<_> / drawLoadingScreen(Str::Screen::CALIBRATING_MOTOR_TITLE, Str::Screen::CALIBRATING_MOTOR_DESC),


                // Start the looping loading animation; it persists across every
                // init.preflight.* step. QR states pause it; exits stop it.
                "init.preflight"_s + on_entry<_> / (startLoadingAnimation),
                "init.preflight"_s + done[!hasFinishedOnboarding] = "init.preflight.welcome"_s,
                "init.preflight"_s + done = "init.preflight.wifiCheck"_s,

                // Welcome screen (first boot only) — QR, so pause the animation
                "init.preflight.welcome"_s + on_entry<_> / (pauseLoadingAnimation, drawWelcomeQR),
                "init.preflight.welcome"_s + buttonPress = "init.preflight.wifiCheck"_s,
                "init.preflight.welcome"_s + backPress / (setOnboardingComplete) = "init.preflight.wifiCheck"_s,
                // BLE provisioning: skip the portal and connect directly
                "init.preflight.welcome"_s + wifi / (setOnboardingComplete, saveWifiSync) = "init.preflight.wifiConnecting"_s,

                // Check WiFi and route accordingly
                "init.preflight.wifiCheck"_s + on_entry<_> / (setLoadingText(Str::Screen::LOADING_WIFI), checkWifi),
                "init.preflight.wifiCheck"_s + done[hasSavedWifiCredentials] = "init.preflight.wifiConnecting"_s,
                "init.preflight.wifiCheck"_s + done = "init.preflight.wifiSetup"_s,

                "init.preflight.wifiConnecting"_s + on_entry<_> / (setLoadingText(Str::Screen::LOADING_WIFI), startWifiConnection),
                "init.preflight.wifiConnecting"_s + done = "init.preflight.updateCheck"_s,
                // Returning devices must resume their saved timer and local
                // controls even when the network cannot be reached on wake.
                "init.preflight.wifiConnecting"_s + error[hasFinishedOnboarding] / startDeferredServices = "init.preflight.datetimeSync"_s,
                "init.preflight.wifiConnecting"_s + error[isError(ErrorT::WIFI_NETWORK_UNREACHABLE)] = "init.preflight.wifiErrorNetwork"_s,
                "init.preflight.wifiConnecting"_s + error[isError(ErrorT::WIFI_AUTH_FAILED)] = "init.preflight.wifiErrorAuth"_s,
                "init.preflight.wifiConnecting"_s + error[isError(ErrorT::WIFI_NO_IP)] = "init.preflight.wifiErrorNoIp"_s,
                "init.preflight.wifiConnecting"_s + error[isError(ErrorT::SERVER_UNREACHABLE)] = "init.preflight.wifiErrorServer"_s,

                "init.preflight.wifiSetup"_s + on_entry<_> / (pauseLoadingAnimation, openWifiSetup),
                "init.preflight.wifiSetup"_s + wifiConnected = "init.preflight.wifiConnecting"_s,
                "init.preflight.wifiSetup"_s + buttonPress = "init.preflight.updateCheck"_s,

                "init.preflight.wifiErrorNetwork"_s + on_entry<_> / (pauseLoadingAnimation, drawWifiErrorNetworkQR),
                "init.preflight.wifiErrorNetwork"_s + buttonPress = "init.preflight.wifiSetup"_s,
                "init.preflight.wifiErrorNetwork"_s + backPress = "init.preflight.wifiCheck"_s,

                "init.preflight.wifiErrorAuth"_s + on_entry<_> / (pauseLoadingAnimation, drawWifiErrorAuthQR),
                "init.preflight.wifiErrorAuth"_s + buttonPress = "init.preflight.wifiSetup"_s,
                "init.preflight.wifiErrorAuth"_s + backPress = "init.preflight.wifiCheck"_s,

                "init.preflight.wifiErrorNoIp"_s + on_entry<_> / (pauseLoadingAnimation, drawWifiErrorNoIpQR),
                "init.preflight.wifiErrorNoIp"_s + buttonPress = "init.preflight.wifiSetup"_s,
                "init.preflight.wifiErrorNoIp"_s + backPress = "init.preflight.wifiCheck"_s,

                "init.preflight.wifiErrorServer"_s + on_entry<_> / (pauseLoadingAnimation, drawWifiErrorServerQR),
                "init.preflight.wifiErrorServer"_s + buttonPress = "init.preflight.wifiSetup"_s,
                "init.preflight.wifiErrorServer"_s + backPress = "init.preflight.wifiCheck"_s,

                "init.preflight.updateCheck"_s + on_entry<_> / (setLoadingText(Str::Screen::LOADING_UPDATES), checkForUpdates),
                "init.preflight.updateCheck"_s + done / startDeferredServices = "init.preflight.datetimeSync"_s,
                "init.preflight.updateCheck"_s + forceUpdate / stopLoadingAnimation = "update"_s,

                "init.preflight.datetimeSync"_s + on_entry<_> / (setLoadingText(Str::Screen::LOADING_DATE), syncClockOnBoot),
                "init.preflight.datetimeSync"_s + done = "init.preflight.mqttConnecting"_s,

                "init.preflight.mqttConnecting"_s + on_entry<_> / (setLoadingText(Str::Screen::LOADING_SESSION), startMqttConnection),
                "init.preflight.mqttConnecting"_s + done = "init.preflight.pairingCheck"_s,
                "init.preflight.mqttConnecting"_s + error[hasFinishedOnboarding && isMqttConnectionError] = "init.preflight.pairingCheck"_s,
                "init.preflight.mqttConnecting"_s + error[isError(ErrorT::AUTH_RATE_LIMITED)] = "init.preflight.authRateLimited"_s,
                "init.preflight.mqttConnecting"_s + error[isError(ErrorT::DASHBOARD_SIGN_IN_FAILED)] = "init.preflight.signInFailed"_s,
                "init.preflight.mqttConnecting"_s + error[isError(ErrorT::DEVICE_KEY_FAILED)] = "init.preflight.deviceKeyFailed"_s,
                "init.preflight.mqttConnecting"_s + error[isMqttConnectionError] = "init.preflight.mqttError"_s,

                "init.preflight.authRateLimited"_s + on_entry<_> / (pauseLoadingAnimation, drawAuthRateLimitedQR),
                "init.preflight.authRateLimited"_s + buttonPress = "init.preflight.mqttConnecting"_s,
                "init.preflight.authRateLimited"_s + backPress = "init.preflight.wifiCheck"_s,

                "init.preflight.signInFailed"_s + on_entry<_> / (pauseLoadingAnimation, drawSignInFailedQR),
                "init.preflight.signInFailed"_s + buttonPress = "init.preflight.mqttConnecting"_s,
                "init.preflight.signInFailed"_s + backPress = "init.preflight.wifiCheck"_s,

                // E-LOCK-999, first setup only: the key could not be stored.
                // Continuing signs in with v1; the key is not requested again
                // this boot.
                "init.preflight.deviceKeyFailed"_s + on_entry<_> / (pauseLoadingAnimation, drawDeviceKeyFailedQR),
                "init.preflight.deviceKeyFailed"_s + buttonPress = "init.preflight.mqttConnecting"_s,
                "init.preflight.deviceKeyFailed"_s + backPress = "init.preflight.wifiCheck"_s,

                "init.preflight.mqttError"_s + on_entry<_> / (pauseLoadingAnimation, drawMqttErrorQR),
                "init.preflight.mqttError"_s + buttonPress = "init.preflight.mqttConnecting"_s,
                "init.preflight.mqttError"_s + backPress = "init.preflight.wifiCheck"_s,

                // Pairing check (phase 1): decide whether the code is needed,
                // without showing it. Keeps the loading animation running.
                "init.preflight.pairingCheck"_s + on_entry<_> / (setLoadingText(Str::Screen::LOADING_PAIRING), startPairingCheck),
                "init.preflight.pairingCheck"_s + done[needsPairing] = "init.preflight.pairingPrompt"_s,
                "init.preflight.pairingCheck"_s + done = "init.preflight.syncLockState"_s,
                "init.preflight.pairingCheck"_s + error[hasFinishedOnboarding] = "init.preflight.syncLockState"_s,
                "init.preflight.pairingCheck"_s + error = "init.preflight.wifiErrorServer"_s,

                // Pairing prompt (phase 2): only shown when not yet paired
                "init.preflight.pairingPrompt"_s + on_entry<_> / (pauseLoadingAnimation, drawInitPairingQR, startPairingPoll),
#include "PairingDisplayTransitions.inc"
                "init.preflight.pairingPrompt"_s + event<PairingConfirmed> = "init.preflight.pairingSuccess"_s,
                "init.preflight.pairingPrompt"_s + buttonPress = "init.preflight.syncLockState"_s,

                // Brief success screen after pairing
                "init.preflight.pairingSuccess"_s + on_entry<_> / (pauseLoadingAnimation, drawPage(&initPairingSuccessPage), startDelay),
                "init.preflight.pairingSuccess"_s + done = "init.preflight.syncLockState"_s,

                "init.preflight.syncLockState"_s + on_entry<_> / (setLoadingText(Str::Screen::LOADING_SYNC), syncLockStateOnBoot),
                "init.preflight.syncLockState"_s + lock [isOnActiveBreak] / stopLoadingAnimation = "locked.break"_s,
                "init.preflight.syncLockState"_s + lock / (stopLoadingAnimation, lockAction) = "locking"_s,
                "init.preflight.syncLockState"_s + locktimer [isOnActiveBreak] / stopLoadingAnimation = "locked.break"_s,
                "init.preflight.syncLockState"_s + locktimer / (stopLoadingAnimation, lockTimerAction) = "locking"_s,
                "init.preflight.syncLockState"_s + lockOffered / (stopLoadingAnimation, movePaddleToUnlock) = "acceptLock"_s,
                "init.preflight.syncLockState"_s + breakE / stopLoadingAnimation = "locked.break"_s,
                "init.preflight.syncLockState"_s + done / stopLoadingAnimation = "home"_s,


                "home"_s + on_entry<_> / [](LKBX &) { bootHttpDone = true; },
                "home"_s [isLockOffered] / movePaddleToUnlock = "acceptLock"_s,
                "home"_s [isEmergencyBlocked] / (clearLocalPlay, unlockAction) = "emergency.blocked"_s,
                "home"_s [isOnActiveBreak] = "locked.break"_s,
                "home"_s [isLockedTimer] / lockTimerAction = "locking"_s,
                "home"_s [isLocked] / lockAction = "locking"_s,
                "home"_s [always] = "home.idle"_s,

                "home.idle"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "home.idle"_s + on_entry<_> / (drawMenu, clearLocalPlay),
                "home.idle"_s + lock / (clearLocalPlay, lockAction) = "locking"_s,
                "home.idle"_s + locktimer / (clearLocalPlay, lockTimerAction) = "locking"_s,
                "home.idle"_s + buttonPress [(isOption(MenuItemE::LOCK))] = "permalock.confirm"_s,
                "home.idle"_s + buttonPress [(isOption(MenuItemE::LOCK_TIMER))] = "timer"_s,
                "home.idle"_s + buttonPress [(isOption(MenuItemE::SETTINGS))] = "home.settings"_s,
                "home.idle"_s + buttonPress [(isOption(MenuItemE::SLEEP))] = "home.sleep"_s,
                "home.idle"_s + forceUpdate = "update"_s,
                "home.idle"_s + recalibrate / clearCalibration = "calibrate"_s,

                "home.settings"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "home.settings"_s + on_entry<_> / (drawSettingsMenu),
                "home.settings"_s + backPress = "home"_s,
                "home.settings"_s + buttonPress [(isOption(MenuItemE::WIFI_SETTINGS))] = "wmConfig"_s,
                "home.settings"_s + buttonPress [(isOption(MenuItemE::UPDATE) && isOnline)] = "update"_s,
                "home.settings"_s + buttonPress [(isOption(MenuItemE::UPDATE) && isOffline)] = "update.wifi"_s,
                "home.settings"_s + buttonPress [(isOption(MenuItemE::PAIRING)) && isOnline] = "pairing"_s,
                "home.settings"_s + buttonPress [(isOption(MenuItemE::PAIRING)) && isOffline] = "pairing.wifi"_s,
                "home.settings"_s + buttonPress [(isOption(MenuItemE::ENROL_IN_ALPHA))] = "enrolInAlpha"_s,
                "home.settings"_s + buttonPress [(isOption(MenuItemE::BACK))] = "home"_s,
                "home.settings"_s + buttonPress [(isOption(MenuItemE::RESTART))] = "restart"_s,
                "home.settings"_s + forceUpdate = "update"_s,
                "home.settings"_s + recalibrate / clearCalibration = "calibrate"_s,
                "home.settings"_s + lock / (clearLocalPlay, lockAction) = "locking"_s,
                "home.settings"_s + locktimer / (clearLocalPlay, lockTimerAction) = "locking"_s,
                "home.settings"_s + unlock / (clearLocalPlay, unlockAction) = "home.settings"_s,

                "enrolInAlpha"_s + on_entry<_> / (drawEnrolInAlphaQR),
                "enrolInAlpha"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "enrolInAlpha"_s + backPress = "home.settings"_s,
                "enrolInAlpha"_s + buttonPress / (toggleAlpha) = X,

                "pairing"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "pairing"_s + on_entry<_> / (drawPairingQR, startPairingPoll),
                "pairing"_s + backPress = "home.settings"_s,
                "pairing"_s + buttonPress = "home"_s,
                "pairing"_s + forceUpdate = "update"_s,

                "pairing.wifi"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "pairing.wifi"_s + on_entry<_> / (openWifiSetup),
                "pairing.wifi"_s + buttonPress = "home.settings"_s,
                "pairing.wifi"_s + backPress = "home.settings"_s,
                "pairing.wifi"_s + wifiConnected = "pairing"_s,
                
                "home.sleep"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "home.sleep"_s + on_entry<_> / (drawSleepQR),
                "home.sleep"_s + buttonPress / deepSleep = "home"_s,
                "home.sleep"_s + backPress = "home"_s,
                "home.sleep"_s + forceUpdate = "update"_s,

                // timer state to let the user set the lock duration.
                "timer"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "timer"_s + on_entry<_> / (drawTimerConfig),
                "timer"_s + backPress = "home.idle"_s,
                "timer"_s + lock / (clearLocalPlay, lockAction) = "locking"_s,
                "timer"_s + locktimer / (clearLocalPlay, lockTimerAction) = "locking"_s,
#include "LocalTimerTransitions.inc"
                "timer"_s + forceUpdate = "update"_s,

                "locking"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locking"_s + on_entry<_> / (drawLockAnimation, startDelay),
                // A replacement session can arrive before the previous motor
                // transition finishes. Apply the newest target before preflight.
                "locking"_s + lock / (clearLocalPlay, lockAction) = "locking"_s,
                "locking"_s + locktimer / (clearLocalPlay, lockTimerAction) = "locking"_s,
                "locking"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
                "locking"_s + breakE = "locked.break"_s,
                "locking"_s + done = "locking.preflight"_s,
                "locking"_s + forceUpdate = "update"_s,

                "locking.preflight"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locking.preflight"_s + on_entry<_> / (startPreflight),
#include "FrozenTransitions.inc"
#include "TimerExpiryTransitions.inc"
#include "ChasterAskTransitions.inc"
                "locking.preflight"_s + done[isReadyToUnlock] / sendMessage(Str::Protocol::LOCKED) = "locked.ready"_s,
                "locking.preflight"_s + done[isLockedTimer] / sendMessage(Str::Protocol::LOCKED) = "locked.timer"_s,
                "locking.preflight"_s + done[isLocked] / sendMessage(Str::Protocol::LOCKED) = "locked"_s,
                "locking.preflight"_s + done / sendMessage(Str::Protocol::UNLOCKED) = "home"_s,
                "locking.preflight"_s + error[isError(ErrorT::CANNOT_LOCK)] / (movePaddleToUnlock) = "locking.errorLock"_s,
                // CANNOT_UNLOCK is in UnlockErrorTransitions.inc, below.
                "locking.preflight"_s + error[isError(ErrorT::NO_BACKPLATE)] / (movePaddleToUnlock) = "locking.errorBackplate"_s,
                "locking.preflight"_s + forceUpdate = "update"_s,

                "locking.errorLock"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locking.errorLock"_s + on_entry<_> / (drawErrorLockQR),
                "locking.errorLock"_s + buttonPress[isLockedTimer] / (lockTimerAction) = "locking"_s,
                "locking.errorLock"_s + buttonPress[isLocked] / (lockAction) = "locking"_s,
                "locking.errorLock"_s + backPress / clearCalibration = "calibrate"_s,
                "locking.errorLock"_s + forceUpdate = "update"_s,

#include "UnlockErrorTransitions.inc"

                "locking.errorBackplate"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locking.errorBackplate"_s + on_entry<_> / (movePaddleToUnlock, drawErrorNoBackplateQR),
                "locking.errorBackplate"_s + buttonPress = "restart"_s,
                "locking.errorBackplate"_s + backPress / clearCalibration = "calibrate"_s,
                "locking.errorBackplate"_s + forceUpdate = "update"_s,

                "permalock.confirm"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "acceptLock"_s + on_entry<_> / drawAcceptLockQR,
                "acceptLock"_s + buttonPress[acceptOfferedLockSession] / (clearEBlock, lockTimerAction) = "locking"_s,
                "acceptLock"_s + backPress / declineLockAction = "home"_s,
                "acceptLock"_s + decline / declineLockAction = "home"_s,
                "acceptLock"_s + lockOffered / drawAcceptLockQR,
                "acceptLock"_s + unlock / unlockAction = "home"_s,
                "acceptLock"_s + forceUpdate = "update"_s,

                "permalock.confirm"_s + on_entry<_> / (drawPermalockQR),
                "permalock.confirm"_s + buttonPress / (setLocalPlay, lockAction) = "locking"_s,
                "permalock.confirm"_s + backPress = "home"_s,
                "permalock.confirm"_s + forceUpdate = "update"_s,

                "locked"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locked"_s + on_entry<_> / (drawLockedActions),
                "locked"_s + buttonPress  = "lockMenu"_s,
                // Messages from Dashboard
                "locked"_s + locktimer / (switchToTimer)= "locking"_s,
                "locked"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
                "locked"_s + forceUpdate = "update"_s,

                "locked.timer"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locked.timer"_s + on_entry<_> / (drawLockedActions, startUnlockTimer),
                "locked.timer"_s + buttonPress = "lockMenu"_s,
                // Messages from Dashboard
                "locked.timer"_s + lock / (switchToLock) = "locking"_s,
                "locked.timer"_s + locktimer / (switchToTimer) = "locking"_s,
                "locked.timer"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
                "locked.timer"_s + forceUpdate = "update"_s,

                "locked.ready"_s + on_entry<_> / (drawReadyToUnlock, startUnlockTimer),
                // The Unlock button's rows are in ChasterAskTransitions.inc:
                // a Chaster session asks first, any other unlocks.
                "locked.ready"_s + backPress = "locked.ready.addTime"_s,
                "locked.ready"_s + locktimer[!isReadyToUnlock] / switchToTimer = "locked.timer"_s,
                "locked.ready"_s + lock / switchToLock = "locked"_s,
                "locked.ready"_s + unlock / (clearActiveBreak, clearLocalPlay, unlockAction) = "locking"_s,
                "locked.ready"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locked.ready"_s + forceUpdate = "update"_s,

                "locked.ready.addTime"_s + on_entry<_> / drawAddTimeQR,
                "locked.ready.addTime"_s + backPress = "locked.ready"_s,
                "locked.ready.addTime"_s + locktimer[!isReadyToUnlock] / switchToTimer = "locked.timer"_s,
                "locked.ready.addTime"_s + lock / switchToLock = "locked"_s,
                "locked.ready.addTime"_s + unlock / (clearActiveBreak, clearLocalPlay, unlockAction) = "locking"_s,
                "locked.ready.addTime"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locked.ready.addTime"_s + forceUpdate = "update"_s,

                "lockMenu"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "lockMenu"_s + on_entry<_> / (drawEmergencyMenu, resumeUnlockTimer),
                "lockMenu"_s + buttonPress[(isOption(MenuItemE::SLEEP))] = "lockMenu.sleep"_s,
                "lockMenu"_s + buttonPress[(isOption(MenuItemE::WIFI_SETTINGS))] = "wmConfig"_s,
                "lockMenu"_s + buttonPress[(isOption(MenuItemE::RESTART))] = "restart"_s,
                "lockMenu"_s + buttonPress[(isOption(MenuItemE::BREAK))] = "locked.breaking"_s,
                "lockMenu"_s + buttonPress[(isOption(MenuItemE::EMERGENCY))] = "emergency.unlock"_s,
                "lockMenu"_s + buttonPress[(isOption(MenuItemE::VOTE))] = "lockMenu.vote"_s,
                "lockMenu"_s + lock / (switchToLock) = "locking"_s,
                "lockMenu"_s + locktimer / (switchToTimer) = "locking"_s,
                "lockMenu"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
                "lockMenu"_s + forceUpdate = "update"_s,

                "locked.breaking"_s + on_entry<_> / (drawBreakQR),
                "locked.breaking"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locked.breaking"_s + buttonPress = "locked.breakPreflight"_s,
                "locked.breaking"_s + backPress = "lockMenu"_s,

                // TODO implement the BREAKING.
                "locked.breakPreflight"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locked.breakPreflight"_s + on_entry<_> / (startBreakPreflight),
                "locked.breakPreflight"_s + error[isError(ErrorT::BREAK_NOT_ENABLED)] / (drawBreakNotEnabledQR) = "locked.breakError"_s,
                "locked.breakPreflight"_s + error[isError(ErrorT::BREAK_KEYHOLDER_ONLY)] / (drawBreakKeyholderOnlyQR) = "locked.breakError"_s,
                "locked.breakPreflight"_s + error[isError(ErrorT::TOO_SOON_FOR_BREAK)] = "locked.breakCooldown"_s,
                "locked.breakPreflight"_s + backPress = "locking"_s,

                "locked.breakError"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locked.breakError"_s + buttonPress = "lockMenu"_s,
                "locked.breakError"_s + backPress = "lockMenu"_s,

                "locked.breakCooldown"_s + on_entry<_> / drawBreakTooSoonQR,
                "locked.breakCooldown"_s + error[isError(ErrorT::TOO_SOON_FOR_BREAK)] / [](LKBX &o) { o.drawBreakCooldown(false); },
                "locked.breakCooldown"_s + done = "locked.breaking"_s,
                "locked.breakCooldown"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "locked.breakCooldown"_s + buttonPress = "lockMenu"_s,
                "locked.breakCooldown"_s + backPress = "lockMenu"_s,

                "locked.break"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                // Shared entry reports dashboard and local breaks after their
                // start timestamp has been persisted by startBreakTimer.
#include "BreakEntryTransitions.inc"
                "locked.break"_s + done[!isBreakEndRequested] / (clearActiveBreak, movePaddleToLock) = "locking"_s,
                "locked.break"_s + buttonPress[acceptBreakEnd] / movePaddleToLock = "locking"_s,
                "locked.break"_s + unlock / (clearActiveBreak, clearLocalPlay, unlockAction) = "locking"_s,

                "lockMenu.wifi"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "lockMenu.wifi"_s + on_entry<_> / (openWifiSetup),
                "lockMenu.wifi"_s + buttonPress = "lockMenu"_s,
                "lockMenu.wifi"_s + backPress = "lockMenu"_s,
                "lockMenu.wifi"_s + wifiConnected = "lockMenu"_s,
                "lockMenu.wifi"_s + forceUpdate = "update"_s,

                "lockMenu.sleep"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "lockMenu.sleep"_s + on_entry<_> / (drawSleepQR),
                // Keep the timer alive instead of spending its last seconds
                // rebooting through preflight after a short sleep.
                "lockMenu.sleep"_s + buttonPress[sleepReturnsToCountdown] = "locked.timer"_s,
                "lockMenu.sleep"_s + buttonPress / deepSleep = "lockMenu"_s,
                "lockMenu.sleep"_s + backPress = "lockMenu"_s,
                "lockMenu.sleep"_s + lock / (switchToLock) = "locking"_s,
                "lockMenu.sleep"_s + locktimer / (switchToTimer)= "locking"_s,
                "lockMenu.sleep"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
                "lockMenu.sleep"_s + breakE = "locked.break"_s,
                "lockMenu.sleep"_s + forceUpdate = "update"_s,

                // The session's voting link as a QR code; either button returns.
                "lockMenu.vote"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "lockMenu.vote"_s + on_entry<_> / (drawVoteQR),
                "lockMenu.vote"_s + buttonPress = "lockMenu"_s,
                "lockMenu.vote"_s + backPress = "lockMenu"_s,
                "lockMenu.vote"_s + lock / (switchToLock) = "locking"_s,
                "lockMenu.vote"_s + locktimer / (switchToTimer) = "locking"_s,
                "lockMenu.vote"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
                "lockMenu.vote"_s + breakE = "locked.break"_s,
                "lockMenu.vote"_s + forceUpdate = "update"_s,

                "emergency.unlock"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "emergency.unlock"_s + on_entry<_> / (drawEUnlockQR),
                "emergency.unlock"_s + backPress = "lockMenu"_s,
                "emergency.unlock"_s + buttonPress / (emergencyUnlockAction, reportEmergencyUnlock) = "locking"_s,
                "emergency.unlock"_s + lock / (switchToLock)= "locking"_s,
                "emergency.unlock"_s + locktimer / (switchToTimer)= "locking"_s,
                "emergency.unlock"_s + unlock / (clearLocalPlay, unlockAction) = "locking"_s,
                "emergency.unlock"_s + breakE = "locked.break"_s,
                "emergency.unlock"_s + forceUpdate = "update"_s,

                "emergency.blocked"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "emergency.blocked"_s + on_entry<_> / (drawEBlockedQR, startUnlockTimer),
                "emergency.blocked"_s + unlock / (clearEBlock, clearLocalPlay, unlockAction) = "home"_s,
                "emergency.blocked"_s + lock / (clearEBlock, clearLocalPlay, lockAction) = "locking"_s,
                "emergency.blocked"_s + locktimer / (clearEBlock, clearLocalPlay, lockTimerAction) = "locking"_s,
                "emergency.blocked"_s + buttonPress = "emergency.wifi"_s,
                "emergency.blocked"_s + forceUpdate = "update"_s,

                "emergency.wifi"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "emergency.wifi"_s + on_entry<_> / (openWifiSetup),
                "emergency.wifi"_s + buttonPress = "emergency.blocked"_s,
                "emergency.wifi"_s + backPress = "emergency.blocked"_s,
                "emergency.wifi"_s + wifiConnected = "emergency.blocked"_s,
                "emergency.wifi"_s + unlock / (clearEBlock, clearLocalPlay, unlockAction) = "home"_s,
                "emergency.wifi"_s + lock / (clearEBlock, clearLocalPlay, lockAction) = "locking"_s,
                "emergency.wifi"_s + locktimer / (clearEBlock, clearLocalPlay, lockTimerAction) = "locking"_s,
                "emergency.wifi"_s + forceUpdate = "update"_s,

                "wmConfig"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "wmConfig"_s + on_entry<_> / (openWifiSetup),
                "wmConfig"_s + buttonPress = "home"_s,
                "wmConfig"_s + backPress = "home"_s,
                "wmConfig"_s + lock / (clearLocalPlay, lockAction) = "locking"_s,
                "wmConfig"_s + locktimer / (clearLocalPlay, lockTimerAction) = "locking"_s,
                "wmConfig"_s + wifiConnected = "home"_s,
                "wmConfig"_s + forceUpdate = "update"_s,

                "wmConfig.error"_s + on_entry<_> / (drawCouldNotConnectQR),
                "wmConfig.error"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "wmConfig.error"_s + buttonPress = "home"_s,
                "wmConfig.error"_s + backPress = "home"_s,
                "wmConfig.error"_s + forceUpdate = "update"_s,

                "update.wifi"_s + on_entry<_> / (openWifiSetup),
                "update.wifi"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "update.wifi"_s + backPress = "home"_s,
                "update.wifi"_s + buttonPress[isOnline] = "update"_s,
                "update.wifi"_s + buttonPress[isOffline] = "wmConfig.error"_s,
                "update.wifi"_s + wifiConnected = "update"_s,
                "update.wifi"_s + forceUpdate = "update"_s,
                

                "update"_s + on_entry<_> / (drawLoadingScreen(Str::Screen::UPDATING_TITLE, Str::Screen::UPDATING_DESC), startUpdate),
                "update"_s + done[isOnline] = "update.idle"_s,
                "update"_s + done[isOffline] = "wmConfig.error"_s,
                "update"_s + error[isError(ErrorT::WIFI_ERROR)] = "wmConfig.error"_s,
                "update"_s + error[isError(ErrorT::NO_UPDATE_AVAILABLE)] = "update.none"_s,
                "update"_s + error[isError(ErrorT::OTA_FAILED)] = "update.failed"_s,

                "update.failed"_s + on_entry<_> / (drawPage(&otaFailedPage), [](LKBX &) { delayTimeout = 5000; }),
                "update.failed"_s + done / restart = X,

                "update.none"_s + on_entry<_> / ([](LKBX &) { bootHttpDone = true; }, drawNoUpdateQR),
                "update.none"_s + lockOffered / (clearActiveBreak, movePaddleToUnlock) = "acceptLock"_s,
                "update.none"_s + buttonPress = "home"_s,
                "update.none"_s + backPress = "home"_s,

                // Wait for restart.
                "update.idle"_s  = X,

                "restart"_s / restart = X

                // clang-format on
            );
        }
    };

    /**
     * ///////////////////////////////////////////
     * ////
     * ////  Private Objects and Services
     * ////
     * ///////////////////////////////////////////
     */
    StateLogger logger;

    /**
     * ///////////////////////////////////////////
     * ////
     * ////  Private Functions
     * ////
     * ///////////////////////////////////////////
     */
    void drawHello();
    void drawTimerConfig();
    void drawLockAnimation();
    void drawLockedActions();
    void drawReadyToUnlock();
    void drawMenu();
    void drawEmergencyMenu();

    /**
     * ///////////////////////////////////////////
     * ////
     * ////  Static Functions and Tasks
     * ////
     * ///////////////////////////////////////////
     */
    // A task function ends with vTaskDelete, which never returns, so nothing
    // declared in it is ever destroyed. A task that owns memory while it runs
    // (a canvas, a String, a sign-in answer) therefore does its work in a
    // run* function and only calls that function and vTaskDelete itself.
    static void drawHelloTask(void *pvParameters);
    static void runHello(LKBX *plkbx);
    static void startUnlockTimerTask(void *pvParameters);
    static void runUnlockTimer(LKBX *plkbx);
    static void startWifiConnectionTask(void *pvParameters);
    static void runWifiConnection(LKBX *plkbx);
    static void checkWifiTask(void *pvParameters);
    static void checkForUpdatesTask(void *pvParameters);
    static void startPairingCheckTask(void *pvParameters);
    static void pollPairingTask(void *pvParameters);
    static void syncLockStateOnBootTask(void *pvParameters);
    static void syncClockOnBootTask(void *pvParameters);
    static void retryClockSyncTask(void *pvParameters);

  public:
    LKBX(Adafruit_ST7789 &display, AiEsp32RotaryEncoder &encoder, Motor &motor,
         esp_mqtt_client_handle_t &client);

    void initDelayTask();
    // A dashboard clear that names no session: re-read the state over HTTPS.
    // Also what a box does when it may have missed a message. Safe from any
    // task: it only notes that the read is wanted. The delay task runs it and
    // tries until one read has completed (serviceStateRead).
    void confirmServerClear() { stateReadPlan.want(); }
    // Shown over the Wi-Fi setup page when its portal could not be opened,
    // so the page does not offer a network that is not there.
    void drawWifiSetupUnavailable();

    Adafruit_ST7789 &display;
    AiEsp32RotaryEncoder &encoder;
    Motor &motor;
    esp_mqtt_client_handle_t &client;

    std::unique_ptr<
        sml::sm<InnerStateMachine, sml::thread_safe<ESP32RecursiveMutex>,
                sml::logger<StateLogger>>>
        sm = nullptr;  // The state machine

    String errorMessage = "";
    // The local timer's duration in ms, as the timer page last showed it. Zero
    // until the page has drawn a duration: nothing can be locked for it.
    uint64_t duration = 0;
    // What the timer page shows. The dial task repaints the whole page when
    // the page was entered again (REDRAW), and once the dial is turned after
    // a notice was drawn over it (NOTICE).
    enum class TimerPage : uint8_t { DIAL, REDRAW, NOTICE };
    std::atomic<TimerPage> timerPage{TimerPage::DIAL};
    // Shown when the timer page cannot be drawn or the timer's end cannot be
    // saved. The box does not lock.
    void drawTimerUnavailable();
    // Set by the phase-1 pairing check; routes preflight to the pairing prompt.
    bool initNeedsPairing = false;
    bool initPairingStatusKnown = false;
    // Credentials provisioned via BLE — used by startWifiConnectionTask
    String pendingWifiSsid = "";
    String pendingWifiPassword = "";
    const MenuItem *activeMenu{};
    int menuSize = 0;
    void drawQRCode(const String &title, const String &description,
                    const String &url, const String &label = Str::Label::INFO,
                    const Action &right = continueAction,
                    const Action &left = backAction,
                    bool descriptionOnly = false);
    void drawBreakEndRequest(const String &requestedBy);
    void drawVoteQR();

    void sendMessage(const String &message, const String &sessionId = "") {
        // Only LOCKED is tracked until the broker acknowledges it. Any other
        // state message means the device has moved on, except DISCONNECTED:
        // it is sent on the way to sleep, and sleep can still be cancelled.
        const bool isLockedReport = message == Str::Protocol::LOCKED;
        const bool isSleepReport = message == Str::Protocol::DISCONNECTED;
        if (!isLockedReport && !isSleepReport) {
            lockedReport::tracker.clear();
        }
        // A report that could not go out earlier is out of date now, with the
        // same exception.
        if (!isSleepReport) unsentReport::holder.clear();
        if (!client || !isTimeSynced) {
            ESP_LOGW(MQTT_TAG, "%s, skipping state message: %s",
                     client ? "Time not synced yet" : "No MQTT client yet",
                     message.c_str());
            // Keep the report pending; the retry sends it once ready.
            if (isLockedReport) lockedReport::tracker.onReport(-1, millis(), {});
            return;
        }

        String msg = createStateMessage(message, true, sessionId);

        const String topic = cloudTopic();
        // The tracker keeps the plain report, so each retry is sealed afresh.
        const String sealed = sealCloudReport(
            topic.c_str(), lockbox::security::ReportKind::state, msg);
        if (sealed.isEmpty()) {
            // Never sent in plain JSON; a LOCKED report retries once it seals,
            // and any other is kept for a retry.
            if (isLockedReport) lockedReport::tracker.onReport(-1, millis(), msg.c_str());
            else if (!isSleepReport) holdUnsentReport(msg);
            return;
        }

        int result = esp_mqtt_client_enqueue(client, topic.c_str(), sealed.c_str(),
                                             0, 2, false, true);
        if (isLockedReport) {
            lockedReport::tracker.onReport(result, millis(), msg.c_str());
        } else if (result < 0 && !isSleepReport) {
            // No memory for the client's outbox either.
            holdUnsentReport(msg);
        }

        ESP_LOGD(MQTT_TAG, "Message enqueue result: %d", result);
    }

    // The wearer used the emergency unlock. The report is written to flash
    // before anything is sent and is owed until the broker has acknowledged
    // it: the activity log task sends it, now if the box is connected, and
    // again after a reconnect or a restart for as long as the dashboard still
    // holds the session (serviceEmergencyReport, utils/emergencyReport.h).
    // Sent once, as it was, a report that was lost left the dashboard holding
    // the session as locked for good.
    void reportEmergencyUnlock() {
        // The box has moved on from whatever it reported before.
        lockedReport::tracker.clear();
        unsentReport::holder.clear();

        const String report =
            createStateMessage(Str::Protocol::EMERGENCY, true, "");
        bool stored = false;
        // Whether it reports the end of a dashboard session, as
        // emergencyUnlockAction has just recorded.
        bool forSession = false;
        if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) ==
            pdTRUE) {
            preferences.begin(Keys::base, false);
            stored = preferences.putString(Keys::Base::emergencyReport,
                                           report) == report.length();
            preferences.putBool(Keys::Base::emergencyOwed, stored);
            forSession =
                !emergencyStringLocked(Keys::Base::emergencySession).isEmpty();
            preferences.end();
            xSemaphoreGive(preferencesMutex);
        }
        // Owed in RAM either way, so it is sent during this boot at least.
        emergencyReport::owed.owe(report.c_str(), forSession);
        emergencyReportLoaded = true;
        if (!stored) {
            ESP_LOGE(EMERGENCY_TAG,
                     "The emergency report could not be stored; it is only "
                     "repeated until the next restart");
        }
    }

    // From the activity log task: sends the emergency report while it is
    // owed, and records the broker's acknowledgement.
    void serviceEmergencyReport() {
        // Once per boot: a report that was still owed when the box restarted.
        if (!emergencyReportLoaded) loadOwedEmergencyReport();

        // The acknowledgement is noted on the MQTT task and written to flash
        // here, where waiting for the preferences is harmless.
        if (emergencyReport::owed.takeAcknowledgement()) {
            emergencyAckToRecord = true;
        }
        if (emergencyAckToRecord && recordEmergencyReportDelivered()) {
            emergencyAckToRecord = false;
        }

        // A report for a dashboard session waits for the dashboard's state.
        // The boot reads it and so does every reconnect, but if that read
        // failed nothing would ask again while the connection holds. So
        // after half a minute of waiting on a live connection the state read
        // is asked for once; it is tried until one has completed.
        if (!mqttConnected || !emergencyReport::owed.isWaitingForState()) {
            emergencyWaitSinceMs = 0;
            emergencyStateAsked = false;
        } else if (emergencyWaitSinceMs == 0) {
            emergencyWaitSinceMs = millis() | 1;
        } else if (!emergencyStateAsked &&
                   static_cast<uint32_t>(millis() - emergencyWaitSinceMs) >=
                       30000) {
            emergencyStateAsked = true;
            ESP_LOGI(EMERGENCY_TAG,
                     "The emergency report is waiting for the dashboard's "
                     "state; reading it");
            confirmServerClear();
        }

        if (!mqttConnected || !client || !isTimeSynced ||
            !emergencyReport::owed.due(millis())) {
            return;
        }
        const std::string report = emergencyReport::owed.report();
        const String topic = cloudTopic();
        // Sealed afresh each time; the report inside keeps its id, so the
        // dashboard takes a second copy as a duplicate.
        const String sealed = sealCloudReport(
            topic.c_str(), lockbox::security::ReportKind::state,
            String(report.c_str()));
        int result = -1;
        if (!sealed.isEmpty()) {
            result = esp_mqtt_client_enqueue(client, topic.c_str(),
                                             sealed.c_str(), 0, 2, false, true);
        }
        emergencyReport::owed.onAttempt(result, millis());
        ESP_LOGW(MQTT_TAG, "Emergency report %s (%u/%u), id %d",
                 result >= 0 ? "sent" : "could not be sent yet",
                 static_cast<unsigned>(emergencyReport::owed.attemptCount()),
                 static_cast<unsigned>(emergencyReport::kMaxAttempts), result);
    }

    // Reads a report that was owed when the box last stopped. False when the
    // preferences are busy; tried again on the next pass.
    bool loadOwedEmergencyReport() {
        if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return false;
        }
        preferences.begin(Keys::base);
        const bool owed = preferences.getBool(Keys::Base::emergencyOwed, false);
        const String report =
            owed ? emergencyStringLocked(Keys::Base::emergencyReport)
                 : String();
        const bool forSession =
            !emergencyStringLocked(Keys::Base::emergencySession).isEmpty();
        preferences.end();
        xSemaphoreGive(preferencesMutex);

        emergencyReportLoaded = true;
        // The boot's own state read may have been first: then it has already
        // said whether the report is still owed.
        if (owed && !report.isEmpty() && !emergencyReport::owed.isOwed()) {
            emergencyReport::owed.restore(report.c_str(), forSession);
            ESP_LOGW(EMERGENCY_TAG,
                     "An emergency report is still owed from before the "
                     "restart; it is sent %s",
                     forSession ? "if the dashboard still holds the session"
                                : "once the box is connected");
        }
        return true;
    }

    // The broker has the report. False when the preferences are busy.
    bool recordEmergencyReportDelivered() {
        if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return false;
        }
        preferences.begin(Keys::base, false);
        // Owed again meanwhile (the dashboard still showed the session):
        // that newer fact stands.
        if (!emergencyReport::owed.isOwed()) {
            preferences.putBool(Keys::Base::emergencyOwed, false);
            // The text is kept while the dashboard may still show the
            // session it ended; without one it cannot be owed again.
            if (emergencyStringLocked(Keys::Base::emergencySession).isEmpty() &&
                preferences.isKey(Keys::Base::emergencyReport)) {
                preferences.remove(Keys::Base::emergencyReport);
            }
        }
        preferences.end();
        xSemaphoreGive(preferencesMutex);
        ESP_LOGI(EMERGENCY_TAG,
                 "The broker has acknowledged the emergency report");
        return true;
    }

    // Keeps a state report that could not be sealed or queued, for
    // retryUnsentReport (utils/unsentReport.h). This happens when internal
    // RAM is short, so the copy lives in PSRAM where there is any, and
    // nothing here can throw.
    void holdUnsentReport(const String &report) {
        if (!unsentReport::holder.hasStorage()) {
            char *storage = static_cast<char *>(
                lockboxAllocPreferPsram(unsentReport::kCapacity));
            if (storage != nullptr &&
                !unsentReport::holder.setStorage(storage,
                                                 unsentReport::kCapacity)) {
                heap_caps_free(storage);  // another task was first
            }
        }
        const bool kept = unsentReport::holder.hold(
            report.c_str(), report.length(), millis());
        ESP_LOGW(MQTT_TAG, "A state report could not be sent; %s",
                 kept ? "it is kept and tried again"
                      : "it could not be kept either");
    }

    // Seals and sends the held report again. From the activity log task.
    void retryUnsentReport() {
        String report;
        uint32_t ticket = 0;
        if (!unsentReport::holder.read(report, ticket)) {
            // Nothing held any more, or no memory for the copy: the latter
            // counts as a try.
            unsentReport::holder.retried(ticket, false, millis());
            return;
        }
        const String topic = cloudTopic();
        const String sealed = sealCloudReport(
            topic.c_str(), lockbox::security::ReportKind::state, report);
        int result = -1;
        // A newer report may have been made while this one was sealed; it
        // must not be followed by the older one.
        if (!sealed.isEmpty() && unsentReport::holder.stillHeld(ticket)) {
            result = esp_mqtt_client_enqueue(client, topic.c_str(),
                                             sealed.c_str(), 0, 2, false, true);
        }
        unsentReport::holder.retried(ticket, result >= 0, millis());
        ESP_LOGW(MQTT_TAG, "Held state report %s",
                 result >= 0 ? "sent" : "still not sent");
    }

    void sendActivityLog(const String &message) {
        if (!client) return;
        if (!isTimeSynced) {
            ESP_LOGW(MQTT_TAG, "Time not synced yet, skipping activity log: %s",
                     message.c_str());
            return;
        }
        // Off the broker the client's outbox only grows (utils/logBacklog.h).
        // Connected, the outbox is not looked at.
        if (!mqttConnected &&
            !activityLogIsQueued(false,
                                 esp_mqtt_client_get_outbox_size(client))) {
            return;
        }

        String currentState = "";
        if (sm) {
            sm->visit_current_states(
                [&](auto state) { currentState = state.c_str(); });
        }
        const String topic = cloudTopic();
        const String msg = sealCloudReport(
            topic.c_str(), lockbox::security::ReportKind::log,
            createActivityLogMessage(message, currentState));
        if (msg.isEmpty()) return;

        const String logTopic = topic + Str::Protocol::LOG_TOPIC_SUFFIX;
        int result = esp_mqtt_client_enqueue(client, logTopic.c_str(), msg.c_str(), 0, 2,
                                             false, true);

        ESP_LOGD(MQTT_TAG, "Message enqueue result: %d", result);
    }

    void checkPendingStateChange() {
        checkOfferedLock();
        checkBreakCooldown();
        restoreUnlockTimer();
        restoreMenu();
        serviceEmergencyReport();
        if (logger.stateChangeMessagePending) {
            logger.stateChangeMessagePending = false;
            sendActivityLog(Str::Protocol::STATE_CHANGE);
        }
        // Cheap checks first: isLocked() reads NVS under the preferences
        // mutex. Attempts are only spent while connected, so the first retry
        // after a reconnect goes out right away.
        if (lockedReport::tracker.dueForResend(millis()) && mqttConnected) {
            if (isLocked()) resendLockedReport();
            else lockedReport::tracker.clear();  // no longer locked: moot
        }
        // Any other state report that could not be sealed or queued.
        if (mqttConnected && client && isTimeSynced &&
            unsentReport::holder.due(millis())) {
            retryUnsentReport();
        }
    }

    // Retry path for an unconfirmed LOCKED report. Re-enqueues the bytes the
    // state machine sent, so the server sees one event and rejects the
    // duplicates as already tracked. Builds the message only if the first
    // attempt never produced one (no client or clock at the time).
    void resendLockedReport() {
        if (!client || !isTimeSynced) {
            lockedReport::tracker.onRetry(-1, millis(), {});
            // A failed boot-time clock sync is otherwise only retried on the
            // next boot or reconnect, holding an accepted lock unreported.
            const bool retrying = !isTimeSynced && startClockRetry();
            ESP_LOGW(MQTT_TAG, "LOCKED report held: %s%s",
                     client ? "clock not verified" : "no MQTT client",
                     retrying ? ", retrying clock sync" : "");
            return;
        }
        std::string payload = lockedReport::tracker.payload();
        if (payload.empty()) {
            payload = createStateMessage(Str::Protocol::LOCKED, true, "").c_str();
        }
        const String topic = cloudTopic();
        const String sealed = sealCloudReport(
            topic.c_str(), lockbox::security::ReportKind::state, String(payload.c_str()));
        if (sealed.isEmpty()) {
            lockedReport::tracker.onRetry(-1, millis(), payload);
            return;
        }
        int result = esp_mqtt_client_enqueue(client, topic.c_str(),
                                             sealed.c_str(), 0, 2, false, true);
        lockedReport::tracker.onRetry(result, millis(), payload);
        ESP_LOGW(MQTT_TAG, "LOCKED report unconfirmed, resent (%u/%u), id %d",
                 static_cast<unsigned>(lockedReport::tracker.attemptCount()),
                 static_cast<unsigned>(lockedReport::kMaxAttempts), result);
    }

    // Starts one background clock sync at most every kClockRetryIntervalMs.
    // It shares the boot sync's task handle, so the two never run together.
    bool startClockRetry() {
        const uint32_t now = millis();
        if (TaskHandles::syncClock != nullptr ||
            (clockRetryStartedMs != 0 &&
             now - clockRetryStartedMs < lockedReport::kClockRetryIntervalMs))
            return false;
        clockRetryStartedMs = now;
        return xTaskCreatePinnedToCore(
                   retryClockSyncTask, Tasks::syncClock.name,
                   Tasks::syncClock.stackSize, nullptr,
                   Tasks::syncClock.priority, &TaskHandles::syncClock,
                   Tasks::syncClock.core) == pdPASS;
    }

    uint32_t clockRetryStartedMs = 0;
    // The emergency report owed from before a restart has been read. Only
    // the activity log task and the emergency unlock itself touch these.
    std::atomic<bool> emergencyReportLoaded{false};
    bool emergencyAckToRecord = false;
    // Since when a report has waited for the dashboard's state on a live
    // connection (0: it is not waiting), and whether the state was asked for.
    uint32_t emergencyWaitSinceMs = 0;
    bool emergencyStateAsked = false;
};

#endif  // LOCKBOX_LKBX_H
