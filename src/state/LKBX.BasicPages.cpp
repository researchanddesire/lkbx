#include "LKBX.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/Tiny3x3a2pt7b.h>
#include <pages/TextPages.h>

#include "qrcode.h"
#include "structs/Action.h"
#include "ui/Animation.h"
#include "ui/bigIcons.h"
#include "ui/loadingSpinner.h"
#include "utils/clearDisplay.h"
#include "utils/psram.h"

#include <esp_heap_caps.h>

#include <cstring>

namespace {

struct LoaderContext {
    LKBX* lkbx;
};

bool isLoadingScreenState(void* context) {
    auto* ctx = static_cast<LoaderContext*>(context);
    return ctx->lkbx->sm->is("update"_s) ||
           ctx->lkbx->sm->is("calibrate.motor"_s);
}

void loadingSpinnerTask(void* pvParameters) {
    auto* lkbx = static_cast<LKBX*>(pvParameters);
    LoaderContext ctx{.lkbx = lkbx};

    const int bitmapX = (Display::WIDTH - loading_spinner_width) / 2;
    const int bitmapY = (Display::HEIGHT - loading_spinner_height) / 2;
    const TickType_t frameTicks =
        pdMS_TO_TICKS(loading_spinner_loop_period_ms) /
        loading_spinner_frame_count;
    TickType_t wakeTime = xTaskGetTickCount();

    const size_t scratchBytes =
        static_cast<size_t>(loading_spinner_pixels) * sizeof(uint16_t);
    uint16_t* scratch =
        static_cast<uint16_t*>(lockboxAllocPreferPsram(scratchBytes));
    if (scratch == nullptr) {
        TaskHandles::loader = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    while (isLoadingScreenState(&ctx)) {
        for (int i = 0; isLoadingScreenState(&ctx) &&
                        i < loading_spinner_frame_count;
             i++) {
            const uint8_t* frame = static_cast<const uint8_t*>(
                pgm_read_ptr(&loading_spinner_frames[i]));
            ui::blitIndexedFrame(lkbx->display, bitmapX, bitmapY, frame,
                                 loading_spinner_palette, loading_spinner_pixels,
                                 scratch, loading_spinner_width,
                                 loading_spinner_height);
            vTaskDelayUntil(&wakeTime, frameTicks);
        }
    }

    heap_caps_free(scratch);
    TaskHandles::loader = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

static int drawHeading(Adafruit_GFX& gfx, const String& title) {
    int yOffset = Display::StatusbarHeight + Display::Padding::P4;

    gfx.setFont(&FreeSansBold9pt7b);
    gfx.setCursor(Display::Padding::P2, yOffset);
    gfx.setTextColor(ST77XX_WHITE);
    gfx.println(title);

    yOffset += Display::Padding::P2;

    // then an hline between title and description
    gfx.drawFastHLine(Display::Padding::P2, yOffset,
                      Display::WIDTH - Display::Padding::P4, Colors::bgGray900);

    yOffset += Display::Padding::P4;

    return yOffset;
}

static void drawButtons(Adafruit_GFX& gfx, const Action right,
                        const Action left) {
    // get width of the left and right buttons
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    gfx.setFont(&FreeSans9pt7b);
    gfx.setTextColor(Colors::bgGray600);

    gfx.getTextBounds(left.name, 0, 0, &x1, &y1, &w1, &h1);
    gfx.getTextBounds(right.name, 0, 0, &x1, &y1, &w2, &h2);

    // Calculate button widths (use text width + padding + icon width)
    uint16_t iconWidth = 24;
    uint16_t iconPadding = 0;
    uint16_t leftWidth =
        w1 + 20 + iconWidth +
        iconPadding;  // 10px padding on each side, plus icon and its padding
    uint16_t rightWidth = w2 + 20 + iconWidth + iconPadding;

    if (left.visible) {
        gfx.drawRoundRect(Display::Padding::P2, Display::HEIGHT - 24, leftWidth,
                          24, 3, Colors::bgGray600);

        gfx.drawBitmap(Display::Padding::P2,
                       Display::HEIGHT - 24 + (24 - iconWidth) / 2, bitmap_left,
                       iconWidth, iconWidth, Colors::bgGray600);

        gfx.drawRoundRect(Display::WIDTH - Display::Padding::P2 - rightWidth,
                          Display::HEIGHT - 24, rightWidth, 24, 3,
                          Colors::bgGray600);

        gfx.setCursor(Display::Padding::P2 + iconWidth + iconPadding +
                          (leftWidth - iconWidth - iconPadding - w1) / 2,
                      Display::HEIGHT - 6);
        gfx.print(left.name);
    }

    if (right.visible) {
        gfx.drawBitmap(Display::WIDTH - Display::Padding::P2 - iconWidth,
                       Display::HEIGHT - 24 + (24 - iconWidth) / 2,
                       bitmap_right, iconWidth, iconWidth, Colors::bgGray600);

        gfx.setCursor(Display::WIDTH - Display::Padding::P2 - rightWidth +
                          (rightWidth - iconWidth - iconPadding - w2) / 2,
                      Display::HEIGHT - 6);
        gfx.print(right.name);
    }
}
void LKBX::drawQRCode(const String& title, const String& description,
                      const String& url, const String& label,
                      const Action& right, const Action& left,
                      bool descriptionOnly) {
    String stateName = "";
    sm->visit_current_states([&](auto state) { stateName = state.c_str(); });

    int qrWidth = 0;
    int qrHeight = 0;
    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        if (descriptionOnly) {
            // Match the version-7 QR and heading layout below. Redraw only
            // the description so each countdown tick leaves the page steady.
            constexpr int qrSize = (17 + 4 * 7) * 2;
            const int width = Display::WIDTH - qrSize - Display::Padding::P4;
            const int top = Display::StatusbarHeight + Display::Padding::P4 +
                            Display::Padding::P2 + Display::Padding::P4;
            GFXcanvas1 canvas(width, Display::HEIGHT - 24 - top);
            if (canvas.getBuffer()) {
                canvas.fillScreen(0);
                canvas.setFont(&FreeSans9pt7b);
                canvas.setTextColor(1);
                wrapText(canvas, description.c_str(), Display::Padding::P2, 14, width);
                display.drawBitmap(0, top, canvas.getBuffer(), canvas.width(),
                                   canvas.height(), Colors::white, Colors::black);
            }
            xSemaphoreGive(displayMutex);
            return;
        }
        clearScreen(display);

        QRCode qrcode;
        const int scale = 2;

        // NOLINTBEGIN(modernize-avoid-c-arrays)
        uint8_t qrcodeData[qrcode_getBufferSize(7)];
        // NOLINTEND(modernize-avoid-c-arrays)

        qrcode_initText(&qrcode, qrcodeData, 7, 1, url.c_str());

        int yOffset = constrain((Display::HEIGHT - qrcode.size * scale) / 2, 0,
                                Display::HEIGHT) +
                      Display::Padding::P2;
        int xOffset = constrain((Display::WIDTH - qrcode.size * scale), 0,
                                Display::WIDTH) -
                      Display::Padding::P2;

        // Draw the QR code
        for (uint8_t y = 0; y < qrcode.size; y++) {
            for (uint8_t x = 0; x < qrcode.size; x++) {
                if (qrcode_getModule(&qrcode, x, y)) {
                    display.fillRect(xOffset + x * scale, yOffset + y * scale,
                                     scale, scale, ST77XX_WHITE);
                }
            }
        }

        qrWidth = qrcode.size * scale;
        qrHeight = qrcode.size * scale;

        yOffset = drawHeading(display, title);
        // and finally the description in a smaller font

        // Draw "INFO" text in the middle of QR code
        display.setFont(&Tiny3x3a2pt7b);
        display.setTextColor(
            Colors::white);  // Light slate blue (R:22, G:45, B:46)
        display.setTextSize(2);

        // Calculate center position of QR code
        uint16_t textWidth, textHeight;
        int16_t x1, y1;
        display.getTextBounds(label.c_str(), 0, 0, &x1, &y1, &textWidth,
                              &textHeight);

        int textX = xOffset + (qrWidth - textWidth) / 2;
        int textY = yOffset + (qrHeight + textHeight) / 2;

        // Draw black pill background
        int pillPadding = 5;  // Reduced padding since default font is smaller
        display.fillRoundRect(
            textX - pillPadding + 1, textY - textHeight - pillPadding + 2,
            textWidth + (pillPadding * 2), textHeight + (pillPadding * 2),
            textHeight + pillPadding, Colors::bgGray600);

        display.setCursor(textX, textY);
        display.print(label.c_str());

        // reset text size after use.
        display.setTextSize(1);

        display.setFont(&FreeSans9pt7b);
        display.setTextColor(Colors::white);
        wrapText(display, description.c_str(), Display::Padding::P2, yOffset + 14,
                 xOffset - Display::Padding::P2);

        drawButtons(display, right, left);

        xSemaphoreGive(displayMutex);
    }
}

void LKBX::drawBreakEndRequest(const String& requestedBy) {
    if (xSemaphoreTake(displayMutex, 100) != pdTRUE) return;
    clearScreen(display);
    const int top = drawHeading(display, "End break requested");
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(Colors::white);
    const String description = requestedBy + " is asking you to end your break now. Accept?";
    wrapText(display, description.c_str(), Display::Padding::P2, top + 14,
             Display::WIDTH - Display::Padding::P4);
    drawButtons(display, {Str::Action::ACCEPT}, Action{.visible = false});
    xSemaphoreGive(displayMutex);
}

void LKBX::drawLockedActions() {
    // A new page clears the timer framebuffer even when its value is unchanged.
    bumpFreezeDisplayVersion();

    // The locking animation tints every frame with the user's hue shift, so the
    // static locked page must apply the same tint or the lock snaps back to the
    // stock red the moment the animation hands over. Read it before taking the
    // display mutex, in the same order as drawLockAnimationTask.
    int hueShiftAmount = 0;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        hueShiftAmount = preferences.getInt(Keys::Base::hueShift, 0);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        clearScreen(display);
        if (sm->is("locked.frozen"_s)) {
            constexpr uint16_t ice = 0x5EFF;
            auto centered = [&](const char* text, const GFXfont* font,
                                int baseline, uint16_t color) {
                int16_t x, y;
                uint16_t w, h;
                display.setFont(font);
                display.setTextColor(color);
                display.getTextBounds(text, 0, 0, &x, &y, &w, &h);
                display.setCursor((Display::WIDTH - w) / 2 - x, baseline);
                display.print(text);
            };
            centered("FROZEN", &FreeSansBold18pt7b, 62, ice);
            centered("by Chaster", &FreeSans9pt7b, 84, Colors::white);

            // A snowflake identifies the held session at a glance. The status
            // bar still shows the physical lock; avoid the ordinary red icon.
            constexpr int cx = Display::WIDTH / 2;
            constexpr int cy = 178;
            const int rays[][2] = {{0, -26}, {23, -13}, {23, 13},
                                  {0, 26}, {-23, 13}, {-23, -13}};
            auto stroke = [&](int x1, int y1, int x2, int y2) {
                display.drawLine(x1, y1, x2, y2, ice);
                display.drawLine(x1 + 1, y1, x2 + 1, y2, ice);
            };
            for (const auto& ray : rays) {
                const int dx = ray[0], dy = ray[1];
                const int jx = cx + dx * 2 / 3;
                const int jy = cy + dy * 2 / 3;
                stroke(cx, cy, cx + dx, cy + dy);
                stroke(jx, jy, jx + dx / 4 - dy / 4,
                       jy + dy / 4 + dx / 4);
                stroke(jx, jy, jx + dx / 4 + dy / 4,
                       jy + dy / 4 - dx / 4);
            }
            drawButtons(display, {Str::Menu::MENU}, Action{.visible = false});
            xSemaphoreGive(displayMutex);
            return;
        }
        constexpr int lockIconSize = 120;
        const int lockX = Display::WIDTH / 2 - lockIconSize / 2;
        const int lockY =
            Display::HEIGHT / 2 - lockIconSize / 2 + Display::StatusbarHeight;
        const uint16_t* lockFrame = lock_icons[lock_icons_length - 1];
        ui::RGB565Canvas tinted;
        if (hueShiftAmount != 0 &&
            tinted.allocate(lockIconSize, lockIconSize)) {
            memcpy(tinted.buffer(), lockFrame, tinted.byteSize());
            ui::applyHueShift(tinted.buffer(), lockIconSize * lockIconSize,
                              hueShiftAmount);
            lockFrame = tinted.buffer();
        }
        ui::fastBlit(display, lockX, lockY, lockFrame, lockIconSize,
                     lockIconSize);
        drawButtons(display, {Str::Menu::MENU}, Action{.visible = false});
        xSemaphoreGive(displayMutex);
    }
}

void LKBX::drawReadyToUnlock() {
    if (xSemaphoreTake(displayMutex, 100) != pdTRUE) return;
    clearScreen(display);
    const int top = drawHeading(display, "Ready to unlock");
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(Colors::white);
    wrapText(display, "Your time is complete. The box stays locked until you unlock now or add more time.",
             Display::Padding::P2, top + 14, Display::WIDTH - Display::Padding::P4);
    drawButtons(display, {"Unlock now"}, {"Add time"});
    xSemaphoreGive(displayMutex);
}

const uint16_t grayScaleColors[8] = {
    0xFFFF,  // White          (R: 31, G: 63, B: 31)
    0xDEFB,  // Light gray 1   (R: 27, G: 55, B: 27)
    0xBDF7,  // Light gray 2   (R: 23, G: 47, B: 23)
    0x9CF3,  // Medium gray 1  (R: 19, G: 39, B: 19)
    0x7BEF,  // Medium gray 2  (R: 15, G: 31, B: 15)
    0x5AEB,  // Dark gray 1    (R: 11, G: 23, B: 11)
    0x39E7,  // Dark gray 2    (R: 7,  G: 15, B: 7)
    0x18E3   // Very dark gray (R: 3,  G: 7,  B: 3)
};

void LKBX::drawPage(const TextPage* page) {
    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        clearScreen(display);
        int yOffset = drawHeading(display, page->header);
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(ST77XX_WHITE);

        wrapText(display, page->content, Display::Padding::P2, yOffset);
        xSemaphoreGive(displayMutex);
    }
}

// dark Loading Screen
void LKBX::drawLoadingScreen(const String& title, const String& description) {
    GFXcanvas1 canvas(Display::WIDTH - Display::Padding::P4,
                      Display::HEIGHT / 3);
    canvas.fillScreen(0);
    canvas.setCursor(0, 12);
    canvas.setFont(&FreeSans9pt7b);
    canvas.setTextColor(ST77XX_WHITE);
    wrapText(canvas, description.c_str());

    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        clearScreen(display);
        drawHeading(display, title);
        display.drawBitmap(
            Display::Padding::P2, Display::HEIGHT - Display::HEIGHT / 3,
            canvas.getBuffer(), Display::WIDTH - Display::Padding::P4,
            Display::HEIGHT / 3, Colors::white, Colors::black);
        xSemaphoreGive(displayMutex);
    }

    if (TaskHandles::loader != nullptr) {
        ESP_LOGW(TASKDEBUG_TAG, "loader task already running, skipping");
        return;
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating loader task");
    xTaskCreatePinnedToCore(
        loadingSpinnerTask, Tasks::loader.name, Tasks::loader.stackSize, this,
        Tasks::loader.priority, &TaskHandles::loader, Tasks::loader.core);
}
