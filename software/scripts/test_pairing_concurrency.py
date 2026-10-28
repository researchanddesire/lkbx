#!/usr/bin/env python3
"""Exercise the actual state-machine mutex under contention past one second."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "firmware"

FREERTOS = r'''
#pragma once
#include <mutex>
#include <chrono>
#include <cstdint>
#include <cassert>
#define configASSERT(x) assert(x)
using BaseType_t = int;
using SemaphoreHandle_t = std::recursive_timed_mutex*;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
constexpr uint32_t portTICK_PERIOD_MS = 1;
constexpr int pdTRUE = 1;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    return new std::recursive_timed_mutex;
}
inline void vSemaphoreDelete(SemaphoreHandle_t mutex) { delete mutex; }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t mutex, uint32_t ticks) {
    if (ticks == portMAX_DELAY) { mutex->lock(); return pdTRUE; }
    return mutex->try_lock_for(std::chrono::milliseconds(ticks)) ? pdTRUE : 0;
}
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t mutex) {
    mutex->unlock(); return pdTRUE;
}
'''

TEST = r'''
#include <cassert>
#include <atomic>
#include <thread>
#include <future>
#include "utils/RecusiveMutex.h"
#include "utils/pairingStatus.h"
int main() {
    assert(pairingStatusFromHttp(200) == PairingStatus::Paired);
    assert(pairingStatusFromHttp(418) == PairingStatus::Unpaired);
    for (int code : {-11, -1, 0, 201, 400, 401, 403, 404, 429, 500, 502, 503})
        assert(pairingStatusFromHttp(code) == PairingStatus::Unknown);

    ESP32RecursiveMutex mutex;
    mutex.lock();
    mutex.lock(); // The state machine emits nested events.
    mutex.unlock();
    std::promise<void> started;
    auto ready = started.get_future();
    std::atomic<bool> entered{false};
    std::atomic<bool> tryAcquired{true};
    std::thread contender([&] {
        tryAcquired = mutex.try_lock();
        if (tryAcquired) mutex.unlock();
        started.set_value();
        mutex.lock();
        entered = true;
        mutex.unlock();
    });
    ready.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    assert(!tryAcquired);
    assert(!entered); // The former one-second lock silently entered here.
    mutex.unlock();
    contender.join();
    assert(entered);
}
'''


class PairingConcurrencyTests(unittest.TestCase):
    def test_mutex_ownership_and_pairing_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "freertos").mkdir()
            (temp / "boost").mkdir()
            (temp / "freertos/FreeRTOS.h").write_text(FREERTOS)
            (temp / "freertos/task.h").write_text('#include "FreeRTOS.h"\n')
            (temp / "freertos/semphr.h").write_text('#include "FreeRTOS.h"\n')
            (temp / "boost/sml.hpp").write_text("")
            (temp / "test.cpp").write_text(TEST)
            subprocess.run([
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                f"-I{temp}", f"-I{ROOT / 'src'}", str(temp / "test.cpp"),
                "-o", str(temp / "test"),
            ], check=True)
            subprocess.run([str(temp / "test")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
