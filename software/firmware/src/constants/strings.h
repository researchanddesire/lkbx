#ifndef LKBX_STRINGS_H
#define LKBX_STRINGS_H

#include <Arduino.h>

namespace Str {

    // ── Action button labels
    // ────────────────────────────────────────────────────
    namespace Action {
        constexpr char CONTINUE[] PROGMEM = "Continue";
        constexpr char BACK[] PROGMEM = "Back";
        constexpr char SKIP[] PROGMEM = "Skip";
        constexpr char RETRY[] PROGMEM = "Retry";
        constexpr char NEXT[] PROGMEM = "Next";
        constexpr char SLEEP[] PROGMEM = "Sleep";
        constexpr char BREAK[] PROGMEM = "Break";
        constexpr char LOCK[] PROGMEM = "Lock";
        constexpr char UNLOCK[] PROGMEM = "Unlock";
        constexpr char CALIBRATE[] PROGMEM = "Calibrate";
        constexpr char RESTART[] PROGMEM = "Restart";
        constexpr char ENROL[] PROGMEM = "Enrol";
        constexpr char END_BREAK[] PROGMEM = "End Break";
        constexpr char ACCEPT[] PROGMEM = "Accept";
        constexpr char DECLINE[] PROGMEM = "Decline";
        constexpr char DONT_SHOW_AGAIN[] PROGMEM = "Don't show again";
        constexpr char WIFI_SETUP[] PROGMEM = "Wi-Fi Setup";
    }  // namespace Action

    // ── QR / Loading screen text
    // ────────────────────────────────────────────────
    namespace Screen {
        constexpr char EMPTY[] PROGMEM = "";

        // Preflight loading-animation status text (single spinner, dynamic
        // caption)
        constexpr char LOADING_STARTING[] PROGMEM = "Starting up";
        constexpr char LOADING_WIFI[] PROGMEM = "Connecting to the internet";
        constexpr char LOADING_UPDATES[] PROGMEM = "Checking for updates";
        constexpr char LOADING_DATE[] PROGMEM = "Getting the date";
        constexpr char LOADING_SESSION[] PROGMEM = "Setting up your session";
        constexpr char LOADING_PAIRING[] PROGMEM = "Checking your pairing";
        constexpr char LOADING_SYNC[] PROGMEM = "Almost ready";

        // Init / preflight pages (used by TextPages.h)
        constexpr char INIT_PAIRING_SUCCESS_HEADER[] PROGMEM =
            "Paired successfully!";
        constexpr char INIT_PAIRING_SUCCESS_CONTENT[] PROGMEM =
            "Your lockbox is now connected to your account.";

        constexpr char OTA_FAILED_HEADER[] PROGMEM = "Update Failed";
        constexpr char OTA_FAILED_CONTENT[] PROGMEM =
            "The firmware update could not be completed. The device will "
            "restart and try again.";

        constexpr char HELLO_ALPHA_VERSION_PREFIX[] PROGMEM = "Alpha Version ";
        constexpr char HELLO_VERSION_PREFIX[] PROGMEM = "Version ";
        constexpr char HELLO_TITLE_TEXT[] PROGMEM = "Chastity Lockbox";

        // QR screens (LKBX.h lambdas)
        constexpr char WIFI_SETUP_TITLE[] PROGMEM = "Wi-Fi Setup";
        constexpr char WIFI_SETUP_DESC[] PROGMEM =
            "Scan the QR code to join the 'Lockbox Setup' Wi-Fi network. "
            "Or connect manually and visit http://192.168.4.1.";
        constexpr char WIFI_SETUP_QR[] PROGMEM =
            "WIFI:S:Lockbox Setup;T:nopass;;";

        constexpr char WIFI_ERROR_TITLE[] PROGMEM = "Could Not Connect to WiFi";
        constexpr char WIFI_ERROR_DESC[] PROGMEM =
            "We couldn't connect to your saved network. You can try a "
            "different network or skip WiFi setup for now.";

        constexpr char WIFI_ERROR_NETWORK_TITLE[] PROGMEM =
            "E-LOCK-10: Network Unreachable";
        constexpr char WIFI_ERROR_NETWORK_DESC[] PROGMEM =
            "Your saved Wi-Fi network was not found or is out of range. Move "
            "closer to your router or choose a different network.";

        constexpr char WIFI_ERROR_AUTH_TITLE[] PROGMEM =
            "E-LOCK-10: Wi-Fi Login Failed";
        constexpr char WIFI_ERROR_AUTH_DESC[] PROGMEM =
            "The saved Wi-Fi password was rejected. Re-enter your network "
            "credentials or choose a different network.";

        constexpr char WIFI_ERROR_NO_IP_TITLE[] PROGMEM =
            "E-LOCK-10: No IP Address";
        constexpr char WIFI_ERROR_NO_IP_DESC[] PROGMEM =
            "The lockbox joined your Wi-Fi network but did not receive an IP "
            "address. Check your router or try again.";

        constexpr char WIFI_ERROR_SERVER_TITLE[] PROGMEM =
            "E-LOCK-12: Server Unreachable";
        constexpr char WIFI_ERROR_SERVER_DESC[] PROGMEM =
            "Wi-Fi is connected, but the dashboard health check failed. Check "
            "your internet connection or try again later.";

        constexpr char MQTT_ERROR_TITLE[] PROGMEM =
            "E-LOCK-11: Server Connection Failed";
        constexpr char MQTT_ERROR_DESC[] PROGMEM =
            "Could not connect to the messaging server within 30 seconds. You "
            "can retry or skip for now.";

        constexpr char AUTH_RATE_LIMITED_TITLE[] PROGMEM =
            "E-LOCK-13: Too Many Requests";
        constexpr char AUTH_RATE_LIMITED_DESC[] PROGMEM =
            "The dashboard is receiving too many requests from your network. "
            "Wait a minute, then retry, or skip for now.";

        constexpr char SIGN_IN_FAILED_TITLE[] PROGMEM =
            "E-LOCK-14: Dashboard Sign-In Failed";
        constexpr char SIGN_IN_FAILED_DESC[] PROGMEM =
            "The Lockbox could not finish signing in to the dashboard. Check "
            "your internet connection, then retry, or skip for now.";

        constexpr char DEVICE_KEY_FAILED_TITLE[] PROGMEM =
            "E-LOCK-999: Secure ID Not Saved";
        constexpr char DEVICE_KEY_FAILED_DESC[] PROGMEM =
            "The Lockbox could not store its secure ID. It still works and "
            "signs in as before. Press to continue, and contact support if "
            "this keeps appearing.";

        constexpr char WELCOME_TITLE[] PROGMEM = "Welcome to your new Lockbox!";
        constexpr char WELCOME_DESC[] PROGMEM =
            "Scan the QR code for setup instructions and documentation.";

        constexpr char SLEEP_TITLE[] PROGMEM = "Do you really want to Sleep?";
        constexpr char SLEEP_DESC[] PROGMEM =
            "Sleeping will save your battery, but you will lose your Wi-Fi "
            "connection.";

        constexpr char PERMALOCK_TITLE[] PROGMEM = "Permanently Lock Device";
        constexpr char PERMALOCK_DESC[] PROGMEM =
            "This will lock your device permanently. Are you sure?";

        constexpr char ACCEPT_LOCK_TITLE[] PROGMEM = "New Lock Session";

        constexpr char EUNLOCK_TITLE[] PROGMEM = "Emergency Unlock";
        constexpr char EUNLOCK_DESC[] PROGMEM =
            "This will stop your session immediately. You cannot use your "
            "device again for 24 hours.";

        constexpr char EBLOCKED_TITLE[] PROGMEM =
            "E-LOCK-20: Emergency Unlocked";
        constexpr char EBLOCKED_DESC[] PROGMEM =
            "This device cannot be used for 24 hours or until the Keyholder "
            "locks the device again.";

        constexpr char COULD_NOT_CONNECT_TITLE[] PROGMEM =
            "E-LOCK-10: Could Not Connect";
        constexpr char COULD_NOT_CONNECT_DESC[] PROGMEM =
            "We could not connect to the WiFi network. Please check your "
            "credentials.";

        constexpr char CALIBRATION_TITLE[] PROGMEM = "Calibrating";
        constexpr char CALIBRATION_DESC[] PROGMEM =
            "Let's calibrate your motor. Hold 'Enter' to continue. ";

        constexpr char NO_UPDATE_TITLE[] PROGMEM = "You're up to date!";
        constexpr char NO_UPDATE_DESC[] PROGMEM =
            "Press any button to return home, or scan the QR code see our "
            "update log.";

        constexpr char PAIRING_PREFIX[] PROGMEM = "Pairing: ";
        constexpr char PAIRING_DESC[] PROGMEM =
            "Scan the QR code to pair your device, or enter the code online.";

        constexpr char INIT_PAIRING_TITLE[] PROGMEM = "Pair Your Lockbox";
        constexpr char INIT_PAIRING_DESC_PREFIX[] PROGMEM =
            "Scan the QR code or enter code: ";

        constexpr char ENROL_ALPHA_TITLE[] PROGMEM = "Enrol in Alpha";
        constexpr char ENROL_ALPHA_DESC[] PROGMEM =
            "Get alpha updates before they are released to the public.";

        constexpr char CALIBRATING_MOTOR_TITLE[] PROGMEM = "Calibrating Motor";
        constexpr char CALIBRATING_MOTOR_DESC[] PROGMEM =
            "Please do not turn off the device, it will restart when the "
            "calibration finishes.";

        constexpr char UPDATING_TITLE[] PROGMEM = "Updating";
        constexpr char UPDATING_DESC[] PROGMEM =
            "Please do not turn off the device, it will restart when the "
            "update finishes.";

        // Break screens
        constexpr char ON_BREAK_TITLE[] PROGMEM = "On Break";
        constexpr char BREAK_LATE_PREFIX[] PROGMEM = "YOU ARE LATE! ";
        constexpr char BREAK_ENDS_PREFIX[] PROGMEM = "Break ends in: ";
        constexpr char BREAK_NOT_ENABLED_TITLE[] PROGMEM =
            "Breaks Not Enabled.";
        constexpr char BREAK_NOT_ENABLED_DESC[] PROGMEM =
            "How did you get here? Breaks are not enabled.";
        constexpr char BREAK_TOO_SOON_TITLE[] PROGMEM = "Too Soon for a Break";
        constexpr char BREAK_TOO_SOON_DESC_PRE[] PROGMEM =
            "You must wait at least ";
        constexpr char BREAK_TOO_SOON_DESC_POST[] PROGMEM =
            " before taking another break.";
        constexpr char BREAK_KEYHOLDER_ONLY_TITLE[] PROGMEM =
            "Keyholder Breaks Only";
        constexpr char BREAK_KEYHOLDER_ONLY_DESC[] PROGMEM =
            "Only your keyholder can start a break on this lock.";

        constexpr char BREAK_QR_TITLE[] PROGMEM = "Take A Break";
        constexpr char BREAK_QR_DESC[] PROGMEM =
            "Your session will pause immediately. You must lock the device "
            "before this period ends.";

        // Voting link of the session (Chaster Share links), from the lock menu
        constexpr char VOTE_TITLE[] PROGMEM = "Vote On This Lock";
        constexpr char VOTE_DESC[] PROGMEM =
            "Scan to add or remove time on this lock.";

        // Locked screen
        constexpr char TIME_HIDDEN[] PROGMEM = "Time Hidden";
        constexpr char WIFI_FOR_TIME[] PROGMEM =
            "Connect Wi-Fi to see time remaining";
        // Shown instead of the countdown while a Chaster freeze holds the
        // session. The optional reason is drawn beneath it.
        constexpr char FROZEN_BY_CHASTER[] PROGMEM = "Frozen by Chaster";

        // A Chaster session asks the dashboard before it opens for a break or
        // unlocks. These pages are shown while it asks and for each answer.
        constexpr char ASKING_CHASTER_TITLE[] PROGMEM = "Asking Chaster";
        constexpr char ASKING_CHASTER_DESC[] PROGMEM =
            "Checking with Chaster first. The box stays locked while it "
            "waits for the answer.";
        constexpr char UNLOCK_REFUSED_TITLE[] PROGMEM = "Waiting for Chaster";
        constexpr char UNLOCK_REFUSED_DEFAULT[] PROGMEM =
            "Chaster does not allow unlocking yet.";
        constexpr char UNLOCK_REFUSED_SUFFIX[] PROGMEM =
            " The box stays locked until Chaster allows it.";
        constexpr char BREAK_REFUSED_TITLE[] PROGMEM = "Break Refused";
        constexpr char BREAK_REFUSED_DEFAULT[] PROGMEM =
            "Chaster did not allow this break.";
        constexpr char BREAK_REFUSED_SUFFIX[] PROGMEM =
            " The box stays locked.";
        constexpr char CHASTER_NO_ANSWER_TITLE[] PROGMEM =
            "Can't Reach Chaster";
        constexpr char UNLOCK_NO_ANSWER_DESC[] PROGMEM =
            "Chaster decides when this lock opens, and the box could not "
            "ask. It stays locked. Check Wi-Fi, then press Unlock again.";
        constexpr char BREAK_NO_ANSWER_DESC[] PROGMEM =
            "Chaster decides breaks on this lock, and the box could not "
            "ask. It stays locked. Check Wi-Fi, then try again.";
        // Headings of the held-session page (see drawLockedActions).
        constexpr char HELD_FROZEN[] PROGMEM = "FROZEN";
        constexpr char HELD_FROZEN_BY[] PROGMEM = "by Chaster";
        constexpr char HELD_WAITING[] PROGMEM = "WAITING";
        constexpr char HELD_WAITING_FOR[] PROGMEM = "for Chaster";

        // Timer unit defaults
        constexpr char TIMER_DEFAULT[] PROGMEM = "10 minutes";

        // The Wi-Fi setup page when its setup network could not be started.
        constexpr char WIFI_SETUP_UNAVAILABLE_TITLE[] PROGMEM =
            "Wi-Fi Setup Not Open";
        constexpr char WIFI_SETUP_UNAVAILABLE_DESC[] PROGMEM =
            "Wi-Fi setup could not be opened just now. Leave this page and "
            "try again.";

        // The timer page could not be drawn, or the timer's end could not be
        // saved. The box does not lock.
        constexpr char TIMER_UNAVAILABLE_TITLE[] PROGMEM = "Timer Not Set";
        constexpr char TIMER_UNAVAILABLE_DESC[] PROGMEM =
            "The timer could not be set, so the box did not lock. Go back "
            "and try again.";
    }  // namespace Screen

    // ── QR label badges
    // ─────────────────────────────────────────────────────────
    namespace Label {
        constexpr char WIFI[] PROGMEM = "WIFI";
        constexpr char HELP[] PROGMEM = "HELP";
        constexpr char DOCS[] PROGMEM = "DOCS";
        constexpr char INFO[] PROGMEM = "INFO";
        constexpr char SCAN[] PROGMEM = "SCAN";
        constexpr char PAIR[] PROGMEM = "PAIR";
        constexpr char VOTE[] PROGMEM = "VOTE";
    }  // namespace Label

    // ── Menu item names
    // ─────────────────────────────────────────────────────────
    namespace Menu {
        constexpr char LOCK[] PROGMEM = "Lock";
        constexpr char LOCK_FOREVER[] PROGMEM = "Lock Forever";
        constexpr char SETTINGS[] PROGMEM = "Settings";
        constexpr char SLEEP[] PROGMEM = "Sleep";
        constexpr char GO_BACK[] PROGMEM = "Go Back";
        constexpr char WIFI_SETTINGS[] PROGMEM = "WiFi Settings";
        constexpr char TAKE_A_BREAK[] PROGMEM = "Take A Break";
        constexpr char EMERGENCY_UNLOCK[] PROGMEM = "Emergency Unlock";
        constexpr char RESTART_DEVICE[] PROGMEM = "Restart Device";
        constexpr char PAIRING[] PROGMEM = "Pairing";
        constexpr char UPDATE_DEVICE[] PROGMEM = "Update Device";
        constexpr char ENROL_IN_ALPHA[] PROGMEM = "Enrol in Alpha";
        constexpr char VOTE_LINK[] PROGMEM = "Vote Link";
        constexpr char MENU[] PROGMEM = "Menu";
    }  // namespace Menu

    // ── Time unit strings
    // ───────────────────────────────────────────────────────
    namespace Time {
        constexpr char NOW[] PROGMEM = "Now!";
        constexpr char SECOND[] PROGMEM = " second";
        constexpr char SECONDS_SUFFIX[] PROGMEM = "s";
        constexpr char SUFFIX_SECONDS[] PROGMEM = " seconds";
        constexpr char SUFFIX_MINUTES[] PROGMEM = " minutes";
        constexpr char SUFFIX_HOURS[] PROGMEM = " hours";
        constexpr char SUFFIX_DAYS[] PROGMEM = " days";
        constexpr char SUFFIX_WEEKS[] PROGMEM = " weeks";
        constexpr char SUFFIX_MONTHS[] PROGMEM = " months";
        constexpr char SUFFIX_YEARS[] PROGMEM = " years";
        constexpr char YEAR_ABBR[] PROGMEM = "y ";
        constexpr char DAY_ABBR[] PROGMEM = "d ";
        constexpr char HOUR_ABBR[] PROGMEM = "h ";
        constexpr char COLON[] PROGMEM = ":";
        constexpr char ZERO[] PROGMEM = "0";
    }  // namespace Time

    // ── MQTT / protocol message values
    // ──────────────────────────────────────────
    namespace Protocol {
        constexpr char DISCONNECTED[] PROGMEM = "disconnected";
        constexpr char LOCKED[] PROGMEM = "locked";
        constexpr char UNLOCKED[] PROGMEM = "unlocked";
        constexpr char COMPLETED[] PROGMEM = "completed";
        constexpr char BREAK[] PROGMEM = "break";
        constexpr char BREAK_ENDING[] PROGMEM = "break_ending";
        constexpr char EMERGENCY[] PROGMEM = "emergency";
        constexpr char BREAK_MISSED[] PROGMEM = "break_missed";
        constexpr char STATE_CHANGE[] PROGMEM = "state_change";
        constexpr char PENDING[] PROGMEM = "pending";
        constexpr char ABANDONED[] PROGMEM = "abandoned";
        constexpr char MQTT_CONNECTED[] PROGMEM = "MQTT Connected";
        constexpr char MQTT_DISCONNECTED[] PROGMEM = "MQTT Disconnected";
        constexpr char MQTT_SUBSCRIBED[] PROGMEM = "MQTT Subscribed";
        constexpr char LOG_TOPIC_SUFFIX[] PROGMEM = "/log";
    }  // namespace Protocol

    // ── JSON field keys (shared with server)
    // ────────────────────────────────────
    namespace Json {
        constexpr char ID[] PROGMEM = "id";
        constexpr char STATE[] PROGMEM = "state";
        constexpr char CREATED_AT[] PROGMEM = "createdAt";
        constexpr char IS_LOCKED[] PROGMEM = "isLocked";
        constexpr char IS_LOCK_TIMER[] PROGMEM = "isLockTimer";
        constexpr char IS_E_BLOCKED[] PROGMEM = "isEBlocked";
        constexpr char IS_LOCAL_PLAY[] PROGMEM = "isLocalPlay";
        constexpr char EMERGENCY_END[] PROGMEM = "emergencyEnd";
        constexpr char SESSION_ID[] PROGMEM = "sessionId";
        constexpr char LOCK_NAME[] PROGMEM = "lockName";
        constexpr char REQUESTED_BY[] PROGMEM = "requestedBy";
        constexpr char BREAK_LAST[] PROGMEM = "breakLast";
        constexpr char BREAK_START[] PROGMEM = "breakStart";
        constexpr char IS_ACTIVITY_LOG[] PROGMEM = "isActivityLog";
        constexpr char CURRENT_STATE[] PROGMEM = "currentState";
        constexpr char TMR1[] PROGMEM = "tmr1";
        constexpr char TMR2[] PROGMEM = "tmr2";
        constexpr char TMR3[] PROGMEM = "tmr3";
        constexpr char HALL1[] PROGMEM = "hall1";
        constexpr char HALL2[] PROGMEM = "hall2";
        constexpr char IS_CHARGING[] PROGMEM = "isCharging";
        constexpr char BATTERY_PERCENT[] PROGMEM = "batteryPercent";
        constexpr char IS_PADDLE_LOCKED[] PROGMEM = "isPaddleLocked";
        constexpr char IS_BACKPLATE_CLOSED[] PROGMEM = "isBackplateClosed";
        constexpr char MAC_ADDRESS[] PROGMEM = "macAddress";
        constexpr char MAC[] PROGMEM = "mac";
        constexpr char CHIP[] PROGMEM = "chip";
        constexpr char MD5[] PROGMEM = "md5";
        constexpr char DEVICE[] PROGMEM = "device";
        constexpr char OK[] PROGMEM = "ok";
        constexpr char DATA[] PROGMEM = "data";
        constexpr char CLEAR[] PROGMEM = "clear";
        constexpr char IS_BREAK[] PROGMEM = "isBreak";
        constexpr char LOCK_END[] PROGMEM = "lockEnd";
        constexpr char BREAK_MAX[] PROGMEM = "breakMax";
        constexpr char BREAK_REG[] PROGMEM = "breakReg";
        constexpr char BREAK_KEYHOLDER_ONLY[] PROGMEM = "breakKeyholderOnly";
        constexpr char DURATION[] PROGMEM = "duration";
        constexpr char IS_E_UNLOCK[] PROGMEM = "isEUnlock";
        constexpr char LOCK_START[] PROGMEM = "lockStart";
        constexpr char LOCK_STATE[] PROGMEM = "lockState";
        constexpr char IS_TEST_LOCK[] PROGMEM = "isTestLock";
        constexpr char IS_TIME_DISP[] PROGMEM = "isTimeDisp";
        constexpr char DISPLAY_ELAPSED_TIME[] PROGMEM = "displayElapsedTime";
        // Chaster freeze fields (HTTP state + MQTT sync)
        constexpr char IS_FROZEN[] PROGMEM = "isFrozen";
        constexpr char FROZEN_AT[] PROGMEM = "frozenAt";
        constexpr char FREEZE_REASON[] PROGMEM = "freezeReason";
        // True while Chaster controls the session (HTTP state + MQTT sync).
        constexpr char CHASTER_CONTROLLED[] PROGMEM = "chasterControlled";
        // The older name for "ask before opening"; either one makes the box ask.
        constexpr char REQUIRES_PERMISSION[] PROGMEM = "requiresPermission";
        // Voting link (HTTP state + MQTT sync + BLE session.apply)
        constexpr char VOTE_URL[] PROGMEM = "voteUrl";
        constexpr char VERSION_KEY[] PROGMEM = "version";
        constexpr char MAJOR[] PROGMEM = "major";
        constexpr char MINOR[] PROGMEM = "minor";
        constexpr char PATCH[] PROGMEM = "patch";
        constexpr char IS_PAIRED[] PROGMEM = "isPaired";
        constexpr char CLIENT_ID[] PROGMEM = "clientId";
        constexpr char USERNAME[] PROGMEM = "username";
        constexpr char PASSWORD[] PROGMEM = "password";
        constexpr char PAIRING_CODE[] PROGMEM = "pairingCode";
        constexpr char HUE_SHIFT[] PROGMEM = "hueShift";
        constexpr char DEVICE_SECRET[] PROGMEM = "deviceSecret";
        constexpr char COMMAND_KEY[] PROGMEM = "commandKey";
        constexpr char DEVICE_TOKEN[] PROGMEM = "deviceToken";
        constexpr char FAILURE_CODE[] PROGMEM = "failureCode";
        // Message parsing
        constexpr char TYPE[] PROGMEM = "type";
        constexpr char MESSAGE[] PROGMEM = "message";
        constexpr char AT[] PROGMEM = "at";
    }  // namespace Json

    // ── Message type discriminators
    // ─────────────────────────────────────────────
    namespace MsgType {
        constexpr char EVENT[] PROGMEM = "event";
        constexpr char STATE[] PROGMEM = "state";
        constexpr char SYNC[] PROGMEM = "sync";
        constexpr char REBOOT[] PROGMEM = "reboot";
    }  // namespace MsgType

    // ── API paths and URL fragments
    // ─────────────────────────────────────────────
    namespace Api {
        constexpr char LOCKBOX_STATE[] PROGMEM = "/api/lockbox/state/";
        constexpr char LOCKBOX_IS_PAIRED[] PROGMEM = "/api/lockbox/is-paired";
        // v2 routes identify the device by its eFuse token, never its MAC.
        constexpr char LOCKBOX_V2_PROVISION[] PROGMEM = "/api/lockbox/v2/provision";
        constexpr char LOCKBOX_V2_AUTH[] PROGMEM = "/api/lockbox/v2/auth";
        constexpr char LOCKBOX_V2_IS_PAIRED[] PROGMEM = "/api/lockbox/v2/is-paired";
        constexpr char LOCKBOX_V2_STATE[] PROGMEM = "/api/lockbox/v2/state";
        // A Chaster session asks here before it opens for a break or unlocks.
        constexpr char LOCKBOX_ASK[] PROGMEM = "/api/lockbox/ask";
        constexpr char LOCKBOX_V2_ASK[] PROGMEM = "/api/lockbox/v2/ask";
        constexpr char HEALTH_CHECK[] PROGMEM = "/api/health-check";
        constexpr char VERSION_JSON[] PROGMEM = "version.json";
        constexpr char FIRMWARE_BIN[] PROGMEM = "firmware.bin";
        constexpr char DEVICE_NAME[] PROGMEM = "LOCKBOX-v0";
    }  // namespace Api

    // ── Documentation / Dashboard URLs
    // ──────────────────────────────────────────
    namespace Url {
        constexpr char DOCS_WIFI_SETUP[] PROGMEM =
            "https://docs.researchanddesire.com/lkbx/quick-start/wifi-setup";
        constexpr char DOCS_MQTT_ERROR[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-11-mqtt-connection-timeout";
        constexpr char DOCS_QUICK_START[] PROGMEM =
            "https://docs.researchanddesire.com/lkbx/quick-start";
        constexpr char DOCS_SLEEP_MODE[] PROGMEM =
            "https://docs.researchanddesire.com/sleep-mode";
        constexpr char DOCS_ERROR_LOCK[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-1-could-not-lock";
        constexpr char DOCS_ERROR_UNLOCK[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-2-could-not-unlock";
        constexpr char DOCS_ERROR_BACKPLATE[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-3-no-backplate-detected";
        constexpr char DOCS_TAKE_A_BREAK[] PROGMEM =
            "https://docs.researchanddesire.com/take-a-break";
        constexpr char DOCS_PERMALOCK[] PROGMEM =
            "https://docs.researchanddesire.com/permalock-device";
        constexpr char DOCS_EMERGENCY_UNLOCK[] PROGMEM =
            "https://docs.researchanddesire.com/emergency-unlock";
        constexpr char DOCS_EBLOCK_ERROR[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-20-emergency-unlock-used";
        constexpr char DOCS_WIFI_ERROR[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-10-could-not-connect-to-wi-fi";
        constexpr char DOCS_SERVER_HEALTH_ERROR[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-12-server-unreachable";
        constexpr char DOCS_RATE_LIMITED_ERROR[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-13-too-many-requests";
        constexpr char DOCS_SIGN_IN_FAILED_ERROR[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-14-dashboard-sign-in-failed";
        constexpr char DOCS_DEVICE_KEY_ERROR[] PROGMEM =
            "https://docs.researchanddesire.com/common-errors/"
            "e-lock-999-secure-id-not-saved";
        constexpr char DOCS_CALIBRATION[] PROGMEM =
            "https://docs.researchanddesire.com/calibration";
        constexpr char DOCS_UPDATES[] PROGMEM =
            "https://docs.researchanddesire.com/updates";
        constexpr char DOCS_ENROL_ALPHA[] PROGMEM =
            "https://docs.researchanddesire.com/enrol-in-alpha";
    }  // namespace Url

    // ── Error screen text
    // ───────────────────────────────────────────────────────
    namespace Error {
        constexpr char LOCK_TITLE[] PROGMEM = "E-LOCK-1: Could Not Lock";
        constexpr char LOCK_DESC[] PROGMEM =
            "We could not confirm that your device is locked. Is the paddle "
            "blocked?";
        constexpr char UNLOCK_TITLE[] PROGMEM = "E-LOCK-2: Could Not Unlock";
        constexpr char UNLOCK_DESC[] PROGMEM =
            "We could not confirm that your device is unlocked. Is the paddle "
            "blocked?";
        constexpr char BACKPLATE_TITLE[] PROGMEM =
            "E-LOCK-3: No Backplate Detected";
        constexpr char BACKPLATE_DESC[] PROGMEM =
            "We could not confirm that the backplate is closed.";
    }  // namespace Error

    // ── BLE identifiers
    // ─────────────────────────────────────────────────────────
    namespace Ble {
        constexpr char SERVICE_UUID[] PROGMEM =
            "522b443a-4c4b-4258-0001-420badbabe69";
        constexpr char CHAR_UUID[] PROGMEM =
            "522b443a-4c4b-4258-0010-420badbabe69";
        constexpr char DEVICE_NAME[] PROGMEM = "LKBX";
    }  // namespace Ble

    // ── WiFi / service names
    // ────────────────────────────────────────────────────
    namespace Wifi {
        constexpr char PORTAL_SSID[] PROGMEM = "Lockbox Setup";
        constexpr char WM_CLASS[] PROGMEM = "invert";
        constexpr char CONTENT_TYPE_KEY[] PROGMEM = "Content-Type";
        constexpr char CONTENT_TYPE_JSON[] PROGMEM = "application/json";
        constexpr char HTTPS_PREFIX[] PROGMEM = "https://";
    }  // namespace Wifi

    // ── Input event labels (for setNotIdle)
    // ─────────────────────────────────────
    namespace Input {
        constexpr char ENTER_CLICK[] PROGMEM = "buttonEnter, click";
        constexpr char ENTER_DOUBLE[] PROGMEM = "buttonEnter, double click";
        constexpr char ENTER_LONG[] PROGMEM = "buttonEnter, long press";
        constexpr char BACK_CLICK[] PROGMEM = "buttonBack, click";
        constexpr char BACK_DOUBLE[] PROGMEM = "buttonBack, double click";
        constexpr char BACK_LONG[] PROGMEM = "buttonBack, long press";
        constexpr char NIMBLE_WRITE[] PROGMEM = "NimBLE onWrite";
        constexpr char MQTT_DATA[] PROGMEM = "MQTT_EVENT_DATA";
        constexpr char STATE_MACHINE[] PROGMEM = "stateMachine";
        constexpr char UNLOCK_TIMER[] PROGMEM = "UnlockTimer";
    }  // namespace Input

    // ── Debug / diagnostic labels
    // ───────────────────────────────────────────────
    namespace Debug {
        constexpr char SENSOR_H1[] PROGMEM = "H1:";
        constexpr char SENSOR_H2[] PROGMEM = " H2:";
        constexpr char SENSOR_T1[] PROGMEM = " T1:";
        constexpr char SENSOR_T2[] PROGMEM = " T2:";
        constexpr char SENSOR_T3[] PROGMEM = " T3:";
        constexpr char ENCODER_E[] PROGMEM = "E:";
        constexpr char CURRENT_C[] PROGMEM = " C:";
        constexpr char VOLTAGE_V[] PROGMEM = " V:";
        constexpr char FREE_HEAP[] PROGMEM = "Free Heap: ";
        constexpr char ALPHA_BADGE[] PROGMEM = "alpha";
        constexpr char GUARD_PASS[] PROGMEM = "[PASS]";
        constexpr char GUARD_FAIL[] PROGMEM = "[DO NOT PASS]";
        constexpr char ALPHANUMERIC_CHARS[] PROGMEM =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        constexpr char ERROR_FALLBACK[] PROGMEM = "ERROR";
        constexpr char NULL_STR[] PROGMEM = "null";
        constexpr char NULLPTR_STR[] PROGMEM = "nullptr";
        constexpr char UNDEFINED_STR[] PROGMEM = "undefined";
        constexpr char INVERTED[] PROGMEM = "inverted";
        constexpr char NOT_INVERTED[] PROGMEM = "not inverted";
        constexpr char CALIBRATED[] PROGMEM = "calibrated";
        constexpr char NOT_CALIBRATED[] PROGMEM = "not calibrated";
        constexpr char DESERIALIZE_FAILED[] PROGMEM =
            "deserializeJson() failed: ";
        constexpr char SML_PREFIX[] PROGMEM = "boost::ext::sml";
    }  // namespace Debug

    // ── NTP servers
    // ─────────────────────────────────────────────────────────────
    namespace Ntp {
        constexpr char POOL[] PROGMEM = "pool.ntp.org";
        constexpr char FALLBACK[] PROGMEM = "time.nist.gov";
    }  // namespace Ntp

    // ── HTTP method labels
    // ──────────────────────────────────────────────────────
    namespace Http {
        constexpr char POST[] PROGMEM = "POST";
        constexpr char GET[] PROGMEM = "GET";
    }  // namespace Http

    // ── UI overlay labels
    // ───────────────────────────────────────────────────────
    namespace Overlay {
        constexpr char LOCAL_PLAY[] PROGMEM = "local";
    }  // namespace Overlay

}  // namespace Str

#endif  // LKBX_STRINGS_H
