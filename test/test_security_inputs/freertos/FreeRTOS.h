#pragma once

#include <cstdint>
#include <stdexcept>

using BaseType_t = int;
using TickType_t = uint32_t;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr TickType_t portTICK_PERIOD_MS = 1;

namespace freertos_mock {
struct AssertionFailure : std::runtime_error {
    AssertionFailure() : std::runtime_error("FreeRTOS assertion") {}
};
inline bool failAllocation = false;
}

#define configASSERT(condition) \
    do { if (!(condition)) throw freertos_mock::AssertionFailure(); } while (false)
