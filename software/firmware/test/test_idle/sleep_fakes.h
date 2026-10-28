#pragma once
#include <cstdint>
#include <functional>
#include <vector>

struct SleepFake {
    bool power = false;
    bool usb = false;
    bool wakeError = false;
    bool inaAsleep = false;
    int brightness = 255;
    int deepSleeps = 0;
    int resetLow = 0;
    int button = -1;
    int buttonLevel = -1;
    uint64_t powerMask = 0;
    uint64_t timerUs = 0;
    std::vector<int> panelCommands;
    std::function<void()> duringPanelSleep;
};
inline SleepFake sleepFake;
constexpr int HIGH = 1, LOW = 0, OUTPUT = 1;
constexpr int ST77XX_SLPIN = 0x10, ST77XX_SLPOUT = 0x11;
using esp_event_base_t = const char*;
constexpr int ARDUINO_HW_CDC_CONNECTED_EVENT = 0;
constexpr int ARDUINO_HW_CDC_BUS_RESET_EVENT = 1;
constexpr int ARDUINO_HW_CDC_RX_EVENT = 2;
constexpr int ARDUINO_HW_CDC_TX_EVENT = 3;
struct HWCDC {
    static bool isPlugged() { return sleepFake.usb; }
};
struct SerialFake {
    using Handler = void (*)(void*, esp_event_base_t, int32_t, void*);
    Handler handler = nullptr;
    void onEvent(Handler callback) { handler = callback; }
};
inline SerialFake Serial;
inline int digitalRead(int) { return sleepFake.power ? HIGH : LOW; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int level) { if (level == LOW) ++sleepFake.resetLow; }
inline void ledcWrite(int, int duty) { sleepFake.brightness = duty; }
inline void delay(int) {}
struct Motor { static void clear() {} };
struct InaFake {
    void powerSave(bool asleep) { sleepFake.inaAsleep = asleep; }
};
inline InaFake ina;
struct DisplayFake {
    void writeCommand(int command) {
        sleepFake.panelCommands.push_back(command);
        if (command == ST77XX_SLPIN && sleepFake.duringPanelSleep)
            sleepFake.duringPanelSleep();
    }
};
inline DisplayFake display;
