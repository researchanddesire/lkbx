#ifndef LOCKBOX_CLEARDISPLAY_H
#define LOCKBOX_CLEARDISPLAY_H

#include "Adafruit_GFX.h"
#include "constants/Sizes.h"

static auto clearScreen = [](Adafruit_GFX &display) {
    display.fillRect(0, Display::StatusbarHeight, Display::WIDTH,
                     Display::HEIGHT - Display::StatusbarHeight,
                     0x0000);  // Black color

};

#endif  // LOCKBOX_CLEARDISPLAY_H
