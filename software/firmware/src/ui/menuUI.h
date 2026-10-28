#ifndef LOCKBOX_MENUUI_H
#define LOCKBOX_MENUUI_H

#include "Adafruit_ST7789.h"
#include "structs/MenuItem.h"

// Off-screen menu renderer. The whole menu region (below the status bar) is
// composed onto a single PSRAM-backed GFXcanvas16 and pushed to the TFT with one
// ui::fastBlit, mirroring the timer/break/animation rendering path.
namespace MenuUI {

// Allocate the scratch canvas (PSRAM-first). Returns true once the canvas is
// usable; safe to call repeatedly. Pair with releaseCanvas() when the owning
// task exits.
bool ensureCanvasAllocated();
void releaseCanvas();

// Compose the menu for the given encoder value into the off-screen canvas.
void compose(int rawCurrentOption, int numOptions, const MenuItem* options);

// Push the composed canvas to the display. Takes displayMutex internally.
void blitTo(Adafruit_ST7789& display);

// Fallback renderer: draw the menu straight to the display, no off-screen
// canvas. Port of the pre-canvas renderer; used when the canvas cannot be
// allocated (e.g. boards whose PSRAM failed init) so the menu still renders
// instead of staying blank. Takes displayMutex internally.
void drawDirect(Adafruit_ST7789& display, int rawCurrentOption, int numOptions,
                const MenuItem* options);

}  // namespace MenuUI

#endif  // LOCKBOX_MENUUI_H
