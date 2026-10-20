#include "ui/menuUI.h"

#include <Fonts/FreeSans9pt7b.h>

#include "constants/Colors.h"
#include "constants/LogTags.h"
#include "constants/Sizes.h"
#include "services/display.h"
#include "ui/Animation.h"

namespace {

constexpr int kScrollWidth = 6;

// The canvas covers the entire region below the status bar, so canvas
// coordinates are display coordinates shifted up by StatusbarHeight on Y (X is
// unchanged). It is blitted to (0, StatusbarHeight).
constexpr int kCanvasWidth = Display::WIDTH;
constexpr int kCanvasHeight = Display::HEIGHT - Display::StatusbarHeight;
constexpr int kScrollHeight = kCanvasHeight;

constexpr int kMenuWidth =
    Display::WIDTH - kScrollWidth - Display::Padding::P1 * 2;
constexpr int kMenuItemHeight = Display::Icons::Small + Display::Padding::P2;

// Shared PSRAM-first scratch canvas (see ui::RGB565Canvas). Persisted for the
// lifetime of the menu task; released on task exit.
ui::RGB565Canvas sMenuCanvas;

void composeScrollBar(GFXcanvas16& canvas, int currentOption, int numOptions) {
    float scrollPercent = (float)currentOption / (numOptions);
    int scrollPosition = scrollPercent * (kScrollHeight - 20);
    int scrollX = Display::WIDTH - kScrollWidth;
    int scrollY = 0;

    canvas.fillRect(scrollX, scrollY, kScrollWidth, kScrollHeight,
                    Colors::black);
    canvas.drawFastVLine(scrollX + kScrollWidth / 2,
                         scrollY + Display::Padding::P0,
                         kScrollHeight - Display::Padding::P1, Colors::bgGray900);
    canvas.fillRoundRect(scrollX, scrollY + scrollPosition, kScrollWidth, 20, 3,
                         Colors::white);
}

void composeMenuItem(GFXcanvas16& canvas, int index, const MenuItem& option,
                     bool selected) {
    auto text = option.name;
    auto bitmap = option.bitmap;

    auto color = option.color > 0 ? option.color : Colors::white;
    auto unfocusedColor =
        option.unfocusedColor > 0 ? option.unfocusedColor : Colors::bgGray600;

    int x = Display::Padding::P1;
    int y = Display::Padding::P2 + (kMenuItemHeight)*index;

    canvas.fillRect(x, y, kMenuWidth, kMenuItemHeight, Colors::black);

    if (index > 0) {
        canvas.drawFastHLine(x + Display::Padding::P1, y,
                             kMenuWidth - Display::Padding::P2,
                             Colors::bgGray900);
    }

    if (selected) {
        canvas.fillRoundRect(x, y, kMenuWidth, kMenuItemHeight, 3,
                             Colors::bgGray900);
    }

    canvas.setFont(&FreeSans9pt7b);
    canvas.setTextColor(selected ? color : unfocusedColor);

    int padding = Display::Padding::P2;
    int textOffset = 6;

    canvas.drawBitmap(x + padding, y + textOffset, bitmap, Display::Icons::Small,
                      Display::Icons::Small, selected ? color : unfocusedColor);

    padding += Display::Icons::Small + Display::Padding::P2;

    canvas.setCursor(x + padding, y + textOffset + kMenuItemHeight / 2);
    canvas.print(text);
}

// --- Direct-draw fallback (no off-screen canvas) ---
// Port of the pre-canvas renderer (src/ui/menuUI.hpp, removed in 7db835e).
// Same layout math as the compose* functions above, but draws straight to the
// display under displayMutex, so it works with zero large allocations. Y is in
// display coordinates (below the status bar).

void drawScrollBarDirect(Adafruit_ST7789& display, int currentOption,
                         int numOptions) {
    float scrollPercent = (float)currentOption / (numOptions);
    int scrollPosition = scrollPercent * (kScrollHeight - 20);
    int scrollX = Display::WIDTH - kScrollWidth;
    int scrollY = Display::StatusbarHeight;

    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        display.fillRect(scrollX, scrollY, kScrollWidth, kScrollHeight,
                         Colors::black);
        display.drawFastVLine(scrollX + kScrollWidth / 2,
                              scrollY + Display::Padding::P0,
                              kScrollHeight - Display::Padding::P1,
                              Colors::bgGray900);
        display.fillRoundRect(scrollX, scrollY + scrollPosition, kScrollWidth,
                              20, 3, Colors::white);
        xSemaphoreGive(displayMutex);
    }
}

void drawMenuItemDirect(Adafruit_ST7789& display, int index,
                        const MenuItem& option, bool selected) {
    auto text = option.name;
    auto bitmap = option.bitmap;

    auto color = option.color > 0 ? option.color : Colors::white;
    auto unfocusedColor =
        option.unfocusedColor > 0 ? option.unfocusedColor : Colors::bgGray600;

    int x = Display::Padding::P1;
    int y = Display::StatusbarHeight + Display::Padding::P2 +
            (kMenuItemHeight)*index;

    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        display.fillRect(x, y, kMenuWidth, kMenuItemHeight, Colors::black);

        if (index > 0) {
            display.drawFastHLine(x + Display::Padding::P1, y,
                                  kMenuWidth - Display::Padding::P2,
                                  Colors::bgGray900);
        }

        if (selected) {
            display.fillRoundRect(x, y, kMenuWidth, kMenuItemHeight, 3,
                                  Colors::bgGray900);
        }

        display.setFont(&FreeSans9pt7b);
        display.setTextColor(selected ? color : unfocusedColor);

        int padding = Display::Padding::P2;
        int textOffset = 6;

        display.drawBitmap(x + padding, y + textOffset, bitmap,
                           Display::Icons::Small, Display::Icons::Small,
                           selected ? color : unfocusedColor);

        padding += Display::Icons::Small + Display::Padding::P2;

        display.setCursor(x + padding, y + textOffset + kMenuItemHeight / 2);
        display.print(text);

        xSemaphoreGive(displayMutex);
    }
}

}  // namespace

namespace MenuUI {

bool ensureCanvasAllocated() {
    if (sMenuCanvas.isValid()) return true;
    return sMenuCanvas.allocate(kCanvasWidth, kCanvasHeight);
}

void releaseCanvas() { sMenuCanvas.release(); }

void compose(int rawCurrentOption, int numOptions, const MenuItem* options) {
    if (numOptions <= 0 || options == nullptr || !sMenuCanvas.isValid()) return;

    GFXcanvas16& canvas = sMenuCanvas.gfx();

    int currentOption = _abs(rawCurrentOption % numOptions);

    if (rawCurrentOption < 0 && currentOption != 0) {
        currentOption = numOptions - currentOption;
    }

    canvas.fillScreen(Colors::black);

    if (numOptions <= 5) {
        for (int i = 0; i < numOptions; i++) {
            composeMenuItem(canvas, i, options[i], i == currentOption);
        }
        return;
    }

    composeScrollBar(canvas, currentOption, numOptions - 1);

    ESP_LOGD(MENU_UI_TAG, "Composing menu with currentOption: %d",
             currentOption);

    for (int i = 0; i < 5; i++) {
        int optionIndex = currentOption - 2 + i;
        if (optionIndex < 0) {
            optionIndex = numOptions + optionIndex;
        } else if (optionIndex > numOptions - 1) {
            optionIndex = optionIndex - numOptions;
        }

        composeMenuItem(canvas, i, options[optionIndex],
                        optionIndex == currentOption);
    }
}

void blitTo(Adafruit_ST7789& display) {
    if (!sMenuCanvas.isValid()) return;

    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        ui::fastBlit(display, 0, Display::StatusbarHeight, sMenuCanvas.buffer(),
                     kCanvasWidth, kCanvasHeight);
        xSemaphoreGive(displayMutex);
    }
}

void drawDirect(Adafruit_ST7789& display, int rawCurrentOption, int numOptions,
                const MenuItem* options) {
    if (numOptions <= 0 || options == nullptr) return;

    int currentOption = _abs(rawCurrentOption % numOptions);

    if (rawCurrentOption < 0 && currentOption != 0) {
        currentOption = numOptions - currentOption;
    }

    if (numOptions <= 5) {
        for (int i = 0; i < numOptions; i++) {
            drawMenuItemDirect(display, i, options[i], i == currentOption);
        }
        return;
    }

    drawScrollBarDirect(display, currentOption, numOptions - 1);

    ESP_LOGD(MENU_UI_TAG, "Direct-drawing menu with currentOption: %d",
             currentOption);

    for (int i = 0; i < 5; i++) {
        int optionIndex = currentOption - 2 + i;
        if (optionIndex < 0) {
            optionIndex = numOptions + optionIndex;
        } else if (optionIndex > numOptions - 1) {
            optionIndex = optionIndex - numOptions;
        }

        drawMenuItemDirect(display, i, options[optionIndex],
                           optionIndex == currentOption);
    }
}

}  // namespace MenuUI
