#include <OneButton.h>

#include "constants/Pins.h"
#include "services/display.h"
#include "services/encoder.h"

#include "test_utils.h"

static constexpr uint32_t INPUT_TIMEOUT_MS = 30000;
static constexpr uint32_t POLL_MS = 25;
static constexpr int CONTENT_Y = Display::StatusbarHeight;
static constexpr int CONTENT_H = Display::HEIGHT - CONTENT_Y;

// ── Display helpers (content area only — preserves header) ──────────────

static void clearContent() {
    display.fillRect(0, CONTENT_Y, Display::WIDTH, CONTENT_H, Colors::black);
}

static void showPrompt(const char* line1, const char* line2,
                       int secondsLeft) {
    if (!displayMutex) return;
    if (xSemaphoreTake(displayMutex, portTICK_PERIOD_MS * 200) != pdTRUE)
        return;

    clearContent();

    display.setTextColor(Colors::white);
    display.setTextSize(2);
    display.setCursor(Display::Padding::P2, CONTENT_Y + 30);
    display.print(line1);

    display.setTextSize(2);
    display.setCursor(Display::Padding::P2, CONTENT_Y + 70);
    display.print(line2);

    display.setTextColor(Colors::bgGray600);
    display.setTextSize(1);
    display.setCursor(Display::Padding::P2, Display::HEIGHT - 20);
    display.printf("Timeout in %ds", secondsLeft);

    xSemaphoreGive(displayMutex);
}

static void showResult(const char* label, bool passed) {
    if (!displayMutex) return;
    if (xSemaphoreTake(displayMutex, portTICK_PERIOD_MS * 200) != pdTRUE)
        return;

    clearContent();
    display.setTextSize(2);
    display.setTextColor(passed ? Colors::green : Colors::red);
    display.setCursor(Display::Padding::P2, CONTENT_Y + 40);
    display.printf("%s: %s", label, passed ? "PASS" : "FAIL");

    xSemaphoreGive(displayMutex);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// ── Polling loop shared by all input tests ──────────────────────────────

template <typename PollFn, typename CheckFn>
static bool waitForInput(const char* line1, const char* line2,
                         PollFn pollFn, CheckFn checkFn) {
    serialPrompt(line2);

    uint32_t start = millis();
    int lastDisplayedSecond = -1;

    while ((millis() - start) < INPUT_TIMEOUT_MS) {
        pollFn();

        if (checkFn()) return true;

        int secsLeft =
            (int)((INPUT_TIMEOUT_MS - (millis() - start)) / 1000) + 1;
        if (secsLeft != lastDisplayedSecond) {
            lastDisplayedSecond = secsLeft;
            showPrompt(line1, line2, secsLeft);
        }

        vTaskDelay(POLL_MS / portTICK_PERIOD_MS);
    }

    return checkFn();
}

// ── Button tests ────────────────────────────────────────────────────────

static volatile bool enterClicked = false;

void test_enter_button_click() {
    enterClicked = false;
    OneButton btn(Pins::Buttons::buttonEnter, true);
    btn.attachClick([]() { enterClicked = true; });

    bool ok = waitForInput(
        "ENTER button", "Press the ENTER button",
        [&]() { btn.tick(); }, []() { return (bool)enterClicked; });

    showResult("Enter click", ok);
    TEST_ASSERT_TRUE_MESSAGE(ok, "ENTER button click not detected within 30s");
}

static volatile bool backClicked = false;

void test_back_button_click() {
    backClicked = false;
    OneButton btn(Pins::Buttons::buttonBack, true);
    btn.attachClick([]() { backClicked = true; });

    bool ok = waitForInput(
        "BACK button", "Press the BACK button",
        [&]() { btn.tick(); }, []() { return (bool)backClicked; });

    showResult("Back click", ok);
    TEST_ASSERT_TRUE_MESSAGE(ok, "BACK button click not detected within 30s");
}

static volatile bool enterLongPressed = false;

void test_enter_button_long_press() {
    enterLongPressed = false;
    OneButton btn(Pins::Buttons::buttonEnter, true);
    btn.attachLongPressStart([]() { enterLongPressed = true; });

    bool ok = waitForInput(
        "ENTER button", "Long-press ENTER (hold 1s)",
        [&]() { btn.tick(); }, []() { return (bool)enterLongPressed; });

    showResult("Enter long", ok);
    TEST_ASSERT_TRUE_MESSAGE(ok,
                             "ENTER long press not detected within 30s");
}

static volatile bool backLongPressed = false;

void test_back_button_long_press() {
    backLongPressed = false;
    OneButton btn(Pins::Buttons::buttonBack, true);
    btn.attachLongPressStart([]() { backLongPressed = true; });

    bool ok = waitForInput(
        "BACK button", "Long-press BACK (hold 1s)",
        [&]() { btn.tick(); }, []() { return (bool)backLongPressed; });

    showResult("Back long", ok);
    TEST_ASSERT_TRUE_MESSAGE(ok, "BACK long press not detected within 30s");
}

// ── Encoder test ────────────────────────────────────────────────────────

void test_encoder_rotation() {
    static bool encoderInited = false;
    if (!encoderInited) {
        initEncoder();
        encoderInited = true;
    }

    rotaryEncoder.setEncoderValue(0);
    rotaryEncoder.readEncoder();

    bool ok = waitForInput(
        "Encoder knob", "Turn the encoder knob",
        []() {},
        []() {
            long val = rotaryEncoder.readEncoder();
            return val != 0;
        });

    long val = rotaryEncoder.readEncoder();
    char msg[64];
    snprintf(msg, sizeof(msg), "Encoder value: %ld", val);
    TEST_MESSAGE(msg);

    showResult("Encoder", ok);
    TEST_ASSERT_TRUE_MESSAGE(ok,
                             "Encoder rotation not detected within 30s");
}

// ── Registration ────────────────────────────────────────────────────────

void register_interactive_tests() {
    RUN_TEST(test_enter_button_click);
    RUN_TEST(test_back_button_click);
    RUN_TEST(test_enter_button_long_press);
    RUN_TEST(test_back_button_long_press);
    RUN_TEST(test_encoder_rotation);
}
