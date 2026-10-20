#ifndef LOCKBOX_STATUSICONCANVAS_H
#define LOCKBOX_STATUSICONCANVAS_H

#include <Adafruit_GFX.h>

#include "constants/Colors.h"
#include "constants/Sizes.h"

enum class StatusIconDot : uint8_t {
    Off = 0,
    YellowCircle = 1,
    YellowFilled = 2,
    GreenCircle = 3,
    GreenFilled = 4,
    RedFilled = 5,
    RedCircle = 6
};

namespace StatusIconCanvas {
constexpr int kSize = Display::Icons::Small;

inline GFXcanvas16 canvas(kSize, kSize);

inline void drawStatusDot(GFXcanvas16& c, StatusIconDot status) {
    constexpr uint8_t dotSize = 2;
    constexpr uint8_t padding = 2;
    constexpr uint8_t yPadding = 5;
    const int16_t dotX = padding;
    const int16_t dotY = kSize - yPadding;

    if (status != StatusIconDot::Off) {
        c.drawCircle(dotX, dotY, dotSize + 1, Colors::bgGray900);
        c.drawCircle(dotX, dotY, dotSize + 2, Colors::bgGray900);
    }

    switch (status) {
        case StatusIconDot::Off:
            c.fillCircle(dotX, dotY, dotSize, Colors::bgGray600);
            break;
        case StatusIconDot::YellowCircle:
            c.drawCircle(dotX, dotY, dotSize, Colors::yellow);
            break;
        case StatusIconDot::YellowFilled:
            c.fillCircle(dotX, dotY, dotSize, Colors::yellow);
            break;
        case StatusIconDot::GreenCircle:
            c.drawCircle(dotX, dotY, dotSize, Colors::green);
            break;
        case StatusIconDot::GreenFilled:
            c.fillCircle(dotX, dotY, dotSize, Colors::green);
            break;
        case StatusIconDot::RedFilled:
            c.fillCircle(dotX, dotY, dotSize, Colors::red);
            break;
        case StatusIconDot::RedCircle:
            c.drawCircle(dotX, dotY, dotSize, Colors::red);
            break;
    }
}

inline void compose(const uint8_t* bitmap, uint16_t color, StatusIconDot status,
                    bool showDot) {
    canvas.fillScreen(Colors::bgGray900);
    canvas.drawBitmap(0, 0, bitmap, kSize, kSize, color);
    if (showDot) {
        drawStatusDot(canvas, status);
    }
}

}  // namespace StatusIconCanvas

#endif  // LOCKBOX_STATUSICONCANVAS_H
