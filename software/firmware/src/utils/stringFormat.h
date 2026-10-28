#ifndef LOCKBOX_STRINGFORMAT_H
#define LOCKBOX_STRINGFORMAT_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "constants/strings.h"

const uint64_t SECOND = 1000;
const uint64_t MINUTE = 60 * SECOND;
const uint64_t HOUR = 60 * MINUTE;
const uint64_t DAY = 24 * HOUR;
const uint64_t MONTH = 30 * DAY;
const uint64_t YEAR = 365 * DAY;

static const char* formatTimeString(uint64_t remainingTimeMs,
                                    bool includeSmaller = true) {
    static char buf[32];

    if (remainingTimeMs < SECOND) {
        return Str::Time::NOW;
    }

    if (remainingTimeMs < MINUTE) {
        uint64_t seconds = remainingTimeMs / SECOND;
        snprintf(buf, sizeof(buf), "%llu second%s", seconds,
                 seconds == 1 ? "" : "s");
        return buf;
    }

    uint64_t years = remainingTimeMs / YEAR;
    remainingTimeMs %= YEAR;
    uint64_t days = remainingTimeMs / DAY;
    remainingTimeMs %= DAY;
    uint64_t hours = remainingTimeMs / HOUR;
    remainingTimeMs %= HOUR;
    uint64_t minutes = remainingTimeMs / MINUTE;
    uint64_t seconds = (remainingTimeMs % MINUTE) / SECOND;

    char* p = buf;
    char* end = buf + sizeof(buf);

    if (years > 0)
        p += snprintf(p, end - p, "%lluy ", years);
    if (days > 0)
        p += snprintf(p, end - p, "%llud ", days);
    if (hours > 0)
        p += snprintf(p, end - p, "%lluh ", hours);

    if ((years == 0 && days == 0) || minutes > 0 || seconds > 0) {
        p += snprintf(p, end - p, "%llu:%02llu", minutes, seconds);
    }

    while (p > buf && *(p - 1) == ' ') p--;
    *p = '\0';

    return buf;
}

inline void wrapText(Adafruit_GFX& gfx, const char* text, int16_t x = 0,
                     int16_t y = 12, int16_t maxWidth = 0) {
    int16_t width = (maxWidth > 0) ? maxWidth : gfx.width();
    gfx.setCursor(x, y);

    char word[64];
    int wi = 0;

    for (const char* c = text; ; c++) {
        if (*c != ' ' && *c != '\0') {
            if (wi < (int)sizeof(word) - 1) word[wi++] = *c;
            continue;
        }

        if (*c == ' ') {
            if (wi < (int)sizeof(word) - 1) word[wi++] = ' ';
        }
        word[wi] = '\0';

        if (wi > 0) {
            int16_t x1, y1;
            uint16_t w, h;
            gfx.getTextBounds(word, 0, 0, &x1, &y1, &w, &h);

            if (gfx.getCursorX() + w > width) {
                gfx.println("");
                gfx.setCursor(x, gfx.getCursorY());
            }
            gfx.print(word);
        }

        wi = 0;
        if (*c == '\0') break;
    }
}

#endif  // LOCKBOX_STRINGFORMAT_H
