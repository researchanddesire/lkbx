#pragma once
#include <string>
#include <cstdint>

#define IRAM_ATTR
#define ESP_LOGD(tag, fmt, ...)

extern unsigned long _mock_millis;
inline unsigned long millis() { return _mock_millis; }

#include "sleep_fakes.h"

class String {
    std::string _s;

   public:
    String() = default;
    String(const char* s) : _s(s ? s : "") {}
    const char* c_str() const { return _s.c_str(); }
};
