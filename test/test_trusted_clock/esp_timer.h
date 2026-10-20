#pragma once
#include <cstdint>
extern int64_t mockMonotonicMs;
inline int64_t esp_timer_get_time() { return mockMonotonicMs * 1000; }
