#include "Arduino.h"
#include "constants/LogTags.h"
#include "esp_log.h"
#include "motor/Motor.h"
#include "services/activityLog.h"
#include "services/board.h"
#include "services/radHil.h"
#include "services/buttons.h"
#include "services/display.h"
#include "services/encoder.h"
#include "services/idle.h"
#include "services/ina.h"
#include "services/machine.h"
#include "services/mqtt.h"
#include "state/LKBX.h"
#include "utils/memory.h"

Motor *motor = nullptr;

#ifdef CONFIG_APP_ROLLBACK_ENABLE
// Arduino otherwise accepts a pending image before setup(). Defer acceptance
// until the application has passed the server health check.
extern "C" bool verifyRollbackLater() { return true; }
#endif

static void print_wakeup_reason() {
    esp_sleep_wakeup_cause_t wakeup_reason;

    wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGD(WAKEUP_TAG, "Wakeup caused by external signal using RTC_IO");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            ESP_LOGD(WAKEUP_TAG,
                     "Wakeup caused by external signal using RTC_CNTL");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGD(WAKEUP_TAG, "Wakeup caused by timer");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            ESP_LOGD(WAKEUP_TAG, "Wakeup caused by touchpad");
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            ESP_LOGD(WAKEUP_TAG, "Wakeup caused by ULP program");
            break;
        default:
            ESP_LOGD(WAKEUP_TAG, "Wakeup was not caused by deep sleep: %d",
                     wakeup_reason);
            break;
    }
}

// Weak linkage: test builds override these with their own setup()/loop().
// PIO_UNIT_TESTING is unreliable with the arduino+espidf combined framework.
__attribute__((weak)) void setup() {
    initBoard();
    radHilStart();

    print_wakeup_reason();

    motor = new Motor();
    motor->ina = &ina;
    delay(500);

    lkbx = new LKBX(display, rotaryEncoder, *motor, mqttClient);
    lkbx->initDelayTask();

    initButtons();
    initIdleTask();
    initActivityLogTask();

#ifdef MEMORY_LOGGING
    initStackMonitor();
#endif

    ESP_LOGD(MAIN_TAG, "Setup complete");
};

__attribute__((weak)) void loop() { vTaskDelete(nullptr); };
