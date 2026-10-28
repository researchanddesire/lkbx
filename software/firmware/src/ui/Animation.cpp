#include "ui/Animation.h"

#include <cmath>
#include <cstring>
#include <pgmspace.h>

#include "services/display.h"
#include "utils/psram.h"

namespace ui {

namespace {

constexpr float kPi = 3.14159265359f;
constexpr int kColorMatrixSize = 20;

void hueRotateMatrix(float hue, float matrix[kColorMatrixSize]) {
    const float cosHue = cosf(hue * kPi / 180.0f);
    const float sinHue = sinf(hue * kPi / 180.0f);
    matrix[0] = 0.213f + cosHue * 0.787f - sinHue * 0.213f;
    matrix[1] = 0.715f - cosHue * 0.715f - sinHue * 0.715f;
    matrix[2] = 0.072f - cosHue * 0.072f + sinHue * 0.928f;
    matrix[3] = matrix[4] = 0;
    matrix[5] = 0.213f - cosHue * 0.213f + sinHue * 0.143f;
    matrix[6] = 0.715f + cosHue * 0.285f + sinHue * 0.140f;
    matrix[7] = 0.072f - cosHue * 0.072f - sinHue * 0.283f;
    matrix[8] = matrix[9] = 0;
    matrix[10] = 0.213f - cosHue * 0.213f - sinHue * 0.787f;
    matrix[11] = 0.715f - cosHue * 0.715f + sinHue * 0.715f;
    matrix[12] = 0.072f + cosHue * 0.928f + sinHue * 0.072f;
    matrix[13] = matrix[14] = 0;
    matrix[15] = matrix[16] = matrix[17] = 0;
    matrix[18] = 1;
    matrix[19] = 0;
}

}  // namespace

bool RGB565Canvas::allocate(int width, int height) {
    release();
    if (width <= 0 || height <= 0) {
        return false;
    }

    width_ = width;
    height_ = height;
    byteSize_ = static_cast<size_t>(width_) * static_cast<size_t>(height_) *
                sizeof(uint16_t);

    buffer_ = static_cast<uint16_t*>(lockboxAllocPreferPsram(byteSize_));
    if (buffer_ == nullptr) {
        release();
        return false;
    }

    canvas_ = new (std::nothrow) ExternalCanvas16(width_, height_, buffer_);
    if (canvas_ == nullptr || canvas_->getBuffer() == nullptr) {
        release();
        return false;
    }
    return true;
}

void RGB565Canvas::release() {
    delete canvas_;
    canvas_ = nullptr;
    if (buffer_ != nullptr) {
        heap_caps_free(buffer_);
        buffer_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
    byteSize_ = 0;
}

uint16_t* RGB565Canvas::buffer() { return buffer_; }

GFXcanvas16& RGB565Canvas::gfx() { return *canvas_; }

uint16_t hueShiftRgb565(uint16_t color565, int degrees) {
    uint8_t r = (color565 >> 11) << 3;
    uint8_t g = ((color565 >> 5) & 0x3F) << 2;
    uint8_t b = (color565 & 0x1F) << 3;

    if ((r == 0 && g == 0 && b == 0) || (r >= 248 && g >= 248 && b >= 248)) {
        return color565;
    }

    float matrix[kColorMatrixSize];
    hueRotateMatrix(static_cast<float>(degrees), matrix);

    const float rf = r / 255.0f;
    const float gf = g / 255.0f;
    const float bf = b / 255.0f;

    float newR = rf * matrix[0] + gf * matrix[1] + bf * matrix[2];
    float newG = rf * matrix[5] + gf * matrix[6] + bf * matrix[7];
    float newB = rf * matrix[10] + gf * matrix[11] + bf * matrix[12];

    newR = min(max(newR, 0.0f), 1.0f);
    newG = min(max(newG, 0.0f), 1.0f);
    newB = min(max(newB, 0.0f), 1.0f);

    const uint8_t finalR = static_cast<uint8_t>(newR * 255.0f);
    const uint8_t finalG = static_cast<uint8_t>(newG * 255.0f);
    const uint8_t finalB = static_cast<uint8_t>(newB * 255.0f);

    return static_cast<uint16_t>(((finalR >> 3) << 11) | ((finalG >> 2) << 5) |
                                 (finalB >> 3));
}

void applyHueShift(uint16_t* buffer, int pixelCount, int degrees) {
    if (degrees == 0) {
        return;
    }
    for (int i = 0; i < pixelCount; i++) {
        buffer[i] = hueShiftRgb565(buffer[i], degrees);
    }
}

const uint16_t* composeFrame(const uint16_t* frame, int pixelCount,
                             int hueShiftDegrees, FrameOverlayFn overlay,
                             void* overlayContext, RGB565Canvas& canvas) {
    const bool needsCanvas =
        hueShiftDegrees != 0 || overlay != nullptr;

    if (!needsCanvas || !canvas.isValid()) {
        return frame;
    }

    memcpy(canvas.buffer(), frame, canvas.byteSize());
    if (hueShiftDegrees != 0) {
        applyHueShift(canvas.buffer(), pixelCount, hueShiftDegrees);
    }
    if (overlay != nullptr) {
        overlay(canvas.gfx(), overlayContext);
    }
    return canvas.buffer();
}

void fastBlit(Adafruit_ST7789& display, int x, int y, const uint16_t* src,
              int width, int height) {
#ifdef VERSIONDEV
    if (&display == &::display)
        ::display.captureBitmap(x, y, src, width, height);
#endif
    // Bulk DMA blit: one address-window set + one block SPI transfer, instead of
    // Adafruit_GFX::drawRGBBitmap's per-pixel writePixel() (which re-sends the
    // address window for every pixel — ~189ms for a 120x120 frame vs ~7ms here).
    // writePixels does the same endian swap as SPI_WRITE16, so pixels match.
    display.startWrite();
    display.setAddrWindow(x, y, width, height);
    display.writePixels(
        const_cast<uint16_t*>(src),
        static_cast<uint32_t>(width) * static_cast<uint32_t>(height));
    display.endWrite();
}

void blitFrame(Adafruit_ST7789& display, int x, int y, const uint16_t* src,
               int width, int height, PostBlitFn afterBlit,
               void* afterBlitContext) {
    if (xSemaphoreTake(displayMutex, 100) != pdTRUE) {
        return;
    }
    fastBlit(display, x, y, src, width, height);
    if (afterBlit != nullptr) {
        afterBlit(display, afterBlitContext);
    }
    xSemaphoreGive(displayMutex);
}

void expandIndexedFrame(const uint8_t* indices, const uint16_t* palette,
                        int pixelCount, uint16_t* scratch) {
    for (int i = 0; i < pixelCount; i++) {
        const uint8_t index = pgm_read_byte(&indices[i]);
        scratch[i] = pgm_read_word(&palette[index]);
    }
}

void blitIndexedFrame(Adafruit_ST7789& display, int x, int y,
                      const uint8_t* indices, const uint16_t* palette,
                      int pixelCount, uint16_t* scratch, int width, int height,
                      PostBlitFn afterBlit, void* afterBlitContext) {
    expandIndexedFrame(indices, palette, pixelCount, scratch);
    blitFrame(display, x, y, scratch, width, height, afterBlit,
              afterBlitContext);
}

void runAnimationPlayback(AnimationPlayback& playback) {
    const FrameSequence& seq = *playback.sequence;
    const int pixelCount = seq.width * seq.height;
    RGB565Canvas canvas;

    if (!canvas.allocate(seq.width, seq.height)) {
        return;
    }

    const int lastIndex = seq.frameCount - 1;
    const int holdIndex =
        playback.holdFrameIndex < 0 ? lastIndex : playback.holdFrameIndex;

    const bool useLoopPeriod =
        playback.loop && playback.timing.loopPeriodMs > 0 && seq.frameCount > 0;
    const TickType_t loopFrameTicks = useLoopPeriod
                                          ? pdMS_TO_TICKS(playback.timing.loopPeriodMs) /
                                                seq.frameCount
                                          : 0;
    TickType_t loopWakeTime = xTaskGetTickCount();

    bool isFirstCycle = true;
    while (playback.shouldPlayFrame(playback.context)) {
        for (int i = 0; playback.shouldPlayFrame(playback.context) &&
                        i < seq.frameCount;
             i++) {
            const int frameIndex = playback.reverse ? lastIndex - i : i;
            const uint16_t* frame = static_cast<const uint16_t*>(
                pgm_read_ptr(&seq.frames[frameIndex]));

            const bool drawOverlay = !playback.loop && isFirstCycle && i == 0 &&
                                     playback.overlayFirstFrame != nullptr;
            const uint16_t* src = composeFrame(
                frame, pixelCount, playback.hueShiftDegrees,
                drawOverlay ? playback.overlayFirstFrame : nullptr,
                playback.overlayContext, canvas);

            blitFrame(*playback.display, playback.bitmapX, playback.bitmapY, src,
                      seq.width, seq.height, playback.afterBlit,
                      playback.afterBlitContext);

            if (useLoopPeriod) {
                vTaskDelayUntil(&loopWakeTime, loopFrameTicks);
            } else {
                const TickType_t delay =
                    (isFirstCycle && i == 0 &&
                     playback.timing.firstFrameDelayMs > 0)
                        ? playback.timing.firstFrameDelayMs
                        : playback.timing.frameDelayMs;
                vTaskDelay(delay / portTICK_PERIOD_MS);
            }
        }

        isFirstCycle = false;
        if (!playback.loop) {
            break;
        }
    }

    if (playback.loop || !playback.shouldHold(playback.context)) {
        canvas.release();
        return;
    }

    const uint16_t* holdFrame = static_cast<const uint16_t*>(
        pgm_read_ptr(&seq.frames[holdIndex]));

    // The hold frame is static — draw it once, then just poll for the exit
    // condition instead of re-blitting the same pixels every holdDelayMs.
    const uint16_t* holdSrc =
        composeFrame(holdFrame, pixelCount, playback.hueShiftDegrees, nullptr,
                     nullptr, canvas);
    blitFrame(*playback.display, playback.bitmapX, playback.bitmapY, holdSrc,
              seq.width, seq.height, playback.afterBlit,
              playback.afterBlitContext);

    while (playback.shouldHold(playback.context)) {
        vTaskDelay(playback.timing.holdDelayMs / portTICK_PERIOD_MS);
    }

    canvas.release();
}

void runLoopingAnimation(AnimationPlayback& playback) {
    playback.loop = true;
    runAnimationPlayback(playback);
}

}  // namespace ui
