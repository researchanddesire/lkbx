#ifndef LOCKBOX_UI_ANIMATION_H
#define LOCKBOX_UI_ANIMATION_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstdint>

namespace ui {

struct FrameSequence {
    const uint16_t* const* frames;
    int frameCount;
    int width;
    int height;
};

struct AnimationTiming {
    TickType_t firstFrameDelayMs = 0;
    TickType_t frameDelayMs = 33;
    TickType_t holdDelayMs = 100;
    // When looping, divide this evenly across frames for drift-free seams.
    TickType_t loopPeriodMs = 0;
};

using FrameOverlayFn = void (*)(GFXcanvas16& canvas, void* context);
using PostBlitFn = void (*)(Adafruit_ST7789& display, void* context);

// Adafruit GFX 1.11.x's GFXcanvas16 only exposes a (w, h, bool allocate_buffer)
// constructor, so there is no supported way to hand it an externally-owned
// (PSRAM) buffer — passing a pointer silently decays to `bool` and the canvas
// mallocs its own buffer in internal RAM instead. This subclass adopts a
// caller-owned buffer. `buffer`/`buffer_owned` are protected in the base, and we
// set buffer_owned=false so the base destructor frees nothing.
class ExternalCanvas16 : public GFXcanvas16 {
   public:
    ExternalCanvas16(uint16_t width, uint16_t height, uint16_t* externalBuffer)
        : GFXcanvas16(width, height, false) {
        buffer = externalBuffer;
        buffer_owned = false;
    }
};

// Single scratch RGB565 buffer (PSRAM-first). Used for hue-shift + overlays.
class RGB565Canvas {
   public:
    RGB565Canvas() = default;
    ~RGB565Canvas() { release(); }

    RGB565Canvas(const RGB565Canvas&) = delete;
    RGB565Canvas& operator=(const RGB565Canvas&) = delete;

    bool allocate(int width, int height);
    void release();
    bool isValid() const { return canvas_ != nullptr; }
    int width() const { return width_; }
    int height() const { return height_; }
    size_t byteSize() const { return byteSize_; }
    uint16_t* buffer();
    GFXcanvas16& gfx();

   private:
    int width_ = 0;
    int height_ = 0;
    size_t byteSize_ = 0;
    uint16_t* buffer_ = nullptr;
    GFXcanvas16* canvas_ = nullptr;
};

uint16_t hueShiftRgb565(uint16_t color565, int degrees);
void applyHueShift(uint16_t* buffer, int pixelCount, int degrees);

// Copy frame into canvas when hue-shift or overlay is needed; otherwise returns src.
const uint16_t* composeFrame(const uint16_t* frame, int pixelCount,
                             int hueShiftDegrees, FrameOverlayFn overlay,
                             void* overlayContext, RGB565Canvas& canvas);

// Bulk DMA blit of an RGB565 buffer. Caller is responsible for holding
// displayMutex. Uses one setAddrWindow + one block SPI transfer instead of
// Adafruit_GFX::drawRGBBitmap's per-pixel path (~27x faster).
void fastBlit(Adafruit_ST7789& display, int x, int y, const uint16_t* src,
              int width, int height);

void blitFrame(Adafruit_ST7789& display, int x, int y, const uint16_t* src,
               int width, int height, PostBlitFn afterBlit = nullptr,
               void* afterBlitContext = nullptr);

// Expand an 8-bit indexed PROGMEM frame through a palette into scratch, then blit.
void expandIndexedFrame(const uint8_t* indices, const uint16_t* palette,
                        int pixelCount, uint16_t* scratch);

void blitIndexedFrame(Adafruit_ST7789& display, int x, int y,
                      const uint8_t* indices, const uint16_t* palette,
                      int pixelCount, uint16_t* scratch, int width, int height,
                      PostBlitFn afterBlit = nullptr,
                      void* afterBlitContext = nullptr);

struct AnimationPlayback {
    Adafruit_ST7789* display;
    const FrameSequence* sequence;
    AnimationTiming timing;
    int bitmapX;
    int bitmapY;
    int hueShiftDegrees;
    bool reverse;
    bool loop = false;
    int holdFrameIndex;
    FrameOverlayFn overlayFirstFrame;
    void* overlayContext;
    PostBlitFn afterBlit;
    void* afterBlitContext;
    bool (*shouldPlayFrame)(void* context);
    bool (*shouldHold)(void* context);
    void* context;
};

void runAnimationPlayback(AnimationPlayback& playback);
void runLoopingAnimation(AnimationPlayback& playback);

}  // namespace ui

#endif  // LOCKBOX_UI_ANIMATION_H
