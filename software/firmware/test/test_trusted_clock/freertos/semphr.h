#pragma once
#include <mutex>
#include <thread>
struct MockSemaphore { std::mutex mutex; std::mutex ownerMutex; std::thread::id owner; };
using SemaphoreHandle_t = MockSemaphore*;
constexpr int pdTRUE = 1;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new MockSemaphore; }
inline int xSemaphoreTake(SemaphoreHandle_t s, int) {
    if (!s->mutex.try_lock()) return 0;
    { std::lock_guard<std::mutex> lock(s->ownerMutex); s->owner = std::this_thread::get_id(); }
    return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t s) {
    { std::lock_guard<std::mutex> lock(s->ownerMutex); s->owner = {}; }
    s->mutex.unlock();
}
inline std::thread::id xSemaphoreGetMutexHolder(SemaphoreHandle_t s) {
    std::lock_guard<std::mutex> lock(s->ownerMutex); return s->owner;
}
