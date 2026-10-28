#pragma once

#include "FreeRTOS.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

struct MockRecursiveSemaphore {
    std::recursive_timed_mutex mutex;
    std::mutex metadataMutex;
    std::thread::id owner;
    unsigned int depth = 0;
};
using SemaphoreHandle_t = MockRecursiveSemaphore*;

namespace freertos_mock {
inline std::atomic<unsigned int> takeAttempts{0};
}

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    return freertos_mock::failAllocation ? nullptr : new MockRecursiveSemaphore();
}

inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t ticks) {
    ++freertos_mock::takeAttempts;
    bool acquired = false;
    if (ticks == portMAX_DELAY) {
        semaphore->mutex.lock();
        acquired = true;
    } else {
        acquired = semaphore->mutex.try_lock_for(std::chrono::milliseconds(ticks));
    }
    if (!acquired) return pdFALSE;
    std::lock_guard<std::mutex> guard(semaphore->metadataMutex);
    semaphore->owner = std::this_thread::get_id();
    ++semaphore->depth;
    return pdTRUE;
}

inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) {
    {
        std::lock_guard<std::mutex> guard(semaphore->metadataMutex);
        if (semaphore->depth == 0 || semaphore->owner != std::this_thread::get_id()) {
            return pdFALSE;
        }
        if (--semaphore->depth == 0) semaphore->owner = {};
    }
    semaphore->mutex.unlock();
    return pdTRUE;
}

inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; }
