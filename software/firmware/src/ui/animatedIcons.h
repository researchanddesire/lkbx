#ifndef LOCKBOX_ANIMATEDICONS_H
#define LOCKBOX_ANIMATEDICONS_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>  // Using a smaller font

#include "./Animation.h"
#include "./iconsSmall.h"
#include "./statusIconCanvas.h"
#include "WiFi.h"
#include "constants/Colors.h"
#include "constants/LogTags.h"
#include "constants/PerferenceKeys.h"
#include "constants/Pins.h"
#include "constants/strings.h"
#include "constants/Sizes.h"
#include "services/display.h"
#include "services/encoder.h"
#include "services/ina.h"
#include "services/lastInteraction.h"
#include "services/mqtt.h"
#include "services/settings.h"
#include "state/Guard.h"
#include "tasks/TaskParams.h"

class StateIcon {
  protected:
    int16_t x, y;
    int16_t width, height;
    uint16_t color;
    unsigned long lastDrawn = 0;

  public:
    bool showStatusDot = false;
    using Status = StatusIconDot;
    Status status = Status::Off;

    StateIcon(int16_t x, int16_t y)
        : x(x),
          y(y),
          width(Display::Icons::Small),
          height(Display::Icons::Small),
          color(Colors::white) {}

    void draw(Adafruit_ST7789* display) {
        if (!shouldDraw()) {
            return;
        }

        const unsigned char* frame = getFrame();
        if (frame == nullptr) {
            return;
        }

        StatusIconCanvas::compose(frame, color, status, showStatusDot);

        if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
            ui::fastBlit(*display, x, y, StatusIconCanvas::canvas.getBuffer(),
                         width, height);
            xSemaphoreGive(displayMutex);
        }

        lastDrawn = millis();
    }

    virtual bool shouldDraw() = 0;
    virtual const unsigned char* getFrame() = 0;
};

class WifiStateIcon : public StateIcon {
  private:
    bool pingOn = false;

  public:
    WifiStateIcon(int16_t x, int16_t y) : StateIcon(x, y) {
        showStatusDot = true;
    }

    bool shouldDraw() override { return millis() - lastDrawn > 1000; };

    const unsigned char* getFrame() override {
        bool weAreOnline = WiFi.status() == WL_CONNECTED;
        bool weAreConnectedToMQTT = mqttConnected;

        if (weAreConnectedToMQTT && weAreOnline) {
            this->status = Status::GreenFilled;
        } else if (!weAreConnectedToMQTT && weAreOnline) {
            if (pingOn) {
                this->status = Status::YellowCircle;
            } else {
                this->status = Status::YellowFilled;
            }

            pingOn = !pingOn;
        } else {
            this->status = Status::Off;
        }

        return WiFiClass::status() == WL_CONNECTED ? bitmap_wifi
                                                   : bitmap_wifi_off;
    }
};

class LockStateIcon : public StateIcon {
  private:
    bool firstRun = true;
    bool locked = false;
    bool lastState = false;

    bool pingOn = false;

  public:
    LockStateIcon(int16_t x, int16_t y) : StateIcon(x, y) {
        showStatusDot = true;
    }

    bool shouldDraw() override {
        if (millis() - lastDrawn <= 1000) {
            return false;
        }
        locked = isPaddleLocked();
        return true;
    };

    const unsigned char* getFrame() override {
        if (isBackplateClosed()) {
            this->status = Status::GreenFilled;
        } else {
            this->status = Status::YellowCircle;
        }

        return locked ? bitmap_locked : bitmap_unlocked;
    }
};

class BatteryStateIcon : public StateIcon {
  public:
    BatteryStateIcon(int16_t x, int16_t y) : StateIcon(x, y) {}

    bool shouldDraw() override { return millis() - lastDrawn > 1000; };

    const unsigned char* getFrame() override {
        color = Colors::white;

        if (digitalRead(Pins::Power::usbDetect) == HIGH) {
            color = Colors::green;
            return bitmap_battery_charging;
        }

#ifdef NO_INA219_INSTALLED
        color = Colors::red;
        return bitmap_battery_empty;
#endif
        if (!inaReady) {
            return bitmap_battery_full;
        }

        float busVoltage = ina.getBusVoltage_V();

        if (busVoltage > 3.9) {
            return bitmap_battery_full;
        }
        if (busVoltage > 3.65) {
            return bitmap_battery_mid;
        }
        if (busVoltage > 3.4) {
            return bitmap_battery_low;
        }

        color = Colors::red;
        return bitmap_battery_empty;
    }
};

namespace DebugOverlayCanvas {
constexpr int kWidth = Display::WIDTH / 2;
constexpr int kHeight = 10;

inline GFXcanvas1 canvas(kWidth, kHeight);
}  // namespace DebugOverlayCanvas

class DebugSensors {
  protected:
    unsigned long lastDrawn = 0;

    void blitDebugRow(Adafruit_ST7789* display, int16_t x, int16_t y) {
        if (xSemaphoreTake(displayMutex, 100) != pdTRUE) {
            return;
        }
        display->drawBitmap(x, y, DebugOverlayCanvas::canvas.getBuffer(),
                            DebugOverlayCanvas::kWidth,
                            DebugOverlayCanvas::kHeight, Colors::white,
                            Colors::black);
        xSemaphoreGive(displayMutex);
    }

  public:
    bool shouldDraw() {
        if (millis() - lastDrawn < 250) {
            return false;
        }
        return true;
    }

    void draw(Adafruit_ST7789* display) {
        if (!shouldDraw()) {
            return;
        }

        bool hall1 = digitalRead(Pins::Magnetics::hall1);
        bool hall2 = digitalRead(Pins::Magnetics::hall2);
        bool tmr1 = digitalRead(Pins::Magnetics::tmr1);
        bool tmr2 = digitalRead(Pins::Magnetics::tmr2);
        bool tmr3 = digitalRead(Pins::Magnetics::tmr3);

        DebugOverlayCanvas::canvas.fillScreen(0);
        DebugOverlayCanvas::canvas.setFont();
        DebugOverlayCanvas::canvas.setTextColor(1);
        DebugOverlayCanvas::canvas.setTextSize(1);
        DebugOverlayCanvas::canvas.setCursor(0, 0);
        DebugOverlayCanvas::canvas.print(Str::Debug::SENSOR_H1);
        DebugOverlayCanvas::canvas.print(hall1);
        DebugOverlayCanvas::canvas.print(Str::Debug::SENSOR_H2);
        DebugOverlayCanvas::canvas.print(hall2);
        DebugOverlayCanvas::canvas.print(Str::Debug::SENSOR_T1);
        DebugOverlayCanvas::canvas.print(tmr1);
        DebugOverlayCanvas::canvas.print(Str::Debug::SENSOR_T2);
        DebugOverlayCanvas::canvas.print(tmr2);
        DebugOverlayCanvas::canvas.print(Str::Debug::SENSOR_T3);
        DebugOverlayCanvas::canvas.print(tmr3);
        blitDebugRow(display, Display::WIDTH / 2, Display::HEIGHT - 10);

        float busVoltage = ina.getBusVoltage_V();
        float current_mA = ina.getCurrent_mA();
        auto value = rotaryEncoder.readEncoder();

        DebugOverlayCanvas::canvas.fillScreen(0);
        DebugOverlayCanvas::canvas.setFont();
        DebugOverlayCanvas::canvas.setTextColor(1);
        DebugOverlayCanvas::canvas.setTextSize(1);
        DebugOverlayCanvas::canvas.setCursor(0, 0);
        DebugOverlayCanvas::canvas.print(Str::Debug::ENCODER_E);
        DebugOverlayCanvas::canvas.print(value);
        DebugOverlayCanvas::canvas.print(Str::Debug::CURRENT_C);
        DebugOverlayCanvas::canvas.print(current_mA);
        DebugOverlayCanvas::canvas.print(Str::Debug::VOLTAGE_V);
        DebugOverlayCanvas::canvas.print(busVoltage);
        blitDebugRow(display, Display::WIDTH / 2, Display::HEIGHT - 10 * 2);

        int freeHeap = ESP.getFreeHeap();
        DebugOverlayCanvas::canvas.fillScreen(0);
        DebugOverlayCanvas::canvas.setFont();
        DebugOverlayCanvas::canvas.setTextColor(1);
        DebugOverlayCanvas::canvas.setTextSize(1);
        DebugOverlayCanvas::canvas.setCursor(0, 0);
        DebugOverlayCanvas::canvas.print(Str::Debug::FREE_HEAP);
        DebugOverlayCanvas::canvas.print(freeHeap);
        blitDebugRow(display, 0, Display::HEIGHT - 10);

        lastDrawn = millis();
    }
};

static void headerBarTask(Adafruit_ST7789& display) {
    auto task = [](void* pvParameters) {
        vTaskDelay(1000);
        auto* displayPtr = static_cast<Adafruit_ST7789*>(pvParameters);

        bool isAlpha = false;

        if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) ==
            pdTRUE) {
            preferences.begin(Keys::base);
            isAlpha = preferences.getBool(Keys::Base::isAlpha, false);
            preferences.end();
            xSemaphoreGive(preferencesMutex);

            ESP_LOGD("TOGGLE ALPHA", "isAlpha: %d", isAlpha);
        }

        if (isAlpha) {
            if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
                displayPtr->fillRect(Display::WIDTH / 2 - 24, 0, 48,
                                     Display::StatusbarHeight,
                                     Colors::bgGray900);
                displayPtr->setFont(&FreeSans9pt7b);
                displayPtr->setTextColor(Colors::white);
                displayPtr->setCursor(Display::WIDTH / 2 - 24,
                                      12 + Display::Padding::P1);
                displayPtr->print(Str::Debug::ALPHA_BADGE);
                xSemaphoreGive(displayMutex);
            }
        }

        BatteryStateIcon batteryIcon(getIconX(0), 3);
        WifiStateIcon wifiIcon(getIconX(1), 3);
        LockStateIcon lockIcon(getIconX(2), 3);
#ifdef DEBUG
        DebugSensors debugSensors;
#endif

        while (true) {
            if (idleState != IdleState::NOT_IDLE) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }

            wifiIcon.draw(displayPtr);
            batteryIcon.draw(displayPtr);
            lockIcon.draw(displayPtr);
#ifdef DEBUG
            debugSensors.draw(displayPtr);
#endif
            vTaskDelay(250 / portTICK_PERIOD_MS);
        }
    };

    ESP_LOGD(TASKDEBUG_TAG, "Creating header bar task");
    xTaskCreatePinnedToCore(task, Tasks::headerBar.name,
                            Tasks::headerBar.stackSize, &display,
                            Tasks::headerBar.priority, &TaskHandles::headerBar,
                            Tasks::headerBar.core);
}

#endif  // LOCKBOX_ANIMATEDICONS_H
