#pragma once
#include <freertos/task.h>
namespace Tasks {
struct Params { const char *name; uint32_t stackSize; UBaseType_t priority; BaseType_t core; };
constexpr Params httpWorker = {"http-test", 4096, 1, 0};
}
namespace TaskHandles { inline TaskHandle_t httpWorker = nullptr; }
