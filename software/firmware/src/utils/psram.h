#ifndef LOCKBOX_PSRAM_H
#define LOCKBOX_PSRAM_H

#include <Esp.h>
#include <esp_heap_caps.h>

#include <cstddef>
#include <cstdint>

#include "constants/LogTags.h"
#include "esp_log.h"

// Shared startup-verification result used by every translation unit.
inline bool& lockboxPsramVerifiedState() {
    static bool verified = false;
    return verified;
}

// True when external PSRAM passed startup verification and remains available.
inline bool lockboxPsramAvailable() {
    return lockboxPsramVerifiedState() &&
           heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

// Prefer SPIRAM; fall back to internal RAM when PSRAM is missing or full.
inline void* lockboxAllocPreferPsram(size_t bytes) {
    void* ptr = nullptr;
    if (lockboxPsramAvailable()) {
        ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    }
    if (ptr == nullptr) {
        ptr = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    return ptr;
}

inline void initLockboxPsram() {
    constexpr size_t testBytes = 64 * 1024;
    lockboxPsramVerifiedState() = false;
#if CONFIG_SPIRAM_MODE_QUAD
    constexpr const char* interfaceName = "Quad";
    constexpr const char* alternateEnvironment = "development";
#elif CONFIG_SPIRAM_MODE_OCT
    constexpr const char* interfaceName = "Octal";
    constexpr const char* alternateEnvironment = "development-r2";
#else
    constexpr const char* interfaceName = "disabled";
    constexpr const char* alternateEnvironment = "a PSRAM-enabled environment";
#endif

    const size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (total == 0) {
        // Arduino-ESP32 2.0.17 reports psramFound() after a failed IDF boot
        // initialization, so the external heap is the authoritative result.
        ESP_LOGE(MEMORY_TAG,
                 "PSRAM unavailable with the compiled %s interface; try the "
                 "%s environment for the other ESP32-S3 package variant",
                 interfaceName, alternateEnvironment);
        return;
    }

    auto* test = static_cast<std::uint8_t*>(
        heap_caps_malloc(testBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (test == nullptr) {
        ESP_LOGE(MEMORY_TAG,
                 "PSRAM heap exists but a %u-byte verification allocation "
                 "failed (total=%u free=%u interface=%s)",
                 static_cast<unsigned>(testBytes), static_cast<unsigned>(total),
                 static_cast<unsigned>(free), interfaceName);
        return;
    }

    volatile std::uint8_t* const testMemory = test;
    const auto expectedAt = [](size_t offset) {
        return static_cast<std::uint8_t>(
            ((offset * 131U) ^ (offset >> 3U) ^ (offset >> 11U) ^ 0x5AU) &
            0xFFU);
    };
    for (size_t offset = 0; offset < testBytes; ++offset) {
        testMemory[offset] = expectedAt(offset);
    }
    bool verified = true;
    for (size_t offset = 0; offset < testBytes; ++offset) {
        const auto expected = expectedAt(offset);
        if (testMemory[offset] != expected) {
            ESP_LOGE(MEMORY_TAG,
                     "PSRAM verification failed at byte %u (expected=%02x "
                     "actual=%02x)",
                     static_cast<unsigned>(offset), expected,
                     testMemory[offset]);
            verified = false;
            break;
        }
    }
    heap_caps_free(test);

    if (verified) {
        lockboxPsramVerifiedState() = true;
        ESP_LOGI(MEMORY_TAG,
                 "PSRAM verified: %u bytes total, %u free, %s interface, "
                 "%u-byte write/read test passed",
                 static_cast<unsigned>(total), static_cast<unsigned>(free),
                 interfaceName, static_cast<unsigned>(testBytes));
    }
}

#endif  // LOCKBOX_PSRAM_H
