#include "display.h"

#include "constants/LogTags.h"
#include "ui/animatedIcons.h"

long timeout = 0;

#ifdef VERSIONDEV
CapturedDisplay display(&SPI, Pins::Display::TFT_CS, Pins::Display::TFT_DC,
                        Pins::Display::TFT_RESET);
#else
Adafruit_ST7789 display(&SPI, Pins::Display::TFT_CS, Pins::Display::TFT_DC,
                       Pins::Display::TFT_RESET);
#endif

SemaphoreHandle_t displayMutex = nullptr;

void initDisplay() {
    displayMutex = xSemaphoreCreateMutex();
    if (displayMutex == nullptr) {
        ESP_LOGE(DISPLAY_TAG, "Failed to create display mutex");
    }

    SPI.begin(Pins::Display::TFT_SCLK, -1, Pins::Display::TFT_MOSI, -1);
    display.init(240, 320, SPI_MODE3);
    display.setRotation(3);
#ifdef VERSIONDEV
    display.initCapture();
#endif
    display.fillScreen(ST77XX_BLACK);

    // set up backlight
    pinMode(Pins::Display::TFT_BL, OUTPUT);
    // set the backlight to 100%
    ledcSetup(0, 5000, 8);
    ledcAttachPin(Pins::Display::TFT_BL, 0);
    ledcWrite(0, 255);

    // draw a gray rectangle at the top of the screen for status bar
    display.fillRect(0, 0, Display::WIDTH, Display::StatusbarHeight,
                     Colors::bgGray900);

    headerBarTask(display);
#ifdef VERSIONDEV
    startScreenCaptureConsole();
#endif
}
