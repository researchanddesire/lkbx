#pragma once
#include <cstdint>

extern unsigned long _mock_millis;
inline int64_t esp_timer_get_time() {
    return static_cast<int64_t>(_mock_millis) * 1000LL;
}
