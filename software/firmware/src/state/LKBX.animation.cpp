#include "LKBX.h"

#include <Arduino.h>

#include "Fonts/FreeSans9pt7b.h"
#include "ui/Animation.h"
#include "ui/bigIcons.h"
#include "ui/loadingSpinner.h"
#include "utils/clearDisplay.h"
#include "utils/psram.h"

#include <esp_heap_caps.h>

namespace {

    constexpr int kLockIconSize = Display::Icons::Big;

    // Draws the "LOCAL PLAY" caption straight onto the display. Used as the
    // playback's PostBlitFn, so it runs while blitFrame already holds
    // displayMutex — it must NOT take the mutex itself.
    void drawLocalPlayPostBlit(Adafruit_ST7789& display, void* /*context*/) {
        const char* word = Str::Overlay::LOCAL_PLAY;
        display.setFont(&FreeSans9pt7b);
        int16_t x1 = 0;
        int16_t y1 = 0;
        uint16_t w = 0;
        uint16_t h = 0;
        display.getTextBounds(word, 0, 0, &x1, &y1, &w, &h);
        display.setTextColor(Colors::bgGray600);
        display.setCursor((Display::WIDTH - w) / 2, Display::HEIGHT - 12);
        display.print(word);
    }

    bool isLockingState(void* context) {
        return static_cast<LKBX*>(context)->sm->is("locking"_s);
    }

    bool shouldHoldLockFrame(void* context) {
        auto* lkbx = static_cast<LKBX*>(context);
        return lkbx->sm->is("locking"_s) || lkbx->sm->is("locking.preflight"_s);
    }

}  // namespace

void drawLockAnimationTask(void* pvParameters) {
    auto* lkbx = static_cast<LKBX*>(pvParameters);

    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        clearScreen(lkbx->display);
        xSemaphoreGive(displayMutex);
    }

    int hueShiftAmount = 0;
    bool isLocalPlay = false;

    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        isLocalPlay = preferences.getBool(Keys::Base::isLocalPlay, false);
        hueShiftAmount = preferences.getInt(Keys::Base::hueShift, 0);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(LKBX_TAG, "hueShiftAmount: %d", hueShiftAmount);

    // Lock animation plays the frames forwards; unlock plays them in reverse
    // and therefore holds on the first frame instead of the last.
    const bool animateAsLock = isLocked();
    const int lastIndex = lock_icons_length - 1;

    const ui::FrameSequence sequence = {
        lock_icons,
        lock_icons_length,
        kLockIconSize,
        kLockIconSize,
    };

    ui::AnimationPlayback playback = {
        .display = &lkbx->display,
        .sequence = &sequence,
        .timing = {.firstFrameDelayMs = 150,
                   .frameDelayMs = 33,
                   .holdDelayMs = 100},
        .bitmapX = Display::WIDTH / 2 - kLockIconSize / 2,
        .bitmapY =
            Display::HEIGHT / 2 - kLockIconSize / 2 + Display::StatusbarHeight,
        .hueShiftDegrees = hueShiftAmount,
        .reverse = !animateAsLock,
        .loop = false,
        .holdFrameIndex = animateAsLock ? lastIndex : 0,
        .overlayFirstFrame = nullptr,
        .overlayContext = nullptr,
        .afterBlit = isLocalPlay ? drawLocalPlayPostBlit : nullptr,
        .afterBlitContext = nullptr,
        .shouldPlayFrame = isLockingState,
        .shouldHold = shouldHoldLockFrame,
        .context = lkbx,
    };

    ui::runAnimationPlayback(playback);

    TaskHandles::drawLockAnim = nullptr;
    vTaskDelete(nullptr);
}

void LKBX::drawLockAnimation() {
    if (TaskHandles::drawLockAnim != nullptr) {
        ESP_LOGW(TASKDEBUG_TAG, "drawLockAnim task already running, skipping");
        return;
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating draw lock animation task");
    xTaskCreatePinnedToCore(
        drawLockAnimationTask, Tasks::drawLockAnim.name,
        Tasks::drawLockAnim.stackSize, this, Tasks::drawLockAnim.priority,
        &TaskHandles::drawLockAnim, Tasks::drawLockAnim.core);
}

// ── Preflight loading animation ─────────────────────────────────────────────
// A single looping spinner with a dynamic caption underneath, shared across all
// init.preflight.* states. The spinner frames blit every tick; the caption is
// only redrawn when the text changes. QR screens call pauseLoadingAnimation()
// (which blocks until the task has parked) so they aren't overwritten.

namespace {

    constexpr int kLoadingFrameDelayMs = 67;
    constexpr int kLoadingSpinnerX =
        (Display::WIDTH - loading_spinner_width) / 2;
    constexpr int kLoadingSpinnerY = Display::StatusbarHeight + 8;
    constexpr int kLoadingCaptionY =
        kLoadingSpinnerY + loading_spinner_height + 6;
    constexpr int kLoadingCaptionH = Display::HEIGHT - kLoadingCaptionY;

    // Lifecycle/state flags driven by the LKBX helper methods below. volatile
    // because they are written from the state-machine task and read by the
    // loader.
    volatile bool sLoadingActive = false;
    volatile bool sLoadingPaused = false;
    volatile bool sLoadingPausedAck = false;
    volatile bool sLoadingTextDirty = false;
    const char* sLoadingText = "";

    // Caller must already hold displayMutex.
    void drawLoadingCaption(Adafruit_ST7789& display, const char* text) {
        display.fillRect(0, kLoadingCaptionY, Display::WIDTH, kLoadingCaptionH,
                         Colors::black);
        if (text == nullptr || text[0] == '\0') {
            return;
        }
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(Colors::white);

        int16_t x1 = 0;
        int16_t y1 = 0;
        uint16_t w = 0;
        uint16_t h = 0;
        display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

        display.setCursor((Display::WIDTH - w) / 2,
                          kLoadingCaptionY + (kLoadingCaptionH + h) / 2);
        display.print(text);
    }

    void loadingAnimationTask(void* pvParameters) {
        auto* lkbx = static_cast<LKBX*>(pvParameters);

        const size_t scratchBytes =
            static_cast<size_t>(loading_spinner_pixels) * sizeof(uint16_t);
        uint16_t* scratch = static_cast<uint16_t*>(
            lockboxAllocPreferPsram(scratchBytes));
        if (scratch == nullptr) {
            ESP_LOGE(LKBX_TAG, "loading anim: scratch alloc failed");
            TaskHandles::loadingAnim = nullptr;
            vTaskDelete(nullptr);
            return;
        }

        int frameIndex = 0;
        bool needFullRedraw = true;

        while (sLoadingActive) {
            if (sLoadingPaused) {
                // Park: acknowledge so pauseLoadingAnimation() can return, and
                // arrange a full redraw whenever we resume.
                sLoadingPausedAck = true;
                needFullRedraw = true;
                vTaskDelay(50 / portTICK_PERIOD_MS);
                continue;
            }

            if (needFullRedraw) {
                if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
                    clearScreen(lkbx->display);
                    xSemaphoreGive(displayMutex);
                }
                needFullRedraw = false;
                sLoadingTextDirty = true;
            }

            const uint8_t* frame = static_cast<const uint8_t*>(
                pgm_read_ptr(&loading_spinner_frames[frameIndex]));
            ui::blitIndexedFrame(
                lkbx->display, kLoadingSpinnerX, kLoadingSpinnerY, frame,
                loading_spinner_palette, loading_spinner_pixels, scratch,
                loading_spinner_width, loading_spinner_height);

            if (sLoadingTextDirty) {
                sLoadingTextDirty = false;
                if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
                    drawLoadingCaption(lkbx->display, sLoadingText);
                    xSemaphoreGive(displayMutex);
                }
            }

            frameIndex = (frameIndex + 1) % loading_spinner_frame_count;
            vTaskDelay(kLoadingFrameDelayMs / portTICK_PERIOD_MS);
        }

        heap_caps_free(scratch);
        TaskHandles::loadingAnim = nullptr;
        vTaskDelete(nullptr);
    }

}  // namespace

void LKBX::drawLoadingAnimation() {
    if (TaskHandles::loadingAnim != nullptr) {
        ESP_LOGW(TASKDEBUG_TAG, "loadingAnim task already running, skipping");
        return;
    }

    sLoadingActive = true;
    sLoadingPaused = false;
    sLoadingPausedAck = false;
    sLoadingTextDirty = true;

    ESP_LOGD(TASKDEBUG_TAG, "Creating loading animation task");
    xTaskCreatePinnedToCore(loadingAnimationTask, Tasks::loadingAnim.name,
                            Tasks::loadingAnim.stackSize, this,
                            Tasks::loadingAnim.priority,
                            &TaskHandles::loadingAnim, Tasks::loadingAnim.core);
}

void LKBX::pauseLoadingAnimation() {
    if (TaskHandles::loadingAnim == nullptr) {
        return;
    }

    sLoadingPausedAck = false;
    sLoadingPaused = true;

    // Block until the loader has parked so the caller's draw isn't overwritten.
    for (int i = 0; i < 60 && !sLoadingPausedAck; i++) {
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

void LKBX::playLoadingAnimation() {
    sLoadingPausedAck = false;
    sLoadingTextDirty = true;
    sLoadingPaused = false;
}

void LKBX::setLoadingText(const char* text) {
    sLoadingText = text;
    sLoadingTextDirty = true;
    playLoadingAnimation();
}

void LKBX::stopLoadingAnimation() {
    if (TaskHandles::loadingAnim == nullptr) {
        return;
    }

    sLoadingActive = false;

    // Join: wait for the task to self-delete and clear its handle before the
    // next screen (lock animation, OTA loader, etc.) takes over the display.
    for (int i = 0; i < 100 && TaskHandles::loadingAnim != nullptr; i++) {
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}
