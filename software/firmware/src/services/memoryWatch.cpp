#include "memoryWatch.h"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "constants/LogTags.h"

namespace {
// Written by the hook, which can run in any task and at any moment an
// allocation fails, so these are single words and nothing here takes a lock,
// logs or allocates.
volatile uint32_t lastFailureAt = 0;
volatile uint32_t failures = 0;

// The clock both sides use: microseconds shifted down to units of 1.024 ms,
// which is a millisecond for this purpose and needs no division in the hook.
#define LKBX_ABOUT_MS() (static_cast<uint32_t>(esp_timer_get_time() >> 10))

// In RAM, like the heap function that calls it: it can run where code in
// flash cannot.
void IRAM_ATTR onAllocationFailed(size_t, uint32_t caps, const char *) {
    // A request for PSRAM by name says nothing about internal RAM. The
    // large page canvases ask that way and try again until they fit.
    if ((caps & MALLOC_CAP_SPIRAM) != 0) return;
    lastFailureAt = LKBX_ABOUT_MS();
    failures = failures + 1;
}
}  // namespace

void initMemoryWatch() {
    heap_caps_register_failed_alloc_callback(onAllocationFailed);
}

bool internalMemoryWasShort(uint32_t withinMs) {
    if (failures == 0) return false;
    // Unsigned subtraction keeps this correct across the counter wrapping.
    return static_cast<uint32_t>(LKBX_ABOUT_MS() - lastFailureAt) < withinMs;
}

uint32_t internalAllocationFailures() { return failures; }

void serviceMemoryWatch() {
    // Only the activity log task comes here.
    static uint32_t loggedMs = 0;
    static bool logged = false;
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (logged && static_cast<uint32_t>(now - loggedMs) < 60000) return;
    logged = true;
    loggedMs = now;
    ESP_LOGI(MEMORY_TAG,
             "Internal RAM: largest free block %u bytes, %u bytes free, "
             "lowest ever %u bytes, %u allocations failed since boot",
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(failures));
}
