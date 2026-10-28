#include "LKBX.h"

#include <esp_heap_caps.h>
#include <new>

#include "LockboxDuration.h"

#include "Fonts/FreeSans9pt7b.h"
#include "ui/Animation.h"
#include "utils/clearDisplay.h"
#include "utils/memory.h"

void drawArc(GFXcanvas16 *canvas, int rawSteps, int value, int min, int max) {
    const int RADIUS = 64;
    const int INNER_RADIUS = 54;
    const int CENTER_X = 64;
    const int CENTER_Y = 64;

    float percent = map(value, min, max, 0, 100);

    // Set the max steps to 90 and min steps to 2
    int steps = constrain(rawSteps, 2, 45);

    int activeSteps = round(steps * (percent / 100.0));

    for (int i = 0; i < steps; i++) {
        // Start from PI (left side) and go to 0 (right side)
        float angle = PI - (i * (180.0 / (steps - 1)) * (PI / 180.0));
        int x1 = CENTER_X + cos(angle) * INNER_RADIUS;
        int y1 = CENTER_Y - sin(angle) * INNER_RADIUS;
        int x2 = CENTER_X + cos(angle) * RADIUS;
        int y2 = CENTER_Y - sin(angle) * RADIUS;

        uint16_t color = (i <= activeSteps) ? ST77XX_GREEN : Colors::bgGray900;
        canvas->drawLine(x1, y1, x2, y2, color);
    }
}

const int offsetMinutes = 1;
const int offsetHours = 2;
const int offsetDays = 1;
const int offsetWeeks = 4;
const int offsetMonths = 2;
const int offsetYears = 1;

const int maxSeconds = 120;
const int maxMinutes = maxSeconds + 120 - offsetMinutes;
const int maxHours = maxMinutes + 24 - offsetHours;
const int maxDays = maxHours + 30 - offsetDays;
const int maxWeeks = maxDays + 8 - offsetWeeks;
const int maxMonths = maxWeeks + 12 - offsetMonths;
const int maxYears = maxMonths + 1000 - offsetYears;
GFXcanvas16 *timerCanvas = nullptr;

void drawTimerConfigTask(void *pvParameters) {
    auto *lkbx = static_cast<LKBX *>(pvParameters);

    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        clearScreen(lkbx->display);
        xSemaphoreGive(displayMutex);
    }

    auto isInCorrectState = [](LKBX *lkbx) { return lkbx->sm->is("timer"_s); };

    bool isFirstRun = true;
    int lastEncoderValue = 0;

    // 10 minutes, written in ms.
    uint64_t duration = 600000;
    char durationString[24];
    strncpy(durationString, Str::Screen::TIMER_DEFAULT, sizeof(durationString));

    // Wait for a contiguous block large enough for the 128x128 canvas buffer
    // (128 * 128 * 2 = 32768 bytes) plus object overhead.
    constexpr size_t canvasBytes = 128 * 128 * sizeof(uint16_t) + 256;
    while (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < canvasBytes &&
           isInCorrectState(lkbx)) {
        printMemoryDiagnostics("drawTimerConfigTask");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    if (isInCorrectState(lkbx)) {
        timerCanvas = new (std::nothrow) GFXcanvas16(128, 128);
    }

    if (timerCanvas == nullptr || timerCanvas->getBuffer() == nullptr) {
        ESP_LOGE(TIMER_TAG, "Failed to allocate timer canvas");
        delete timerCanvas;
        timerCanvas = nullptr;
        TaskHandles::timerConfig = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    while (isInCorrectState(lkbx)) {
        // If we're sleeping, don't draw anything (poll often for encoder wake).
        if (idleState != IdleState::NOT_IDLE) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        int encoderValue =
            constrain(maxYears - lkbx->encoder.readEncoder(), 30, maxYears);

        if (!isFirstRun && encoderValue == lastEncoderValue) {
            vTaskDelay(250 / portTICK_PERIOD_MS);
            continue;
        }

        timerCanvas->fillScreen(ST77XX_BLACK);

        // the first 120 ticks are in units of seconds,
        // the next 120 ticks are in units of minutes,
        // then next 22 ticks are in units of hours,
        // the next 30 ticks are in units of days.
        // the next 52 ticks are in units of weeks.
        // the next 12 ticks are in units of months.
        // all the remaining ticks are in units of years.
        using lockbox::security::DurationUnit;
        using lockbox::security::durationMilliseconds;
        bool validDuration = false;
        if (encoderValue < maxSeconds) {
            validDuration = durationMilliseconds(encoderValue, DurationUnit::Seconds, duration);
            snprintf(durationString, sizeof(durationString), "%d%s", encoderValue, Str::Time::SUFFIX_SECONDS);

            drawArc(timerCanvas, maxSeconds, encoderValue, 30, maxSeconds);
        } else if (encoderValue < maxMinutes) {
            validDuration = durationMilliseconds(encoderValue - maxSeconds + offsetMinutes,
                                                 DurationUnit::Minutes, duration);
            snprintf(durationString, sizeof(durationString), "%d%s",
                     encoderValue - maxSeconds + offsetMinutes, Str::Time::SUFFIX_MINUTES);
            drawArc(timerCanvas, maxMinutes - maxSeconds, encoderValue,
                    maxSeconds, maxMinutes);
        } else if (encoderValue < maxHours) {
            validDuration = durationMilliseconds(encoderValue - maxMinutes + offsetHours,
                                                 DurationUnit::Hours, duration);
            snprintf(durationString, sizeof(durationString), "%d%s",
                     encoderValue - maxMinutes + offsetHours, Str::Time::SUFFIX_HOURS);
            drawArc(timerCanvas, maxHours - maxMinutes, encoderValue,
                    maxMinutes, maxHours);
        } else if (encoderValue < maxDays) {
            validDuration = durationMilliseconds(encoderValue - maxHours + offsetDays,
                                                 DurationUnit::Days, duration);
            snprintf(durationString, sizeof(durationString), "%d%s",
                     encoderValue - maxHours + offsetDays, Str::Time::SUFFIX_DAYS);
            drawArc(timerCanvas, maxDays - maxHours, encoderValue, maxHours,
                    maxDays);
        } else if (encoderValue < maxWeeks) {
            validDuration = durationMilliseconds(encoderValue - maxDays + offsetWeeks,
                                                 DurationUnit::Weeks, duration);
            snprintf(durationString, sizeof(durationString), "%d%s",
                     encoderValue - maxDays + offsetWeeks, Str::Time::SUFFIX_WEEKS);
            drawArc(timerCanvas, maxWeeks - maxDays, encoderValue, maxDays,
                    maxWeeks);
        } else if (encoderValue < maxMonths) {
            validDuration = durationMilliseconds(encoderValue - maxWeeks + offsetMonths,
                                                 DurationUnit::Months, duration);
            snprintf(durationString, sizeof(durationString), "%d%s",
                     encoderValue - maxWeeks + offsetMonths, Str::Time::SUFFIX_MONTHS);
            drawArc(timerCanvas, maxMonths - maxWeeks, encoderValue, maxWeeks,
                    maxMonths);
        } else {
            validDuration = durationMilliseconds(encoderValue - maxMonths + offsetYears,
                                                 DurationUnit::Years, duration);
            snprintf(durationString, sizeof(durationString), "%d%s",
                     encoderValue - maxMonths + offsetYears, Str::Time::SUFFIX_YEARS);
            drawArc(timerCanvas, maxYears - maxMonths, encoderValue, maxMonths,
                    maxYears);
        }

        if (!validDuration) {
            ESP_LOGW(TIMER_TAG, "Rejected invalid timer duration");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        int16_t x1, y1;
        uint16_t w, h;

        timerCanvas->setFont(&FreeSans9pt7b);
        timerCanvas->getTextBounds(durationString, 0, 0, &x1, &y1, &w, &h);

        int x = (128 - w) / 2;

        timerCanvas->setCursor(x, 110);
        timerCanvas->setTextColor(ST77XX_WHITE);
        timerCanvas->println(durationString);

        if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
            ui::fastBlit(lkbx->display, Display::WIDTH / 2 - 64,
                         Display::HEIGHT / 2 - 64 + Display::StatusbarHeight,
                         timerCanvas->getBuffer(), 128, 128);
            xSemaphoreGive(displayMutex);
        }

        lkbx->duration = duration;

        lastEncoderValue = encoderValue;
        isFirstRun = false;

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    delete timerCanvas;
    timerCanvas = nullptr;

    TaskHandles::timerConfig = nullptr;
    vTaskDelete(nullptr);
}

void LKBX::drawTimerConfig() {
    if (TaskHandles::timerConfig != nullptr) {
        ESP_LOGW(TIMER_TAG, "Timer config task already running, skipping");
        return;
    }

    encoder.setAcceleration(100);
    encoder.setBoundaries(30, maxYears, false);
    encoder.setEncoderValue(maxYears - 120);

    ESP_LOGD(TASKDEBUG_TAG, "Creating timer config task");
    xTaskCreatePinnedToCore(drawTimerConfigTask, Tasks::timerConfig.name,
                            Tasks::timerConfig.stackSize, this,
                            Tasks::timerConfig.priority,
                            &TaskHandles::timerConfig, Tasks::timerConfig.core);
}
