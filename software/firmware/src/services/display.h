#ifndef LOCKBOX_DISPLAY_H
#define LOCKBOX_DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "constants/Colors.h"
#include "constants/Pins.h"
#include "constants/Sizes.h"
#include "utils/RecusiveMutex.h"
#include "capturedDisplay.h"

/**
 * Welcome to our display service.
 *
 * Here are some documents that might help you to start drawing with this
 * library:
 *
 * https://github.com/olikraus/u8g2/wiki/u8g2reference
 *
 * And here's a list of all U8G2 fonts:
 *
 * https://github.com/olikraus/u8g2/wiki/fntlistallplain
 */

extern long timeout;

#ifdef VERSIONDEV
extern CapturedDisplay display;
#else
extern Adafruit_ST7789 display;
#endif

// semaphore for multithreading with display
extern SemaphoreHandle_t displayMutex;

void initDisplay();

#endif  // LOCKBOX_DISPLAY_H
