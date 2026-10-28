#include "sleep.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "constants/Pins.h"
#include "display.h"
#include "ina.h"
#include "lastInteraction.h"
#include "motor/Motor.h"
#include "utils/idleTarget.h"

bool sleepPowerConnected() {
    return digitalRead(Pins::Power::usbDetect) == HIGH || HWCDC::isPlugged();
}

void initSerialWake() {
    // These callbacks run on the Arduino event task. Do not consume RX bytes:
    // the screen console and other serial clients still need their commands.
    Serial.onEvent([](void*, esp_event_base_t, int32_t event, void*) {
        if (event == ARDUINO_HW_CDC_RX_EVENT ||
            event == ARDUINO_HW_CDC_BUS_RESET_EVENT ||
            event == ARDUINO_HW_CDC_CONNECTED_EVENT) {
            setNotIdle("serial");
        }
    });
}

void enterSleep(uint64_t wakeDelayMs,
                const std::function<void()>& beforeDeepSleep) {
    const auto interaction = lastInteraction;
    auto pseudoSleep = []() {
        // Keep the idle task at pseudo sleep until the next real interaction.
        // On battery it may still progress to deep sleep after the normal delay.
        lastInteraction = millis() - PSEUDO_SLEEP_TIMEOUT;
        idleState = IdleState::PSEUDO_SLEEP;
    };
    if (!deepSleepAllowed(sleepPowerConnected(), wakeDelayMs)) {
        pseudoSleep();
        return;
    }

    const int button = Pins::Buttons::buttonEnter <= 21
                           ? Pins::Buttons::buttonEnter
                           : Pins::Buttons::buttonBack;
    // EXT0 retains the active-low button; EXT1 independently wakes on VBUS.
    // USB itself is powered off in deep sleep, so cable insertion must wake
    // through the RTC-capable power-sense pin instead.
    if (esp_sleep_enable_ext0_wakeup(gpio_num_t(button), 0) != ESP_OK ||
        esp_sleep_enable_ext1_wakeup(1ULL << Pins::Power::usbDetect,
                                    ESP_EXT1_WAKEUP_ANY_HIGH) != ESP_OK ||
        (wakeDelayMs > 0 &&
         esp_sleep_enable_timer_wakeup(wakeDelayMs * 1000ULL) != ESP_OK)) {
        pseudoSleep();
        return;
    }

    if (beforeDeepSleep) beforeDeepSleep();
    // Power or serial activity can arrive while preparing sleep or publishing.
    if (lastInteraction != interaction) return;
    if (sleepPowerConnected()) {
        pseudoSleep();
        return;
    }

    ledcWrite(0, 0);
    Motor::clear();
    ina.powerSave(true);
    display.writeCommand(ST77XX_SLPIN);

    // Leave the panel reset deasserted until the final check, allowing a late
    // serial/power wake to resume the display without reinitializing it.
    if (sleepPowerConnected() || lastInteraction != interaction) {
        display.writeCommand(ST77XX_SLPOUT);
        delay(120);
        ina.powerSave(false);
        ledcWrite(0, 255);
        setNotIdle("sleep cancelled");
        return;
    }
    pinMode(Pins::Display::TFT_RESET, OUTPUT);
    digitalWrite(Pins::Display::TFT_RESET, LOW);
    esp_deep_sleep_start();
}
