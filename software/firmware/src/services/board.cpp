#include "services/serialIdentity.h"
#include "board.h"

#include <Arduino.h>

#include "../constants/Pins.h"
#include "../tasks/TaskParams.h"
#include "display.h"
#include "driver/rtc_io.h"
#include "encoder.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "http_worker.h"
#include "hardwareHealth.h"
#include "ina.h"
#include "lastInteraction.h"
#include "magnetics.h"
#include "nimble.h"
#include "services/helpers/initMQTT.h"
#include "settings.h"
#include "sleep.h"
#include "state/LKBX.h"
#include "structs/Version.h"
#include "utils/psram.h"
#include "wm.h"

void initPinConfig() {
    bool switchPins = false;

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        switchPins = preferences.getBool(Keys::Base::isButtonInverted, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    if (switchPins) {
        Pins::MOTOR::A = 12;
        Pins::Buttons::buttonEnter = 46;
    }
}

void initMotorGPIO() {
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);
    delay(50);

    pinMode(Pins::MOTOR::A, OUTPUT);
    pinMode(38, OUTPUT);
    pinMode(Pins::MOTOR::B, OUTPUT);
    pinMode(3, OUTPUT);
}

void initPowerDetect() {
    rtc_gpio_deinit(gpio_num_t(Pins::Power::usbDetect));
    pinMode(Pins::Power::usbDetect, INPUT);

    attachInterrupt(
        digitalPinToInterrupt(Pins::Power::usbDetect),
        []() { setNotIdleISR(); }, CHANGE);
}

void initServices() {
    initNimble(lkbx);
    ESP_LOGD(TASKDEBUG_TAG, "Creating nimble task");
    xTaskCreatePinnedToCore(
        nimbleTask, Tasks::nimble.name, Tasks::nimble.stackSize, lkbx,
        Tasks::nimble.priority, &TaskHandles::nimble, Tasks::nimble.core);

    initWM();
    initHttpWorker();
}

void initButtonGPIO() {
    rtc_gpio_deinit(gpio_num_t(Pins::Buttons::buttonBack));
    rtc_gpio_deinit(gpio_num_t(Pins::Buttons::buttonEnter));
}

void initBoard() {
#if (defined(RAD_HIL_VARIANT) || defined(VERSIONDEV)) && ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
    // Reserve room for a complete health frame plus simultaneous task logs
    // before HWCDC starts its interrupt handler. The 256-byte default can
    // exhaust its queue during a burst and drop part of a record.
#ifdef VERSIONDEV
    constexpr size_t txBytes = 4096; // A full worst-case RLE screen row plus logs.
#else
    constexpr size_t txBytes = 2048;
#endif
    if (Serial.setTxBufferSize(txBytes) != txBytes) abort();
    Serial.setTxTimeoutMs(1000);
#endif
    configureSerialIdentityUsb();
    Serial.begin(115200);
    startSerialIdentity();
    initSerialWake();
    delay(500);
    hardwareHealthBoot();

    initLockboxPsram();

    initVersionString();
    initPinConfig();
    initDisplay();
    initMotorGPIO();
    initPowerDetect();
    initMagnetics();
    initEncoder();
    initINA();
    initServices();
    initButtonGPIO();
}
