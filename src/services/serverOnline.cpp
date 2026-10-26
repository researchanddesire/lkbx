#include "serverOnline.h"

#include <atomic>

namespace {
// One lock-free word makes the result and disconnect generation indivisible
// across the HTTP worker, Wi-Fi event task and BLE readers. Bit 0 is the result.
std::atomic<uint32_t> connectivity{0};
}

uint32_t beginServerHttpAttempt() {
    return connectivity.load(std::memory_order_acquire) & ~uint32_t{1};
}

void completeServerHttpAttempt(uint32_t attempt, bool success) {
    uint32_t current = connectivity.load(std::memory_order_acquire);
    while ((current & ~uint32_t{1}) == attempt) {
        if (connectivity.compare_exchange_weak(
                current, attempt | (success ? 1U : 0U),
                std::memory_order_acq_rel, std::memory_order_acquire)) return;
    }
}

void clearServerOnline() {
    uint32_t current = connectivity.load(std::memory_order_acquire);
    while (!connectivity.compare_exchange_weak(
        current, (current + 2U) & ~uint32_t{1},
        std::memory_order_acq_rel, std::memory_order_acquire)) {}
}

bool isServerOnline(bool wifiConnected) {
    if (!wifiConnected) {
        clearServerOnline();
        return false;
    }
    return (connectivity.load(std::memory_order_acquire) & 1U) != 0;
}

bool isTimeSynced = false;
