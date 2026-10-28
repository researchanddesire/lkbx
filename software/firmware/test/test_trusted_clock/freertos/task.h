#pragma once
#include <thread>
inline std::thread::id xTaskGetCurrentTaskHandle() { return std::this_thread::get_id(); }
