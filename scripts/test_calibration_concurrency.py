#!/usr/bin/env python3
"""Run the firmware calibration functions and SML rows with blocked hardware."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "boost/sml.hpp"
using namespace std::chrono_literals;
using namespace boost::sml;
using String = std::string;
#include "state/Events.h"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
constexpr int INPUT = 0, OUTPUT = 1, pdTRUE = 1, pdPASS = 1;
constexpr int portMAX_DELAY = -1, portTICK_PERIOD_MS = 1;
using TaskHandle_t = void*;
namespace TaskHandles { TaskHandle_t calibrate = nullptr; }
namespace Tasks {
struct Params { const char* name; int stackSize, priority, core; };
constexpr Params calibrate{"calibrate", 8192, 1, 1};
}
namespace Pins {
namespace MOTOR { int A = 46; }
namespace Buttons { int buttonEnter = 12; }
}
namespace Keys {
constexpr char base[] = "base";
namespace Base {
constexpr char isButtonInverted[] = "inverted";
constexpr char isButtonCalibrated[] = "calibrated";
}
}
struct Preferences {
    std::map<std::string, bool> values;
    void begin(const char*, bool) {}
    void end() {}
    void putBool(const char* key, bool value) { values[key] = value; }
} preferences;
int preferencesMutex = 0;
int xSemaphoreTake(int, int) { return pdTRUE; }
void xSemaphoreGive(int) {}
std::atomic<int> pressedPin{0}, polls{0}, motorCalls{0};
std::atomic<bool> finishMotor{false};
std::map<int, int> modes;
void pinMode(int pin, int mode) { modes[pin] = mode; }
int digitalRead(int pin) { return pressedPin == pin ? 1 : 0; }
void vTaskDelay(int) { ++polls; std::this_thread::sleep_for(1ms); }
void vTaskDelete(void*) {}
void (*queuedTask)(void*) = nullptr;
void* queuedArgument = nullptr;
bool failTask = false;
int starts = 0;
int xTaskCreatePinnedToCore(void (*task)(void*), const char*, int, void* arg,
                           int, void** handle, int) {
    ++starts;
    if (failTask) return 0;
    queuedTask = task;
    queuedArgument = arg;
    *handle = arg;
    return pdPASS;
}
struct Motor {
    void calibrate() {
        ++motorCalls;
        while (!finishMotor) std::this_thread::sleep_for(1ms);
    }
};
namespace Str::Screen {
constexpr char CALIBRATING_MOTOR_TITLE[] = "motor";
constexpr char CALIBRATING_MOTOR_DESC[] = "calibrating";
}
struct Start {};
struct LKBX {
    void calibrate();
    void calibrateButton();
    static void calibrateTask(void*);
    struct Machine {
        auto operator()() const {
            auto calibrateButton = [](LKBX& o) { o.calibrate(); };
            auto drawCalibrationQR = [](LKBX&) {};
            auto drawLoadingScreen = [](const char*, const char*) {
                return [](LKBX&) {};
            };
            return make_transition_table(
                *"init"_s + event<Start> = "calibrate"_s,
                @ROWS@
            );
        }
    };
    std::unique_ptr<boost::sml::sm<Machine, thread_safe<std::recursive_mutex>>> sm;
    Motor motor;
    LKBX() : sm(std::make_unique<boost::sml::sm<Machine,
                 thread_safe<std::recursive_mutex>>>(*this)) {}
};
@FUNCTIONS@
template<class F> void eventually(F condition) {
    auto end = std::chrono::steady_clock::now() + 2s;
    while (!condition()) {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(1ms);
    }
}
void responsive(LKBX& box) {
    auto request = std::async(std::launch::async, [&] {
        assert(!box.sm->process_event(BackPress{}));
        assert(!box.sm->process_event(ButtonPress{}));
        assert(!box.sm->process_event(Done{}));
        assert(!box.sm->process_event(Recalibrate{}));
        assert(!box.sm->process_event(EncoderValue{1}));
    });
    assert(request.wait_for(500ms) == std::future_status::ready);
    request.get();
}
int main() {
    for (bool inverted : {false, true}) {
        Pins::MOTOR::A = 46;
        Pins::Buttons::buttonEnter = 12;
        pressedPin = polls = motorCalls = 0;
        finishMotor = false;
        preferences.values.clear();
        LKBX box;
        assert(box.sm->process_event(Start{}));
        const int started = starts;
        box.calibrate();
        assert(starts == started);
        std::thread worker(queuedTask, queuedArgument);
        eventually([] { return polls.load() > 2; });
        responsive(box);
        assert(box.sm->is("calibrate"_s));
        assert(preferences.values.empty());
        assert(motorCalls == 0);
        pressedPin = inverted ? 46 : 12;
        eventually([] { return motorCalls.load() == 1; });
        responsive(box);
        assert(box.sm->is("calibrate.motor"_s));
        assert(preferences.values.at("calibrated"));
        assert(preferences.values.at("inverted") == inverted);
        assert(modes.at(Pins::MOTOR::A) == OUTPUT);
        assert(modes.at(Pins::Buttons::buttonEnter) == INPUT);
        finishMotor = true;
        worker.join();
        assert(TaskHandles::calibrate == nullptr);
    }
    failTask = true;
    LKBX box;
    assert(box.sm->process_event(Start{}));
    assert(TaskHandles::calibrate == nullptr);
    responsive(box);
    assert(box.sm->is("calibrate"_s));
}
'''


class CalibrationConcurrencyTests(unittest.TestCase):
    def test_ble_events_remain_responsive_without_bypassing_pin_detection(self):
        source = (ROOT / "src/state/LKBX.cpp").read_text(encoding="utf-8")
        functions = source[source.index("void LKBX::calibrate() {"):]
        machine = (ROOT / "src/state/LKBX.h").read_text(encoding="utf-8")
        start = machine.index('"calibrate"_s + on_entry')
        end = machine.index("// Start the looping loading animation", start)
        rows = machine[start:end].strip().rstrip(",")
        harness = HARNESS.replace("@ROWS@", rows).replace("@FUNCTIONS@", functions)
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            cpp = temp / "calibration.cpp"
            cpp.write_text(harness, encoding="utf-8")
            executable = temp / "calibration.exe"
            subprocess.run([
                "c++", "-std=c++17", "-pthread", f"-I{ROOT / 'include'}",
                f"-I{ROOT / 'src'}", str(cpp), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
