#pragma once
#include "sleep_fakes.h"
using gpio_num_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_EXT1_WAKEUP_ANY_HIGH = 1;
inline int esp_sleep_enable_ext0_wakeup(int pin, int level) {
    sleepFake.button = pin;
    sleepFake.buttonLevel = level;
    return sleepFake.wakeError ? -1 : ESP_OK;
}
inline int esp_sleep_enable_ext1_wakeup(uint64_t mask, int) {
    sleepFake.powerMask = mask;
    return sleepFake.wakeError ? -1 : ESP_OK;
}
inline int esp_sleep_enable_timer_wakeup(uint64_t us) {
    sleepFake.timerUs = us;
    return sleepFake.wakeError ? -1 : ESP_OK;
}
inline void esp_deep_sleep_start() { ++sleepFake.deepSleeps; }
