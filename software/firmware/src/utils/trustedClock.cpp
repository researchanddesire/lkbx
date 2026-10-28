#include "trustedClock.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_sntp.h>
#include "ClockAuthority.h"

namespace {
portMUX_TYPE clockMux = portMUX_INITIALIZER_UNLOCKED;
lockbox::security::ClockAuthority authority;
uint32_t clockEpoch = 0;
uint64_t hintMs = 0;

SemaphoreHandle_t operationMutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return mutex;
}

SemaphoreHandle_t networkTimeMutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return mutex;
}

int64_t systemWallMs() {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

int setSystemMs(int64_t ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return settimeofday(&tv, nullptr);
}
}

NetworkTimeTransaction::NetworkTimeTransaction() {
    const auto mutex = networkTimeMutex();
    acquired_ = mutex && xSemaphoreTake(mutex, 0) == pdTRUE;
}

NetworkTimeTransaction::~NetworkTimeTransaction() {
    if (acquired_) xSemaphoreGive(networkTimeMutex());
}

int lockboxGetTimeOfDay(timeval* tv, void*) {
    if (!tv) return -1;
    for (;;) {
        portENTER_CRITICAL(&clockMux);
        const uint32_t epoch = clockEpoch;
        const auto snapshot = authority;
        portEXIT_CRITICAL(&clockMux);
        const int64_t wall = systemWallMs();
        const int64_t mono = esp_timer_get_time() / 1000;
        portENTER_CRITICAL(&clockMux);
        const bool unchanged = epoch == clockEpoch;
        portEXIT_CRITICAL(&clockMux);
        if (!unchanged) continue;
        const int64_t value = snapshot.read(wall, mono);
        tv->tv_sec = value / 1000;
        tv->tv_usec = (value % 1000) * 1000;
        return 0;
    }
}

time_t lockboxTime(time_t* out) {
    timeval tv{};
    lockboxGetTimeOfDay(&tv);
    if (out) *out = tv.tv_sec;
    return tv.tv_sec;
}

int lockboxSetTimeOfDay(const timeval* tv, const void*) {
    if (!tv) return -1;
    const auto mutex = operationMutex();
    if (!mutex || xSemaphoreTake(mutex, 0) != pdTRUE) return -1;
    const int result = settimeofday(tv, nullptr);
    xSemaphoreGive(mutex);
    return result;
}

void clearNetworkTimeHint() {
    portENTER_CRITICAL(&clockMux);
    hintMs = 0;
    portEXIT_CRITICAL(&clockMux);
}

uint64_t networkTimeHintMs() {
    portENTER_CRITICAL(&clockMux);
    const uint64_t value = hintMs;
    portEXIT_CRITICAL(&clockMux);
    return value;
}

// Override ESP-IDF's weak hook: SNTP must never set the application clock.
extern "C" void sntp_sync_time(struct timeval* tv) {
    if (!tv || tv->tv_sec < 1704067200 || tv->tv_sec > 4102444799 ||
        tv->tv_usec < 0 || tv->tv_usec >= 1000000) return;
    portENTER_CRITICAL(&clockMux);
    hintMs = uint64_t(tv->tv_sec) * 1000 + tv->tv_usec / 1000;
    portEXIT_CRITICAL(&clockMux);
    sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}

bool beginTlsClockBootstrap(uint64_t hint) {
    const auto mutex = operationMutex();
    if (!mutex || xSemaphoreTake(mutex, 0) != pdTRUE) return false;
    // Keep ownership through finish. Another bootstrap cannot capture the
    // temporary TLS clock as its own application baseline.
    const int64_t wall = systemWallMs();
    const int64_t mono = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&clockMux);
    const bool accepted = authority.begin(wall, mono);
    if (accepted) ++clockEpoch;
    portEXIT_CRITICAL(&clockMux);
    if (!accepted) { xSemaphoreGive(mutex); return false; }
    if (hint && (hint < 1704067200000ULL || hint > 4102444799999ULL ||
                 setSystemMs(hint) != 0)) {
        finishTlsClockBootstrap(0);
        return false;
    }
    return true;
}

bool finishTlsClockBootstrap(uint64_t authenticatedTimeMs) {
    const auto mutex = operationMutex();
    if (!mutex || xSemaphoreGetMutexHolder(mutex) != xTaskGetCurrentTaskHandle()) return false;
    timeval previous{};
    lockboxGetTimeOfDay(&previous);
    const bool valid = authenticatedTimeMs >= 1704067200000ULL &&
                       authenticatedTimeMs <= 4102444799999ULL;
    const int64_t value = valid ? int64_t(authenticatedTimeMs)
                               : int64_t(previous.tv_sec) * 1000 + previous.tv_usec / 1000;
    const bool saved = setSystemMs(value) == 0;
    if (!saved) settimeofday(&previous, nullptr);
    portENTER_CRITICAL(&clockMux);
    authority.finish();
    ++clockEpoch;
    portEXIT_CRITICAL(&clockMux);
    xSemaphoreGive(mutex);
    return valid && saved;
}
